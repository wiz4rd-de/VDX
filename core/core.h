// vdx::Core — all synth and front-panel state (SPEC §9.4). Single-threaded:
// only the audio thread touches it, through vdx::Instrument.
#pragma once

#include <cstddef>
#include <cstdint>

#include "fm/engine.h"
#include "vdx/vdx.h"

namespace vdx {

// Implemented by Instrument: the lock-free event queue and payload pool.
class EventSink {
 public:
  virtual ~EventSink() = default;
  virtual bool push(const Event&) = 0;  // false when the queue is full (event dropped)
  virtual bool pushPayload(Event, const uint8_t* data, int len) = 0;
  // Fill level 0..1 of the event queue (for the drop policy: meters first).
  virtual float fill() const = 0;
};

// Implemented by Instrument: transmitted MIDI (one complete message per call).
class MidiOutSink {
 public:
  virtual ~MidiOutSink() = default;
  virtual void send(const uint8_t* msg, int len) = 0;
};

// Maximum size of a serialised state blob (§4.8).
constexpr size_t kMaxStateBytes = 16384;

class Core {
 public:
  Core(EventSink& events, MidiOutSink& midiOut);
  ~Core();
  Core(const Core&) = delete;
  Core& operator=(const Core&) = delete;

  // Real-time safe. engineRate = kInternalRate, or the host rate when the
  // "render at host rate" setting is on. Keeps all state; stops voices.
  void prepare(double engineRate);
  double engineRate() const;

  // Apply one command (payload may be null). Audio thread.
  void command(const Command& cmd, const uint8_t* payload, int len);
  // Apply one complete received MIDI message (status byte first; SysEx F0..F7). Audio thread.
  void midiMessage(const uint8_t* msg, int len);
  // Render one block of kBlock samples at the engine rate and advance all
  // timers (splash, messages, auto-repeat, transmit throttling) by one block.
  void renderBlock(int32_t* dacTap, float* out);

  // Current engine settings (Instrument reacts to hostRate changes).
  const Settings& settings() const;
  int masterVolume() const;  // 0..127 (output stage, R-E10)

  // State (§4.8). serialize() is RT-safe (fixed buffer); returns bytes written.
  size_t serialize(uint8_t* dst, size_t cap) const;
  enum class LoadResult { Ok, Reset, Empty };
  // Applies a blob: Ok (loaded, incl. newer versions), Reset (discarded ->
  // first-run state, STATE RESET shown after the splash), Empty (zero-length ->
  // first-run state, no message). Real-time safe.
  LoadResult deserialize(const uint8_t* data, size_t len);
  // Validate a blob without applying it (any thread).
  static LoadResult validate(const uint8_t* data, size_t len);
  // Set whenever saved state changes; Instrument republishes the snapshot.
  bool takeStateDirty();

  fm::Engine& engine();

  struct Impl;
  Impl& impl();
  const Impl& impl() const;

 private:
  Impl* impl_;
};

}  // namespace vdx
