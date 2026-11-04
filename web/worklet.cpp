// VDX web module (SPEC R-E1 Web, §15.2): vdx_core + vdx_bridge compiled with
// Emscripten. The browser main thread owns the Instrument's producer/consumer
// side (commands, events, state, MIDI) through the C API below; rendering runs
// in an AudioWorklet (Wasm Audio Worklets, shared memory). Before audio starts
// — or if the AudioContext cannot start — the main thread renders silent
// blocks (vdx_pump) so the panel keeps working (§5.9).
#include <emscripten.h>
#include <emscripten/webaudio.h>

#include <atomic>
#include <cstring>
#include <string>
#include <vector>

#include "bridge.h"
#include "vdx/vdx.h"

namespace {

vdx::Instrument* g_inst = nullptr;
vdx::bridge::Bridge* g_bridge = nullptr;
std::string g_reply;
std::string g_events;
std::vector<uint8_t> g_state;
std::vector<vdx::bridge::ExportFile> g_exports;
std::atomic<int> g_audio{0};  // 0 idle, 1 starting, 2 running, -1 failed
EMSCRIPTEN_WEBAUDIO_T g_node = 0;
alignas(16) uint8_t g_workletStack[128 * 1024];
float g_scratch[2][4096];

bool process(int, const AudioSampleFrame*, int numOutputs, AudioSampleFrame* outputs, int, const AudioParamFrame*,
             void*) {
  if (numOutputs < 1 || !g_inst) return true;
  AudioSampleFrame& o = outputs[0];
  const int n = o.samplesPerChannel;
  float* L = o.data;
  float* R = o.numberOfChannels > 1 ? o.data + n : nullptr;
  g_inst->render(L, R, n);
  for (int c = 2; c < o.numberOfChannels; ++c) std::memcpy(o.data + c * n, L, sizeof(float) * size_t(n));
  return true;
}

void nodeCreated(EMSCRIPTEN_WEBAUDIO_T ctx, bool ok, void*) {
  if (!ok) {
    g_audio = -1;
    return;
  }
  int channels[1] = {2};
  EmscriptenAudioWorkletNodeCreateOptions opts = {};
  opts.numberOfInputs = 0;
  opts.numberOfOutputs = 1;
  opts.outputChannelCounts = channels;
  g_node = emscripten_create_wasm_audio_worklet_node(ctx, "vdx", &opts, &process, nullptr);
  emscripten_audio_node_connect(g_node, ctx, 0, 0);
  g_audio = 2;
  EM_ASM({ if (Module.onVdxNode) Module.onVdxNode($0); }, g_node);
}

void threadStarted(EMSCRIPTEN_WEBAUDIO_T ctx, bool ok, void*) {
  if (!ok) {
    g_audio = -1;
    return;
  }
  WebAudioWorkletProcessorCreateOptions opts = {};
  opts.name = "vdx";
  emscripten_create_wasm_audio_worklet_processor_async(ctx, &opts, &nodeCreated, nullptr);
}

}  // namespace

extern "C" {

// Creates the instrument. rate = host rate of the AudioContext (49096 when the
// context accepted it). Called once at page load (with 49096) before state is
// loaded; vdx_prepare() switches to the real rate before audio starts.
EMSCRIPTEN_KEEPALIVE void vdx_init(double rate) {
  if (g_inst) return;
  g_inst = new vdx::Instrument(rate, 4096);
  g_bridge = new vdx::bridge::Bridge(*g_inst);
}

EMSCRIPTEN_KEEPALIVE void vdx_prepare(double rate) {
  if (g_inst && g_audio.load() != 2) g_inst->prepare(rate, 4096);
}

EMSCRIPTEN_KEEPALIVE int vdx_latency() { return g_inst ? g_inst->latencySamples() : 0; }

// UI command (JSON, docs/PROTOCOL.md). Returns the reply JSON array ("" for shell commands).
EMSCRIPTEN_KEEPALIVE const char* vdx_command(const char* json) {
  g_reply = g_bridge ? g_bridge->handle(json ? json : "") : std::string();
  return g_reply.c_str();
}

// Pending core events as a JSON array ("" if none).
EMSCRIPTEN_KEEPALIVE const char* vdx_poll() {
  g_events.clear();
  if (g_bridge) g_events = g_bridge->drain([](vdx::bridge::ExportFile&& f) { g_exports.push_back(std::move(f)); });
  return g_events.c_str();
}

EMSCRIPTEN_KEEPALIVE int vdx_state_dirty() { return g_bridge && g_bridge->takeStateDirty() ? 1 : 0; }

// Snapshot the state blob; read it with vdx_state_ptr().
EMSCRIPTEN_KEEPALIVE int vdx_save_state() {
  if (!g_inst) return 0;
  vdx::StateBlob b;
  g_inst->saveState(b);
  g_state = std::move(b.bytes);
  return int(g_state.size());
}
EMSCRIPTEN_KEEPALIVE const uint8_t* vdx_state_ptr() { return g_state.data(); }

// Returns 1 when the blob was loaded (or empty), 0 when it was reset (§4.8).
EMSCRIPTEN_KEEPALIVE int vdx_load_state(const uint8_t* data, int len) {
  if (!g_inst) return 0;
  vdx::StateBlob b;
  if (data && len > 0) b.bytes.assign(data, data + len);
  return g_inst->loadState(b) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE uint8_t* vdx_alloc(int n) { return static_cast<uint8_t*>(::operator new(size_t(n > 0 ? n : 1))); }
EMSCRIPTEN_KEEPALIVE void vdx_free(uint8_t* p) { ::operator delete(p); }

// Exports produced by the core (ExportData events), for downloads.
EMSCRIPTEN_KEEPALIVE int vdx_export_count() { return int(g_exports.size()); }
EMSCRIPTEN_KEEPALIVE const uint8_t* vdx_export_ptr(int i) { return g_exports[size_t(i)].bytes.data(); }
EMSCRIPTEN_KEEPALIVE int vdx_export_len(int i) { return int(g_exports[size_t(i)].bytes.size()); }
EMSCRIPTEN_KEEPALIVE const char* vdx_export_name(int i) { return g_exports[size_t(i)].name.c_str(); }
EMSCRIPTEN_KEEPALIVE int vdx_export_kind(int i) { return int(g_exports[size_t(i)].kind); }
EMSCRIPTEN_KEEPALIVE void vdx_export_clear() { g_exports.clear(); }

// Received Web MIDI message(s) (main thread).
EMSCRIPTEN_KEEPALIVE void vdx_midi_in(const uint8_t* bytes, int len) {
  if (!g_inst || !bytes || len <= 0) return;
  vdx::Command c;
  c.type = vdx::CmdType::MidiBytes;
  c.src = vdx::Source::Midi;
  if (len <= int(sizeof c.data)) {
    std::memcpy(c.data, bytes, size_t(len));
    c.len = uint32_t(len);
    g_inst->post(c);
  } else {
    g_inst->postPayload(c, bytes, len);
  }
}

// Transmitted MIDI bytes (complete messages) for the Web MIDI output.
EMSCRIPTEN_KEEPALIVE int vdx_midi_out(uint8_t* dst, int cap) { return g_inst ? g_inst->readMidiOut(dst, cap) : 0; }

// Main-thread rendering while no AudioWorklet runs (before the start gesture,
// or when the AudioContext cannot start).
EMSCRIPTEN_KEEPALIVE void vdx_pump(int frames) {
  if (!g_inst || g_audio.load() == 2) return;
  while (frames > 0) {
    const int n = frames > 4096 ? 4096 : frames;
    g_inst->render(g_scratch[0], g_scratch[1], n);
    frames -= n;
  }
}

// Starts rendering in an AudioWorklet of the AudioContext registered by JS
// (emscriptenRegisterAudioObject). Poll vdx_audio_state(): 2 running, -1 failed.
EMSCRIPTEN_KEEPALIVE void vdx_start_audio(EMSCRIPTEN_WEBAUDIO_T ctx) {
  if (g_audio.load() != 0) return;
  g_audio = 1;
  emscripten_start_wasm_audio_worklet_thread_async(ctx, g_workletStack, sizeof g_workletStack, &threadStarted, nullptr);
}

EMSCRIPTEN_KEEPALIVE int vdx_audio_state() { return g_audio.load(); }

}  // extern "C"
