// MIDI byte-stream parser (R-M2): running status, SysEx reassembly across
// calls up to 4104 bytes (longer SysEx is discarded silently), realtime bytes
// inside SysEx are not stored. Allocation-free.
#pragma once

#include <cstdint>

namespace vdx::util {

constexpr int kMaxSysEx = 4104;

class MidiParser {
 public:
  // Sink receives each complete message: status byte first; SysEx as F0..F7.
  template <typename Sink>
  void feed(const uint8_t* bytes, int len, Sink&& sink) {
    for (int i = 0; i < len; ++i) {
      const uint8_t b = bytes[i];
      if (b >= 0xF8) {  // realtime: acted on (here: ignored), never stored
        continue;
      }
      if (inSysEx_) {
        if (b == 0xF7) {
          if (!sysexOverflow_ && sysexLen_ < kMaxSysEx) {
            sysex_[sysexLen_++] = b;
            sink(sysex_, sysexLen_);
          }
          inSysEx_ = false;
          continue;
        }
        if (b < 0x80) {
          if (sysexLen_ < kMaxSysEx) sysex_[sysexLen_++] = b;
          else sysexOverflow_ = true;
          continue;
        }
        // Any other status byte aborts the unterminated SysEx; fall through.
        inSysEx_ = false;
      }
      if (b == 0xF0) {
        inSysEx_ = true;
        sysexOverflow_ = false;
        sysexLen_ = 0;
        sysex_[sysexLen_++] = b;
        running_ = 0;
        continue;
      }
      if (b >= 0x80) {
        if (b >= 0xF0) {  // system common: cancels running status
          running_ = 0;
          common_ = b;
          need_ = (b == 0xF2) ? 2 : (b == 0xF1 || b == 0xF3) ? 1 : 0;
          have_ = 0;
          if (need_ == 0) {
            msg_[0] = b;
            if (b != 0xF7) sink(msg_, 1);
            common_ = 0;
          }
          continue;
        }
        running_ = b;
        common_ = 0;
        need_ = dataBytes(b);
        have_ = 0;
        continue;
      }
      // data byte
      const uint8_t status = common_ ? common_ : running_;
      if (!status) continue;
      msg_[1 + have_] = b;
      ++have_;
      if (have_ >= need_) {
        msg_[0] = status;
        sink(msg_, 1 + need_);
        have_ = 0;
        if (common_) common_ = 0;
      }
    }
  }
  void reset() {
    inSysEx_ = false;
    sysexOverflow_ = false;
    sysexLen_ = 0;
    running_ = common_ = 0;
    need_ = have_ = 0;
  }

 private:
  static int dataBytes(uint8_t status) {
    const uint8_t hi = status & 0xF0;
    return (hi == 0xC0 || hi == 0xD0) ? 1 : 2;
  }
  uint8_t sysex_[kMaxSysEx];
  int sysexLen_ = 0;
  bool inSysEx_ = false;
  bool sysexOverflow_ = false;
  uint8_t msg_[4] = {};
  uint8_t running_ = 0;
  uint8_t common_ = 0;
  int need_ = 0;
  int have_ = 0;
};

}  // namespace vdx::util
