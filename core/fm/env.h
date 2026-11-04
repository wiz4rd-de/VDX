/*
 * Portions ported from MSFA as distributed with Dexed (source/msfa):
 *   Copyright 2012, 2013 Google Inc.; Dexed contributors.
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *        http://www.apache.org/licenses/LICENSE-2.0
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 * Modified for VDX: per-sample clock, gate masks and hold rescaling (SPEC R-E4).
 */
// Operator envelope generator (SPEC R-E4). Ports the semantics of MSFA/Dexed
// env.cc (targets, attack shape, holds, key-off to segment 4) and replaces
// only the timing: one state update per internal-rate clock, with the
// hardware-style tick period and 8-step gate masks.
#pragma once

#include <cstdint>

#include "fm/dx7tables.h"

namespace vdx::fm {

inline int scaleoutlevel(int x) { return x >= 20 ? 28 + x : tab::levellut[x < 0 ? 0 : x]; }

class Env {
 public:
  static constexpr int kJumpTarget = 1716;
  static constexpr uint8_t kGate[4] = {0x55, 0x57, 0x77, 0x7F};  // 01010101 01010111 01110111 01111111

  // rates/levels: effective 0..99. outlevel: R-E5 value. rateScale: ScaleRate qrate delta.
  void init(const int r[4], const int l[4], int outlevel, int rateScale) {
    set(r, l, outlevel, rateScale);
    level_ = 0;
    down_ = true;
    advance(0);
  }
  // Voice data changed while sounding: retarget the current segment.
  void update(const int r[4], const int l[4], int outlevel, int rateScale) {
    set(r, l, outlevel, rateScale);
    if (ix_ < 3) advance(ix_);
    else if (ix_ == 3) advance(down_ ? 2 : 3);
  }
  void keyOff() {
    if (down_) {
      down_ = false;
      advance(3);
    }
  }
  void silence() {
    level_ = 0;
    ix_ = 4;
    down_ = false;
    hold_ = 0;
  }

  // One EG clock; c = the engine-wide clock counter.
  inline void clock(uint32_t c) {
    if (ix_ >= 4 || (ix_ == 3 && down_)) return;
    if (hold_ > 0) {
      if (--hold_ == 0) advance(ix_ + 1);
      return;
    }
    if (c & tmask_) return;  // not a tick
    const uint32_t tick = c >> tshift_;
    if (!((gate_ >> (7 - (tick & 7))) & 1)) return;
    if (rising_) {
      level_ += step_ * (17 - (level_ >> 8));
      if (level_ >= target_) {
        level_ = target_;
        advance(ix_ + 1);
      }
    } else {
      level_ -= step_;
      if (level_ <= target_) {
        level_ = target_;
        advance(ix_ + 1);
      }
    }
  }

  int level() const { return level_; }
  int target() const { return target_; }
  int stage() const { return ix_; }
  bool down() const { return down_; }
  bool rising() const { return rising_; }
  int qrate() const { return qrate_; }
  int hold() const { return hold_; }
  // Test support: start from an arbitrary level.
  void forceLevel(int l) { level_ = l; }
  void forceStage(int ix) { advance(ix); }

  static int targetLevel(int levelParam, int outlevel) {
    int actual = ((scaleoutlevel(levelParam) >> 1) << 6) + outlevel - 4256;
    return actual < 16 ? 16 : (actual > 4095 ? 4095 : actual);
  }
  // Hold length in internal-rate clocks for a segment of rate r (+ scaling).
  static int holdLength(int rate, int rateScale, bool attackToZero) {
    int sr = rate + rateScale;
    if (sr > 99) sr = 99;
    if (sr < 77) {
      int64_t cnt = tab::statics[sr];
      if (attackToZero) cnt /= 20;
      return int((cnt * 49096 + 22050) / 44100);
    }
    return 20 * (99 - sr);  // AC-E4.4: not rescaled
  }

 private:
  void set(const int r[4], const int l[4], int outlevel, int rateScale) {
    for (int i = 0; i < 4; ++i) {
      rates_[i] = r[i];
      levels_[i] = l[i];
    }
    outlevel_ = outlevel;
    rateScale_ = rateScale;
  }
  void advance(int newix) {
    for (;;) {
      ix_ = newix;
      hold_ = 0;
      if (ix_ >= 4) return;
      const int newlevel = levels_[ix_];
      target_ = targetLevel(newlevel, outlevel_);
      rising_ = target_ > level_;
      int q = ((rates_[ix_] * 41) >> 6) + rateScale_;
      qrate_ = q > 63 ? 63 : q;
      if (qrate_ < 48) {
        tshift_ = 11 - (qrate_ >> 2);
        tmask_ = (1u << tshift_) - 1u;
        step_ = 1;
      } else {
        tshift_ = 0;
        tmask_ = 0;
        step_ = 1 << ((qrate_ >> 2) - 11);
      }
      gate_ = kGate[qrate_ & 3];
      const bool attackToZero = (ix_ == 0 && newlevel == 0);
      if (target_ == level_ || attackToZero) {
        hold_ = holdLength(rates_[ix_], rateScale_, attackToZero);
        if (hold_ > 0) return;
        if (ix_ == 3 && down_) return;
      }
      if (rising_ && !(ix_ == 3 && down_)) {
        if (level_ < kJumpTarget) level_ = kJumpTarget;  // attack start
        if (level_ >= target_) {
          level_ = target_;
          newix = ix_ + 1;
          continue;
        }
      }
      return;
    }
  }

  int rates_[4] = {99, 99, 99, 99};
  int levels_[4] = {99, 99, 99, 0};
  int outlevel_ = 0;
  int rateScale_ = 0;
  int level_ = 0;
  int target_ = 16;
  int ix_ = 4;
  bool down_ = false;
  bool rising_ = false;
  int qrate_ = 0;
  uint32_t tmask_ = 0;
  int tshift_ = 0;
  int step_ = 1;
  uint8_t gate_ = 0x55;
  int hold_ = 0;
};

}  // namespace vdx::fm
