// vdx::Instrument: queues, MIDI scheduling, resampling, output stage (SPEC
// §7.1 R-E1, R-E10, R-E15; §9.4).
//
// Timing model. Host sample h maps to engine time h * engineRate / hostRate.
// Internal block b covers engine samples [64b, 64b + 64). A received MIDI
// message stamped with host time h is applied at the start of the block that
// contains it. Block b is rendered in the render() call during which its end
// passes, so the block containing an event is never rendered before the event
// arrives; the result is independent of the host block size (AC-E15.3). The
// output FIFO is pre-filled with latencySamples() zeros: one block of
// buffering plus the resampler's look-ahead (AC-E1.1).
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

#include "CDSPResampler.h"
#include "core.h"
#include "util/midi_parser.h"
#include "util/spsc.h"
#include "vdx/vdx.h"

namespace vdx {

namespace {
constexpr size_t kCmdQueue = 4096;
constexpr size_t kEvQueue = 4096;
constexpr int kPayloadSlots = 8;
constexpr int kPayloadBytes = 8192;
constexpr size_t kMidiOutBytes = 1 << 16;
constexpr int kPendingMsgs = 2048;
constexpr int kPendingBytes = 1 << 16;
constexpr size_t kFifo = 1 << 16;
constexpr int kMaxChunk = 4096;
// r8brain 24-bit preset; 15 % transition band: ~4.4 ms look-ahead, passband ~20 kHz (DECISIONS D-E21).
constexpr double kResamplerTransBand = 15.0;
}  // namespace

struct Instrument::Impl final : EventSink, MidiOutSink {
  Impl() : core(*this, *this) {}

  // ---- EventSink / MidiOutSink (audio thread) ----
  bool push(const Event& e) override {
    if (!evQ.push(e)) {
      dropped.fetch_add(1, std::memory_order_relaxed);
      return false;
    }
    return true;
  }
  bool pushPayload(Event e, const uint8_t* data, int len) override {
    const int slot = evPool.acquire();
    if (slot < 0 || len > evPool.slotBytes()) {
      if (slot >= 0) evPool.release(slot);
      dropped.fetch_add(1, std::memory_order_relaxed);
      return false;
    }
    std::memcpy(evPool.data(slot), data, size_t(len));
    e.slot = uint32_t(slot);
    e.len = uint32_t(len);
    if (!evQ.push(e)) {
      evPool.release(slot);
      dropped.fetch_add(1, std::memory_order_relaxed);
      return false;
    }
    return true;
  }
  float fill() const override { return float(evQ.size()) / float(evQ.capacity()); }
  void send(const uint8_t* msg, int len) override { midiOut.push(msg, len); }

  // ---- configuration (not real-time) ----
  void configure(double host, int maxBlk) {
    hostRate = host > 0 ? host : 48000.0;
    maxBlock = std::max(1, maxBlk);
    // Build the resampler whenever the host rate differs from the internal
    // rate, even in host-rate mode, so toggling the setting never allocates.
    if (std::fabs(hostRate - kInternalRate) > 1e-6) {
      rs = std::make_unique<r8b::CDSPResampler24>(kInternalRate, hostRate, kBlock, kResamplerTransBand);
      rsHost = hostRate;
      lookahead = rs->getInLenBeforeOutStart(0);
    } else {
      rs.reset();
      rsHost = 0.0;
    }
    applyRateMode(core.settings().hostRate, /*allowAlloc=*/false);
  }

  // Real-time safe unless allowAlloc (the resampler is built only then).
  void applyRateMode(bool hostRateMode, bool allowAlloc) {
    rateModeHost = hostRateMode;
    engineRate = hostRateMode ? hostRate : kInternalRate;
    useResampler = std::fabs(engineRate - hostRate) > 1e-6;
    hostRateI = int64_t(std::llround(hostRate));
    engineRateI = int64_t(std::llround(engineRate));
    if (useResampler && rs) {
      (void)allowAlloc;
      rs->clear();
      latency = int(std::ceil(double(kBlock + lookahead) * hostRate / engineRate)) + 1;
    } else {
      useResampler = false;
      latency = kBlock;
    }
    core.prepare(engineRate);
    fifoRead = fifoWrite = 0;
    for (int i = 0; i < latency; ++i) fifo[(fifoWrite++) & (kFifo - 1)] = 0.f;
    hostTime = 0;
    blockIndex = 0;
    pendHead = pendTail = 0;
    pendByteHead = pendByteTail = 0;
    parser.reset();
    cmdParser.reset();
  }

  // ---- pending timestamped MIDI (audio thread only) ----
  void schedule(const uint8_t* msg, int len, int64_t time) {
    if (pendTail - pendHead >= kPendingMsgs) return;
    if (int(pendByteTail - pendByteHead) + len > kPendingBytes) return;
    Pending& p = pend[size_t(pendTail % kPendingMsgs)];
    p.time = time;
    p.off = pendByteTail;
    p.len = len;
    for (int i = 0; i < len; ++i) pendBytes[size_t((pendByteTail + int64_t(i)) % kPendingBytes)] = msg[i];
    pendByteTail += len;
    ++pendTail;
  }
  int64_t blockOf(int64_t h) const {
    // floor(h * engineRate / (64 * hostRate)), h >= 0
    return (h * engineRateI) / (int64_t(kBlock) * hostRateI);
  }
  void applyDueMidi(int64_t block) {
    while (pendHead < pendTail) {
      Pending& p = pend[size_t(pendHead % kPendingMsgs)];
      if (blockOf(p.time) > block) break;
      uint8_t* m = scratchMsg;
      for (int i = 0; i < p.len; ++i) m[i] = pendBytes[size_t((p.off + int64_t(i)) % kPendingBytes)];
      core.midiMessage(m, p.len);
      pendByteHead = p.off + p.len;
      ++pendHead;
    }
  }

  void renderOneBlock() {
    applyDueMidi(blockIndex);
    core.renderBlock(tap, block);
    if (useResampler) {
      const double dc = (blockIndex & 1) ? -1e-20 : 1e-20;  // R-E15 denormal guard
      for (int i = 0; i < kBlock; ++i) rsIn[i] = double(block[i]) + dc;
      double* op = nullptr;
      const int k = rs->process(rsIn, kBlock, op);
      for (int i = 0; i < k; ++i) fifo[(fifoWrite++) & (kFifo - 1)] = float(op[i]);
    } else {
      for (int i = 0; i < kBlock; ++i) fifo[(fifoWrite++) & (kFifo - 1)] = block[i];
    }
    ++blockIndex;
  }

  void drainCommands() {
    Command c;
    while (cmdQ.pop(c)) {
      const uint8_t* payload = nullptr;
      int len = 0;
      if (c.slot != 0xFFFFFFFFu) {
        payload = cmdPool.data(int(c.slot));
        len = int(c.len);
      }
      if (c.type == CmdType::MidiBytes) {
        const uint8_t* bytes = payload ? payload : c.data;
        const int n = payload ? len : int(c.len);
        cmdParser.feed(bytes, n, [this](const uint8_t* m, int l) { schedule(m, l, hostTime); });
      } else {
        core.command(c, payload, len);
      }
      if (c.slot != 0xFFFFFFFFu) cmdPool.release(int(c.slot));
    }
  }

  void applyPendingLoad() {
    int expected = 2;
    if (!loadFlag.compare_exchange_strong(expected, 3, std::memory_order_acq_rel)) return;
    core.deserialize(loadBuf.data(), loadLen);
    appliedGen = loadGenPending;
    loadFlag.store(0, std::memory_order_release);
    publishSnapshot();
  }

  void publishSnapshot() {
    if (appliedGen != loadGen.load(std::memory_order_acquire)) return;
    bool expected = false;
    if (!snapWriter.compare_exchange_strong(expected, true, std::memory_order_acquire)) {
      snapshotPending = true;
      return;
    }
    const size_t n = core.serialize(serial.data(), serial.size());
    snapshot.publish(serial.data(), n);
    snapWriter.store(false, std::memory_order_release);
    snapshotPending = false;
  }

  void render(float* L, float* R, int n) {
    if (n <= 0 || L == nullptr) return;
    applyPendingLoad();
    drainCommands();
    if (core.settings().hostRate != rateModeHost) applyRateMode(core.settings().hostRate, false);

    int done = 0;
    while (done < n) {
      const int chunk = std::min(kMaxChunk, n - done);
      const int64_t end = hostTime + chunk;
      // Render every block whose end lies within this chunk.
      while (int64_t(kBlock) * (blockIndex + 1) * hostRateI <= end * engineRateI) renderOneBlock();
      const float v = float(core.masterVolume()) / 127.f;
      const float gain = v * v;
      for (int i = 0; i < chunk; ++i) {
        float y = 0.f;
        if (fifoRead != fifoWrite) y = fifo[(fifoRead++) & (kFifo - 1)];
        else ++underruns;
        y *= gain;
        y = y > 1.f ? 1.f : (y < -1.f ? -1.f : y);  // R-E10 hard clip
        L[done + i] = y;
      }
      if (R != nullptr && R != L) std::memcpy(R + done, L + done, sizeof(float) * size_t(chunk));
      hostTime = end;
      done += chunk;
    }
    if (core.takeStateDirty() || snapshotPending) publishSnapshot();
  }

  // ---- members ----
  util::SpscQueue<Command> cmdQ{kCmdQueue};
  util::SpscQueue<Event> evQ{kEvQueue};
  util::SlotPool cmdPool{kPayloadSlots, kPayloadBytes};
  util::SlotPool evPool{kPayloadSlots, kPayloadBytes};
  util::SpscByteQueue midiOut{kMidiOutBytes};
  std::atomic<uint32_t> dropped{0};

  Core core;

  double hostRate = 48000.0;
  int maxBlock = 512;
  double engineRate = kInternalRate;
  bool rateModeHost = false;
  bool useResampler = true;
  int64_t hostRateI = 48000, engineRateI = 49096;
  std::unique_ptr<r8b::CDSPResampler24> rs;
  double rsHost = 0.0;
  int lookahead = 0;
  int latency = 0;
  double rsIn[kBlock] = {};
  int32_t tap[kBlock] = {};
  float block[kBlock] = {};

  std::vector<float> fifo = std::vector<float>(kFifo, 0.f);
  uint64_t fifoRead = 0, fifoWrite = 0;
  uint64_t underruns = 0;

  int64_t hostTime = 0;
  int64_t blockIndex = 0;

  struct Pending {
    int64_t time;
    int64_t off;
    int len;
  };
  std::vector<Pending> pend = std::vector<Pending>(kPendingMsgs);
  std::vector<uint8_t> pendBytes = std::vector<uint8_t>(kPendingBytes);
  int64_t pendHead = 0, pendTail = 0;
  int64_t pendByteHead = 0, pendByteTail = 0;
  uint8_t scratchMsg[util::kMaxSysEx] = {};
  util::MidiParser parser;
  util::MidiParser cmdParser;

  // state
  util::SnapshotBuffer snapshot{kMaxStateBytes};
  std::vector<uint8_t> serial = std::vector<uint8_t>(kMaxStateBytes);
  std::atomic<bool> snapWriter{false};
  bool snapshotPending = false;
  std::atomic<int> loadFlag{0};  // 0 free, 1 writing, 2 ready, 3 applying
  std::vector<uint8_t> loadBuf = std::vector<uint8_t>(kMaxStateBytes);
  size_t loadLen = 0;
  std::atomic<uint32_t> loadGen{0};
  uint32_t loadGenPending = 0;
  uint32_t appliedGen = 0;
};

Instrument::Instrument(double hostSampleRate, int maxBlock) : impl_(std::make_unique<Impl>()) {
  impl_->configure(hostSampleRate, maxBlock);
  impl_->publishSnapshot();
}

Instrument::~Instrument() = default;

void Instrument::prepare(double hostSampleRate, int maxBlock) { impl_->configure(hostSampleRate, maxBlock); }

bool Instrument::post(const Command& c) {
  Command copy = c;
  copy.slot = 0xFFFFFFFFu;
  if (c.type != CmdType::MidiBytes) copy.len = 0;
  return impl_->cmdQ.push(copy);
}

bool Instrument::postPayload(Command c, const uint8_t* data, int len) {
  if (len <= 0) {
    c.slot = 0xFFFFFFFFu;
    c.len = 0;
    return impl_->cmdQ.push(c);
  }
  if (len > impl_->cmdPool.slotBytes()) return false;
  int slot = -1;
  for (int tries = 0; tries < 2000 && slot < 0; ++tries) {
    slot = impl_->cmdPool.acquire();
    if (slot < 0) std::this_thread::yield();
  }
  if (slot < 0) return false;
  std::memcpy(impl_->cmdPool.data(slot), data, size_t(len));
  c.slot = uint32_t(slot);
  c.len = uint32_t(len);
  if (!impl_->cmdQ.push(c)) {
    impl_->cmdPool.release(slot);
    return false;
  }
  return true;
}

bool Instrument::pollEvent(Event& e) { return impl_->evQ.pop(e); }

int Instrument::readPayload(const Event& e, uint8_t* dst, int cap) {
  if (e.slot == 0xFFFFFFFFu) return 0;
  const int n = std::min(int(e.len), cap);
  if (dst && n > 0) std::memcpy(dst, impl_->evPool.data(int(e.slot)), size_t(n));
  impl_->evPool.release(int(e.slot));
  return n;
}

int Instrument::readMidiOut(uint8_t* dst, int cap) { return impl_->midiOut.pop(dst, cap); }

void Instrument::midi(const uint8_t* bytes, int len, int sampleOffset) {
  if (!bytes || len <= 0) return;
  const int64_t t = impl_->hostTime + std::max(0, sampleOffset);
  impl_->parser.feed(bytes, len, [this, t](const uint8_t* m, int l) { impl_->schedule(m, l, t); });
}

void Instrument::render(float* L, float* R, int n) { impl_->render(L, R, n); }

int Instrument::latencySamples() const { return impl_->latency; }

void Instrument::saveState(StateBlob& blob) const {
  // A pending loadState() is reported until the audio thread applies it.
  Impl& d = *impl_;
  d.snapshot.read(blob.bytes);
}

bool Instrument::loadState(const StateBlob& blob) {
  Impl& d = *impl_;
  const auto result = Core::validate(blob.bytes.data(), blob.bytes.size());
  if (blob.bytes.size() > d.loadBuf.size()) return false;
  for (;;) {
    int expected = 0;
    if (d.loadFlag.compare_exchange_strong(expected, 1, std::memory_order_acq_rel)) break;
    expected = 2;
    if (d.loadFlag.compare_exchange_strong(expected, 1, std::memory_order_acq_rel)) break;
    std::this_thread::yield();
  }
  if (!blob.bytes.empty()) std::memcpy(d.loadBuf.data(), blob.bytes.data(), blob.bytes.size());
  d.loadLen = blob.bytes.size();
  d.loadGenPending = d.loadGen.fetch_add(1, std::memory_order_acq_rel) + 1;
  // Publish the incoming state right away so saveState() is consistent before
  // the audio thread applies it (valid blobs only; others become first-run).
  if (result == Core::LoadResult::Ok) {
    while (d.snapWriter.exchange(true, std::memory_order_acquire)) std::this_thread::yield();
    d.snapshot.publish(blob.bytes.data(), blob.bytes.size());
    d.snapWriter.store(false, std::memory_order_release);
  }
  d.loadFlag.store(2, std::memory_order_release);
  return result != Core::LoadResult::Reset;
}

void Instrument::applyNow(const Command& c) { impl_->core.command(c, nullptr, 0); }
Core& Instrument::core() { return impl_->core; }
const Core& Instrument::core() const { return impl_->core; }
double Instrument::hostRate() const { return impl_->hostRate; }
uint32_t Instrument::droppedEvents() const { return impl_->dropped.load(); }

}  // namespace vdx
