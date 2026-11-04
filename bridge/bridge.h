// vdx_bridge: the UI protocol (docs/PROTOCOL.md) on top of vdx::Instrument.
// Used by the desktop editor (message thread) and the WebAssembly module (main
// thread). Not real-time code: it may allocate.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "vdx/vdx.h"

namespace vdx::bridge {

// Minimal JSON value for flat UI messages.
struct Json {
  enum class Type { Null, Bool, Number, String, Array, Object } type = Type::Null;
  bool b = false;
  double n = 0;
  std::string s;
  std::vector<Json> a;
  std::map<std::string, Json> o;

  const Json* get(const std::string& k) const {
    auto it = o.find(k);
    return it == o.end() ? nullptr : &it->second;
  }
  double num(const std::string& k, double def = 0) const {
    const Json* v = get(k);
    if (!v) return def;
    if (v->type == Type::Number) return v->n;
    if (v->type == Type::Bool) return v->b ? 1 : 0;
    return def;
  }
  int i(const std::string& k, int def = 0) const { return int(num(k, def)); }
  bool flag(const std::string& k, bool def = false) const {
    const Json* v = get(k);
    if (!v) return def;
    if (v->type == Type::Bool) return v->b;
    if (v->type == Type::Number) return v->n != 0;
    return def;
  }
  std::string str(const std::string& k) const {
    const Json* v = get(k);
    return v && v->type == Type::String ? v->s : std::string();
  }
};

bool parseJson(const std::string& text, Json& out);
std::string quote(const std::string& s);  // JSON string literal

std::string base64Encode(const uint8_t* data, size_t len);
bool base64Decode(const std::string& in, std::vector<uint8_t>& out);

// Ids used in the protocol.
const char* globalId(Global g);
bool globalFromId(const std::string& id, Global& g);
const char* settingId(SettingId s);
bool settingFromId(const std::string& id, SettingId& s);

// Encodes one core event as a JSON object (no payload events).
std::string encodeEvent(const Event& e);

struct ExportFile {
  ExportKind kind;
  std::string name;  // sanitised file name incl. ".syx"
  std::vector<uint8_t> bytes;
};

class Bridge {
 public:
  explicit Bridge(Instrument& inst);

  // UI -> core. Returns a JSON array of immediate replies for the UI ("[]" if
  // none). Shell-only commands (docs/PROTOCOL.md) are not handled here: the
  // return value is then empty and isShellCommand() is true.
  std::string handle(const std::string& json);
  static bool isShellCommand(const std::string& t);

  // File import (R-D4, F-12.1): scan bytes, apply a voice, hold a bank for
  // the import choice, or reject. Returns the JSON reply array.
  std::string importFile(const std::string& name, const std::vector<uint8_t>& bytes);

  // core -> UI: drain pending events as one JSON array ("" if none).
  // ExportData payloads go to onExport; StateDirty sets stateDirty().
  std::string drain(const std::function<void(ExportFile&&)>& onExport);
  bool takeStateDirty() {
    const bool d = stateDirty_;
    stateDirty_ = false;
    return d;
  }

  // Latest values seen in events (for the plugin: program names, current program).
  std::string voiceName(int bank, int slot) const;
  int voiceSourceBank() const { return srcBank_; }
  int voiceSourceSlot() const { return srcSlot_; }

 private:
  bool post(const Command& c);
  Instrument& inst_;
  std::vector<uint8_t> pendingBank_;  // 4096 packed bytes
  std::string pendingName_;
  bool pendingChecksumError_ = false;
  bool stateDirty_ = false;
  std::string names_[2][32];
  int srcBank_ = 0, srcSlot_ = 1;
};

}  // namespace vdx::bridge
