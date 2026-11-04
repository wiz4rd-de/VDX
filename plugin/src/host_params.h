// Host parameters (SPEC §4.9). JUCE-free description of the 174 parameters in
// host order:
//   OP1 … OP6 blocks: op{n}_on, then the 21 operator parameters in offset
//   order 0–20 (op{n}_{id});
//   voice globals 126–144 (g_{name});
//   function 64–70, 71 as three bools, 72, 73 as bools, 74, 75 as bools, 76,
//   77 as bools;
//   master_tune.
// Value strings come from the core's formatting module (core/panel/format.h),
// so they equal the LCD and programmer strings (R-P8).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace vdx::plugin {

enum class HostKind : uint8_t {
  Voice,       // voice parameter `index` 0..144
  OpSwitch,    // operator `index` 1..6 on/off (operator mask bit 6 - op)
  Function,    // function parameter `index` 64..70, 72, 74, 76
  AssignBit,   // bit `bit` (0 pitch, 1 amp, 2 EG bias) of assign byte `index` 71/73/75/77
  MasterTune,  // panel global master tune 0..127
};

struct HostParamInfo {
  std::string id;
  std::string name;
  HostKind kind;
  int index;
  int bit;
  int max;  // minimum is always 0
  int def;  // first-run value
  bool isBool() const { return kind == HostKind::OpSwitch || kind == HostKind::AssignBit; }
};

constexpr int kHostParamCount = 174;

// The table, in host order.
const std::vector<HostParamInfo>& hostParams();

// Host index of a voice parameter 0..144 (-1 for names / the mask), of an
// operator switch, of a range/mode function parameter, of an assign bit and
// of master tune.
int hostIndexOfVoice(int voiceIndex);
int hostIndexOfOpSwitch(int op);
int hostIndexOfFunction(int fn);
int hostIndexOfAssignBit(int assignFn, int bit);
int hostIndexOfMasterTune();

// Value string of host parameter `hostIndex` at `value`. `hostValues` holds
// the current value of every host parameter (context for strings that depend
// on other parameters: frequency, bend range, portamento mode).
std::string hostValueString(int hostIndex, int value, const int* hostValues);
// Inverse: the value whose string equals `text` (case-insensitive, spaces
// trimmed), else the number in `text`, clamped to the range.
int hostValueFromString(int hostIndex, const std::string& text, const int* hostValues);

}  // namespace vdx::plugin
