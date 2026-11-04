// The single value-formatting module (SPEC §5.3, R-P8). Every display string
// — LCD fields, programmer/event text, plugin host value strings — comes from
// here, so the three are identical by construction.
//
// Conventions:
//  * "field" functions return the LCD value field exactly as on the LCD
//    (untrimmed, e.g. " 7", " 1.00", "A -1").
//  * "string" functions return the canonical value string: the field with
//    leading/trailing spaces trimmed (e.g. "7", "1.00", "A -1"). Operator
//    on/off is "ON"/"OFF".
// All functions write a NUL-terminated string into `out` (capacity >= 24) and
// return its length. No allocation; usable from any thread.
#pragma once

#include <cstdint>

namespace vdx::fmt {

constexpr int kMaxText = 24;

// ---- primitives -----------------------------------------------------------
int nn(int v, char* out);                         // 2-char right-aligned number
int number(int v, char* out);                     // plain integer
int noteName(int midi, char* out);                // "C 3", "A -1", "C#4"
int ratio(int coarse, int fine, char* out);       // "dd.dd" (5 chars)
int fixedFreq(int coarse, int fine, char* out);   // "1.000Hz" .. "1000 Hz" (7 chars)
int opFrequency(int mode, int coarse, int fine, char* out);  // ratio or fixed field
int detune(int v0to14, char* out);                // "-7".."-1", " 0", "+1".."+7"
const char* wave(int w);                          // TRIANGL SAW DWN SAW UP SQUARE SINE S/HOLD
const char* curve(int c);                         // -LIN -EXP +EXP +LIN
const char* onOff(int v);                         // ON / OFF
const char* oscMode(int mode);                    // FREQUENCY(RATIO) / FIXED FREQ.(Hz)
int masterTune(int v0to127, char* out);           // "+73.8 cents", "0.0 cents", "-75.0 cents"
int lfoHz(int speed, char* out);                  // "5.62 Hz" (MSFA lfoSource[], 2 decimals)
int trim(const char* in, char* out);              // strip leading/trailing spaces

// ---- voice parameters (index 0..155) --------------------------------------
// eff156: effective voice values, [155] = operator mask. The value at
// `index` is replaced by `value` (so host value strings can be computed for
// hypothetical values; frequency fields read coarse/fine/mode of the same op).
int voiceField(const uint8_t* eff156, int index, int value, char* out);
int voiceString(const uint8_t* eff156, int index, int value, char* out);
// Operator mask as the six LCD digits, OP1..OP6 ("111111").
int maskDigits(int mask, char* out);
// Host operator switch value string.
inline const char* opOnString(bool on) { return on ? "ON" : "OFF"; }

// ---- function parameters (64..77) -----------------------------------------
// fn14: function parameters 64..77; the value of `n` is replaced by `value`.
// Assign bytes (71/73/75/77) give the three bits PITCH AMP EG-BIAS as
// "ON OFF OFF"; assignBitString gives one bit as on the LCD.
int functionField(const uint8_t* fn14, int n, int value, char* out);
int functionString(const uint8_t* fn14, int n, int value, char* out);
int assignBitString(int assignValue, int bit, char* out);

// ---- panel globals (vdx::Global) ------------------------------------------
int globalString(int global, int value, char* out);

}  // namespace vdx::fmt
