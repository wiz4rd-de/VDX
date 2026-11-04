/*
 * LFO and pitch envelope, ported from MSFA as distributed with Dexed
 * (source/msfa/lfo.cc, pitchenv.cc):
 *   Copyright 2013 Google Inc.
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *        http://www.apache.org/licenses/LICENSE-2.0
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 *
 * Modified for VDX:
 *   - per-instance rate units instead of statics (several engines per process);
 *   - LFO frequency = lfoSource[] exactly (ratio 2^32 * N / fs instead of
 *     Dexed's 4437500000 * N / fs, which ran 3.3 % fast; SPEC AC-E7.1);
 *   - effective parameters passed as ints.
 */
#pragma once

#include <cstdint>

#include "fm/dx7tables.h"
#include "fm/msfa_sin.h"

namespace vdx::fm {

constexpr int kN = 64;

class Lfo {
 public:
  void init(double sampleRate) {
    unit_ = uint32_t(kN * 25190424 / sampleRate + 0.5);
    ratio_ = 4294967296.0 * kN / sampleRate;
  }
  // speed, delay, pmd, amd, keysync, wave (effective values)
  void reset(int speed, int delay, int keySync, int wave) {
    delta_ = uint32_t(tab::lfoSource[speed] * ratio_);
    int a = 99 - delay;
    if (a == 99) {
      delayinc_ = ~0u;
      delayinc2_ = ~0u;
    } else {
      a = (16 + (a & 15)) << (1 + (a >> 4));
      delayinc_ = unit_ * uint32_t(a);
      a &= 0xff80;
      a = a > 0x80 ? a : 0x80;
      delayinc2_ = unit_ * uint32_t(a);
    }
    waveform_ = uint8_t(wave);
    sync_ = keySync != 0;
  }
  // 0..1 in Q24
  int32_t getsample() {
    phase_ += delta_;
    int32_t x;
    switch (waveform_) {
      case 0:  // triangle
        x = int32_t(phase_ >> 7);
        x ^= -int32_t(phase_ >> 31);
        x &= (1 << 24) - 1;
        return x;
      case 1:  // sawtooth down
        return int32_t((~phase_ ^ (1U << 31)) >> 8);
      case 2:  // sawtooth up
        return int32_t((phase_ ^ (1U << 31)) >> 8);
      case 3:  // square
        return int32_t(((~phase_) >> 7) & (1 << 24));
      case 4:  // sine
        return (1 << 23) + (Sin::lookup(int32_t(phase_ >> 8)) >> 1);
      case 5:  // s&h
        if (phase_ < delta_) randstate_ = uint8_t((randstate_ * 179 + 17) & 0xff);
        x = randstate_ ^ 0x80;
        return (x + 1) << 16;
      default:
        return 1 << 23;
    }
  }
  // 0..1 in Q24
  int32_t getdelay() {
    const uint32_t delta = delaystate_ < (1U << 31) ? delayinc_ : delayinc2_;
    const uint64_t d = uint64_t(delaystate_) + delta;
    if (d > ~0u) return 1 << 24;
    delaystate_ = uint32_t(d);
    if (d < (1U << 31)) return 0;
    return int32_t((d >> 7) & ((1 << 24) - 1));
  }
  void keydown() {
    if (sync_) phase_ = (1U << 31) - 1;
    delaystate_ = 0;
  }
  uint32_t phase() const { return phase_; }
  uint32_t delta() const { return delta_; }
  uint8_t randstate() const { return randstate_; }

 private:
  uint32_t unit_ = 0;
  double ratio_ = 0.0;
  uint32_t phase_ = 0;
  uint32_t delta_ = 0;
  uint8_t waveform_ = 0;
  uint8_t randstate_ = 0;
  bool sync_ = false;
  uint32_t delaystate_ = 0;
  uint32_t delayinc_ = ~0u;
  uint32_t delayinc2_ = ~0u;
};

class PitchEnv {
 public:
  static int unitFor(double sampleRate) { return int(kN * (1 << 24) / (21.3 * sampleRate) + 0.5); }
  void setUnit(int u) { unit_ = u; }
  void set(const int r[4], const int l[4]) {
    for (int i = 0; i < 4; ++i) {
      rates_[i] = r[i];
      levels_[i] = l[i];
    }
    level_ = tab::pitchenv_tab[l[3]] * (1 << 19);
    down_ = true;
    advance(0);
  }
  void update(const int r[4], const int l[4]) {
    for (int i = 0; i < 4; ++i) {
      rates_[i] = r[i];
      levels_[i] = l[i];
    }
    if (ix_ < 4) advance(ix_);
  }
  int32_t getsample() {
    if (ix_ < 3 || ((ix_ < 4) && !down_)) {
      if (rising_) {
        level_ += inc_;
        if (level_ >= targetlevel_) {
          level_ = targetlevel_;
          advance(ix_ + 1);
        }
      } else {
        level_ -= inc_;
        if (level_ <= targetlevel_) {
          level_ = targetlevel_;
          advance(ix_ + 1);
        }
      }
    }
    return level_;
  }
  void keydown(bool d) {
    if (down_ != d) {
      down_ = d;
      advance(d ? 0 : 3);
    }
  }
  int32_t level() const { return level_; }

 private:
  void advance(int newix) {
    ix_ = newix;
    if (ix_ < 4) {
      const int newlevel = levels_[ix_];
      targetlevel_ = tab::pitchenv_tab[newlevel] * (1 << 19);
      rising_ = targetlevel_ > level_;
      inc_ = tab::pitchenv_rate[rates_[ix_]] * unit_;
    }
  }
  int rates_[4] = {99, 99, 99, 99};
  int levels_[4] = {50, 50, 50, 50};
  int32_t level_ = 0;
  int32_t targetlevel_ = 0;
  int ix_ = 4;
  bool rising_ = false;
  bool down_ = false;
  int32_t inc_ = 0;
  int unit_ = 0;
};

}  // namespace vdx::fm
