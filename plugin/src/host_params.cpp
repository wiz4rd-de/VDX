#include "host_params.h"

#include <cctype>
#include <cstdlib>
#include <cstring>

#include "panel/format.h"
#include "sysex/default_bank.h"
#include "sysex/voice.h"
#include "vdx/params.h"

namespace vdx::plugin {

namespace {

using namespace vdx::params;

struct FnDesc {
  int number;
  const char* id;
  const char* name;
};

// Function parameters 64..69 (modes) and the four ranges (§4.2).
constexpr FnDesc kModes[6] = {
    {kFnMono, "fn_mono", "Mono/Poly"},
    {kFnBendRange, "fn_bend_range", "Pitch Bend Range"},
    {kFnBendStep, "fn_bend_step", "Pitch Bend Step"},
    {kFnPortaMode, "fn_porta_mode", "Portamento Mode"},
    {kFnGlissando, "fn_glissando", "Glissando"},
    {kFnPortaTime, "fn_porta_time", "Portamento Time"},
};
constexpr const char* kBitId[3] = {"pitch", "amp", "egbias"};
constexpr const char* kBitName[3] = {"Pitch", "Amp", "EG Bias"};

struct Tables {
  std::vector<HostParamInfo> params;
  int voice[156];
  int opSwitch[7];
  int fn[78];
  int assign[4][3];
  int masterTune = -1;

  Tables() {
    for (int& v : voice) v = -1;
    for (int& v : opSwitch) v = -1;
    for (int& v : fn) v = -1;
    for (auto& a : assign)
      for (int& v : a) v = -1;

    // First-run values: internal slot 1 of the default bank, all operators on,
    // §4.2 function defaults, master tune 64.
    sysex::Voice first;
    sysex::unpackVoice(sysex::defaultBankPacked(), first);
    uint8_t eff[sysex::kVoiceBytes];
    sysex::effectiveVoice(first.raw, eff);

    auto add = [this](HostParamInfo p) {
      params.push_back(std::move(p));
      return int(params.size()) - 1;
    };
    for (int op = 1; op <= 6; ++op) {
      const std::string n = std::to_string(op);
      opSwitch[op] = add({"op" + n + "_on", "OP" + n + " On", HostKind::OpSwitch, op, 6 - op, 1, 1});
      for (int k = 0; k < kOpBytes; ++k) {
        const int index = opParam(op, k);
        voice[index] = add({"op" + n + "_" + kOpParamId[k], "OP" + n + " " + kOpParamName[k], HostKind::Voice, index,
                            0, voiceMax(index), eff[index]});
      }
    }
    for (int i = 0; i < 19; ++i) {
      const int index = kPegR1 + i;
      voice[index] = add({kGlobalParamId[i], kGlobalParamName[i], HostKind::Voice, index, 0, voiceMax(index), eff[index]});
    }
    for (const FnDesc& d : kModes)
      fn[d.number] = add({d.id, d.name, HostKind::Function, d.number, 0, functionMax(d.number), functionDefault(d.number)});
    for (int s = 0; s < 4; ++s) {
      const int range = kFnWheelRange + 2 * s;
      const int assignFn = range + 1;
      const std::string sid = kCtlSourceId[s], sname = kCtlSourceName[s];
      fn[range] = add({sid + "_range", sname + " Range", HostKind::Function, range, 0, functionMax(range),
                       functionDefault(range)});
      for (int b = 0; b < 3; ++b)
        assign[s][b] = add({sid + "_" + kBitId[b], sname + " " + kBitName[b], HostKind::AssignBit, assignFn, b, 1,
                            (functionDefault(assignFn) >> b) & 1});
    }
    masterTune = add({"master_tune", "Master Tune", HostKind::MasterTune, 0, 0, 127, kMasterTuneDefault});
  }
};

const Tables& tables() {
  static const Tables t;
  return t;
}

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

std::string normalise(const std::string& s) {
  size_t b = 0, e = s.size();
  while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
  std::string o;
  for (size_t i = b; i < e; ++i) o += char(std::toupper(static_cast<unsigned char>(s[i])));
  return o;
}

}  // namespace

const std::vector<HostParamInfo>& hostParams() { return tables().params; }

int hostIndexOfVoice(int voiceIndex) {
  return voiceIndex >= 0 && voiceIndex < 156 ? tables().voice[voiceIndex] : -1;
}
int hostIndexOfOpSwitch(int op) { return op >= 1 && op <= 6 ? tables().opSwitch[op] : -1; }
int hostIndexOfFunction(int fn) { return fn >= 0 && fn < 78 ? tables().fn[fn] : -1; }
int hostIndexOfAssignBit(int assignFn, int bit) {
  if (assignFn < kFnWheelAssign || assignFn > kFnAtAssign || ((assignFn - kFnWheelAssign) & 1) || bit < 0 || bit > 2)
    return -1;
  return tables().assign[(assignFn - kFnWheelAssign) / 2][bit];
}
int hostIndexOfMasterTune() { return tables().masterTune; }

std::string hostValueString(int hostIndex, int value, const int* hv) {
  const auto& ps = hostParams();
  if (hostIndex < 0 || hostIndex >= int(ps.size())) return {};
  const HostParamInfo& p = ps[size_t(hostIndex)];
  value = clampi(value, 0, p.max);
  char out[fmt::kMaxText] = {};
  switch (p.kind) {
    case HostKind::Voice: {
      uint8_t eff[156] = {};
      for (int i = 0; i < 145; ++i) {
        const int h = tables().voice[i];
        if (h >= 0 && hv) eff[i] = uint8_t(clampi(hv[h], 0, 127));
      }
      eff[kOpMask] = 0x3F;
      fmt::voiceString(eff, p.index, value, out);
      break;
    }
    case HostKind::OpSwitch: return fmt::opOnString(value != 0);
    case HostKind::Function: {
      uint8_t fn14[14] = {};
      for (int n = kFnFirst; n <= kFnLast; ++n) {
        int v = 0;
        if (hv) {
          if (tables().fn[n] >= 0) v = hv[tables().fn[n]];
          else
            for (int b = 0; b < 3; ++b) v |= (hv[hostIndexOfAssignBit(n, b)] ? 1 : 0) << b;
        }
        fn14[n - kFnFirst] = uint8_t(clampi(v, 0, functionMax(n)));
      }
      fmt::functionString(fn14, p.index, value, out);
      break;
    }
    case HostKind::AssignBit: fmt::assignBitString(value << p.bit, p.bit, out); break;
    case HostKind::MasterTune: fmt::globalString(0, value, out); break;
  }
  return out;
}

int hostValueFromString(int hostIndex, const std::string& text, const int* hv) {
  const auto& ps = hostParams();
  if (hostIndex < 0 || hostIndex >= int(ps.size())) return 0;
  const HostParamInfo& p = ps[size_t(hostIndex)];
  const std::string want = normalise(text);
  for (int v = 0; v <= p.max; ++v)
    if (normalise(hostValueString(hostIndex, v, hv)) == want) return v;
  char* end = nullptr;
  const long n = std::strtol(want.c_str(), &end, 10);
  if (end != want.c_str()) return clampi(int(n), 0, p.max);
  return p.def;
}

}  // namespace vdx::plugin
