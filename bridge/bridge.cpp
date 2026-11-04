// UI protocol translation (docs/PROTOCOL.md).
#include "bridge.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "sysex/sysex.h"

namespace vdx::bridge {

// ---- JSON -------------------------------------------------------------

namespace {

struct Parser {
  const std::string& t;
  size_t p = 0;
  explicit Parser(const std::string& s) : t(s) {}
  void ws() {
    while (p < t.size() && (t[p] == ' ' || t[p] == '\t' || t[p] == '\n' || t[p] == '\r')) ++p;
  }
  bool lit(const char* w) {
    const size_t n = std::strlen(w);
    if (t.compare(p, n, w) == 0) {
      p += n;
      return true;
    }
    return false;
  }
  static void utf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) out += char(cp);
    else if (cp < 0x800) {
      out += char(0xC0 | (cp >> 6));
      out += char(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
      out += char(0xE0 | (cp >> 12));
      out += char(0x80 | ((cp >> 6) & 0x3F));
      out += char(0x80 | (cp & 0x3F));
    } else {
      out += char(0xF0 | (cp >> 18));
      out += char(0x80 | ((cp >> 12) & 0x3F));
      out += char(0x80 | ((cp >> 6) & 0x3F));
      out += char(0x80 | (cp & 0x3F));
    }
  }
  bool hex4(uint32_t& v) {
    if (p + 4 > t.size()) return false;
    v = 0;
    for (int i = 0; i < 4; ++i) {
      const char c = t[p++];
      v <<= 4;
      if (c >= '0' && c <= '9') v |= uint32_t(c - '0');
      else if (c >= 'a' && c <= 'f') v |= uint32_t(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') v |= uint32_t(c - 'A' + 10);
      else return false;
    }
    return true;
  }
  bool str(std::string& out) {
    if (p >= t.size() || t[p] != '"') return false;
    ++p;
    while (p < t.size()) {
      const char c = t[p++];
      if (c == '"') return true;
      if (c != '\\') {
        out += c;
        continue;
      }
      if (p >= t.size()) return false;
      const char e = t[p++];
      switch (e) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'u': {
          uint32_t cp;
          if (!hex4(cp)) return false;
          if (cp >= 0xD800 && cp < 0xDC00 && p + 6 <= t.size() && t[p] == '\\' && t[p + 1] == 'u') {
            p += 2;
            uint32_t lo;
            if (!hex4(lo)) return false;
            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
          }
          utf8(out, cp);
          break;
        }
        default: return false;
      }
    }
    return false;
  }
  bool value(Json& v, int depth) {
    if (depth > 16) return false;
    ws();
    if (p >= t.size()) return false;
    const char c = t[p];
    if (c == '{') {
      ++p;
      v.type = Json::Type::Object;
      ws();
      if (p < t.size() && t[p] == '}') {
        ++p;
        return true;
      }
      for (;;) {
        ws();
        std::string k;
        if (!str(k)) return false;
        ws();
        if (p >= t.size() || t[p] != ':') return false;
        ++p;
        Json child;
        if (!value(child, depth + 1)) return false;
        v.o[k] = std::move(child);
        ws();
        if (p < t.size() && t[p] == ',') {
          ++p;
          continue;
        }
        if (p < t.size() && t[p] == '}') {
          ++p;
          return true;
        }
        return false;
      }
    }
    if (c == '[') {
      ++p;
      v.type = Json::Type::Array;
      ws();
      if (p < t.size() && t[p] == ']') {
        ++p;
        return true;
      }
      for (;;) {
        Json child;
        if (!value(child, depth + 1)) return false;
        v.a.push_back(std::move(child));
        ws();
        if (p < t.size() && t[p] == ',') {
          ++p;
          continue;
        }
        if (p < t.size() && t[p] == ']') {
          ++p;
          return true;
        }
        return false;
      }
    }
    if (c == '"') {
      v.type = Json::Type::String;
      return str(v.s);
    }
    if (lit("true")) {
      v.type = Json::Type::Bool;
      v.b = true;
      return true;
    }
    if (lit("false")) {
      v.type = Json::Type::Bool;
      v.b = false;
      return true;
    }
    if (lit("null")) {
      v.type = Json::Type::Null;
      return true;
    }
    // number
    const size_t start = p;
    if (t[p] == '-') ++p;
    while (p < t.size() && ((t[p] >= '0' && t[p] <= '9') || t[p] == '.' || t[p] == 'e' || t[p] == 'E' || t[p] == '+' ||
                            t[p] == '-'))
      ++p;
    if (p == start) return false;
    v.type = Json::Type::Number;
    v.n = std::strtod(t.substr(start, p - start).c_str(), nullptr);
    return true;
  }
};

void appendNum(std::string& s, long long v) { s += std::to_string(v); }

}  // namespace

bool parseJson(const std::string& text, Json& out) {
  Parser ps(text);
  if (!ps.value(out, 0)) return false;
  ps.ws();
  return ps.p == text.size();
}

std::string quote(const std::string& s) {
  std::string o = "\"";
  for (unsigned char c : s) {
    switch (c) {
      case '"': o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\n': o += "\\n"; break;
      case '\r': o += "\\r"; break;
      case '\t': o += "\\t"; break;
      default:
        if (c < 0x20 || c == 0x7F) {
          char buf[8];
          std::snprintf(buf, sizeof buf, "\\u%04x", c);
          o += buf;
        } else {
          o += char(c);
        }
    }
  }
  return o + "\"";
}

// ---- base64 -----------------------------------------------------------

static const char* kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64Encode(const uint8_t* d, size_t n) {
  std::string o;
  o.reserve((n + 2) / 3 * 4);
  for (size_t i = 0; i < n; i += 3) {
    const uint32_t v = uint32_t(d[i]) << 16 | (i + 1 < n ? uint32_t(d[i + 1]) << 8 : 0u) | (i + 2 < n ? d[i + 2] : 0u);
    o += kB64[(v >> 18) & 63];
    o += kB64[(v >> 12) & 63];
    o += i + 1 < n ? kB64[(v >> 6) & 63] : '=';
    o += i + 2 < n ? kB64[v & 63] : '=';
  }
  return o;
}

bool base64Decode(const std::string& in, std::vector<uint8_t>& out) {
  out.clear();
  uint32_t buf = 0;
  int bits = 0;
  for (char c : in) {
    int v;
    if (c >= 'A' && c <= 'Z') v = c - 'A';
    else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
    else if (c >= '0' && c <= '9') v = c - '0' + 52;
    else if (c == '+' || c == '-') v = 62;
    else if (c == '/' || c == '_') v = 63;
    else if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
    else return false;
    buf = (buf << 6) | uint32_t(v);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(uint8_t((buf >> bits) & 0xFF));
    }
  }
  return true;
}

// ---- ids --------------------------------------------------------------

static const char* kGlobalIds[] = {"masterTune", "midiChannel", "omni", "sysInfo", "protectInt", "protectCrt", "volume"};
static const char* kSettingIds[] = {"engine", "dac", "hostRate", "classicVelocity", "keyboardPlay", "zoom", "opOrder"};

const char* globalId(Global g) {
  const int i = int(g);
  return i >= 0 && i < int(Global::Count) ? kGlobalIds[i] : "";
}
bool globalFromId(const std::string& id, Global& g) {
  for (int i = 0; i < int(Global::Count); ++i)
    if (id == kGlobalIds[i]) {
      g = Global(i);
      return true;
    }
  return false;
}
const char* settingId(SettingId s) {
  const int i = int(s);
  return i >= 0 && i < int(SettingId::Count) ? kSettingIds[i] : "";
}
bool settingFromId(const std::string& id, SettingId& s) {
  for (int i = 0; i < int(SettingId::Count); ++i)
    if (id == kSettingIds[i]) {
      s = SettingId(i);
      return true;
    }
  return false;
}

static const char* sourceName(Source s) {
  switch (s) {
    case Source::Panel: return "panel";
    case Source::Programmer: return "programmer";
    case Source::Host: return "host";
    case Source::SysEx: return "sysex";
    case Source::Midi: return "midi";
    case Source::Keyboard: return "keyboard";
    case Source::BankManager: return "bank";
    case Source::State: return "state";
    case Source::Internal: return "internal";
    default: return "none";
  }
}

static std::string textOf(const Event& e, size_t maxLen) {
  size_t n = 0;
  while (n < maxLen && n < sizeof e.text && e.text[n] != 0) ++n;
  return std::string(e.text, n);
}

static std::string fixed(const Event& e, size_t off, size_t len) {
  std::string s(e.text + off, len);
  for (auto& c : s)
    if (c == 0) c = ' ';
  return s;
}

std::string encodeEvent(const Event& e) {
  std::string s;
  auto kv = [&s](const char* k, long long v) {
    s += ",\"";
    s += k;
    s += "\":";
    appendNum(s, v);
  };
  auto kb = [&s](const char* k, bool v) {
    s += ",\"";
    s += k;
    s += v ? "\":true" : "\":false";
  };
  auto ks = [&s](const char* k, const std::string& v) {
    s += ",\"";
    s += k;
    s += "\":";
    s += quote(v);
  };
  switch (e.type) {
    case EvType::VoiceParam:
      s = "{\"e\":\"param\"";
      kv("i", e.a);
      kv("v", e.b);
      ks("s", textOf(e, 64));
      ks("src", sourceName(e.src));
      break;
    case EvType::FunctionParam:
      s = "{\"e\":\"func\"";
      kv("i", e.a);
      kv("v", e.b);
      ks("s", textOf(e, 64));
      ks("src", sourceName(e.src));
      break;
    case EvType::GlobalValue:
      s = "{\"e\":\"global\"";
      ks("id", globalId(Global(e.a)));
      kv("v", e.b);
      ks("s", textOf(e, 64));
      break;
    case EvType::SettingValue:
      s = "{\"e\":\"setting\"";
      ks("id", settingId(SettingId(e.a)));
      kv("v", e.b);
      break;
    case EvType::Lcd:
      s = "{\"e\":\"lcd\"";
      ks("l1", fixed(e, 0, 16));
      ks("l2", fixed(e, 16, 16));
      kv("row", e.a);
      kv("col", e.b);
      kb("cursor", e.c != 0);
      kb("lit", e.d != 0);
      break;
    case EvType::Led:
      s = "{\"e\":\"led\"";
      kv("v", e.a);
      kb("dot", e.b != 0);
      break;
    case EvType::Lamps:
      s = "{\"e\":\"lamps\"";
      kv("num", uint32_t(e.a));
      kv("mode", uint32_t(e.b));
      kv("blink", uint32_t(e.c));
      break;
    case EvType::PanelState: {
      static const char* modes[] = {"PLAY", "EDIT", "EDIT_NAME", "FUNCTION", "SPLASH"};
      s = "{\"e\":\"panel\"";
      ks("mode", e.a >= 0 && e.a <= 4 ? modes[e.a] : "PLAY");
      kv("page", e.b);
      kv("op", e.c);
      kb("comparing", (e.d & kFlagComparing) != 0);
      kb("edited", (e.d & kFlagEdited) != 0);
      kb("dialog", (e.d & kFlagDialog) != 0);
      kb("message", (e.d & kFlagMessage) != 0);
      kb("armed", (e.d & kFlagTransposeArmed) != 0);
      break;
    }
    case EvType::DataEntryPos: {
      s = "{\"e\":\"dataEntry\",\"pos\":";
      char buf[32];
      std::snprintf(buf, sizeof buf, "%.5f", double(e.f));
      s += buf;
      break;
    }
    case EvType::Wheels:
      s = "{\"e\":\"wheels\"";
      kv("bend", e.a);
      kv("mod", e.b);
      break;
    case EvType::VoiceName:
      s = "{\"e\":\"voiceName\"";
      kv("bank", e.a);
      kv("slot", e.b);
      ks("name", fixed(e, 0, 10));
      break;
    case EvType::Cartridge:
      s = "{\"e\":\"cartridge\"";
      kb("present", e.a != 0);
      kb("changed", e.b != 0);
      ks("name", textOf(e, 64));
      break;
    case EvType::Memory:
      s = "{\"e\":\"memory\"";
      kv("selected", e.a);
      kv("srcBank", e.b);
      kv("srcSlot", e.c);
      kb("edited", e.d != 0);
      break;
    case EvType::OpFrequency:
      s = "{\"e\":\"opFreq\"";
      kv("op", e.a);
      ks("s", textOf(e, 64));
      break;
    case EvType::LfoHz:
      s = "{\"e\":\"lfoHz\"";
      ks("s", textOf(e, 64));
      break;
    case EvType::Meter: {
      s = "{\"e\":\"meter\",\"db\":[";
      for (int k = 0; k < 6; ++k) {
        const int16_t v = int16_t(uint16_t(uint8_t(e.text[2 * k])) | uint16_t(uint8_t(e.text[2 * k + 1])) << 8);
        char buf[24];
        std::snprintf(buf, sizeof buf, "%s%.1f", k ? "," : "", double(v) / 100.0);
        s += buf;
      }
      s += "]";
      break;
    }
    case EvType::Notice: {
      const char* code = e.a == kNoticeProtected ? "protected" : e.a == kNoticeDumpReceived ? "dumpReceived" : "completed";
      s = "{\"e\":\"notice\"";
      ks("code", code);
      kv("bank", e.b);
      ks("text", textOf(e, 64));
      break;
    }
    case EvType::SnapshotEnd:
      s = "{\"e\":\"snapshotEnd\"";
      break;
    default:
      return std::string();
  }
  s += "}";
  return s;
}

// ---- Bridge -----------------------------------------------------------

Bridge::Bridge(Instrument& inst) : inst_(inst) {}

bool Bridge::post(const Command& c) { return inst_.post(c); }

bool Bridge::isShellCommand(const std::string& t) {
  return t == "pickFile" || t == "openDevices" || t == "midiSelect" || t == "uiReady" || t == "editorClosing";
}

std::string Bridge::handle(const std::string& json) {
  Json j;
  if (!parseJson(json, j) || j.type != Json::Type::Object) return "[]";
  const std::string t = j.str("t");
  if (isShellCommand(t)) return std::string();
  Command c;
  auto send = [&](CmdType type, Source src, int a = 0, int b = 0, int cc = 0) {
    c = Command();
    c.type = type;
    c.src = src;
    c.a = a;
    c.b = b;
    c.c = cc;
    post(c);
  };
  if (t == "noteOn") send(CmdType::NoteOn, Source::Keyboard, j.i("note"), j.i("vel", 100));
  else if (t == "noteOff") send(CmdType::NoteOff, Source::Keyboard, j.i("note"));
  else if (t == "cc") send(CmdType::Controller, Source::Keyboard, j.i("cc"), j.i("v"));
  else if (t == "bend") send(CmdType::PitchBend, Source::Keyboard, j.i("v", 8192));
  else if (t == "releaseAll") send(CmdType::ReleaseAll, Source::Keyboard);
  else if (t == "button") send(CmdType::Button, Source::Panel, j.i("id"), j.flag("down") ? 1 : 0);
  else if (t == "dataEntry") {
    c = Command();
    c.type = CmdType::DataEntry;
    c.src = Source::Panel;
    c.f = float(j.num("pos"));
    post(c);
  } else if (t == "setParam") send(CmdType::SetVoiceParam, Source::Programmer, j.i("i"), j.i("v"));
  else if (t == "setFunc") send(CmdType::SetFunctionParam, Source::Programmer, j.i("i"), j.i("v"));
  else if (t == "setGlobal") {
    Global g;
    if (globalFromId(j.str("id"), g)) send(CmdType::SetGlobal, Source::Programmer, int(g), j.i("v"));
  } else if (t == "setSetting") {
    SettingId s;
    if (settingFromId(j.str("id"), s)) send(CmdType::SetSetting, Source::Programmer, int(s), j.i("v"));
  } else if (t == "writeOp") {
    c = Command();
    c.type = CmdType::WriteOperator;
    c.src = Source::Programmer;
    c.a = j.i("op");
    const Json* bytes = j.get("bytes");
    if (!bytes || bytes->type != Json::Type::Array || bytes->a.size() != 21) return "[]";
    for (size_t k = 0; k < 21; ++k) c.data[k] = uint8_t(int(bytes->a[k].n) & 0x7F);
    post(c);
  } else if (t == "swapOps") send(CmdType::SwapOperators, Source::Programmer, j.i("a"), j.i("b"));
  else if (t == "egCopy") send(CmdType::EgCopy, Source::Programmer, j.i("from"), j.i("to"));
  else if (t == "initVoice") send(CmdType::InitVoice, Source::Programmer);
  else if (t == "randomise") send(CmdType::Randomise, Source::Programmer, j.i("seed", 1));
  else if (t == "compare") send(CmdType::ToggleCompare, Source::Programmer);
  else if (t == "selectVoice") send(CmdType::SelectVoice, Source::BankManager, j.i("bank"), j.i("slot", 1));
  else if (t == "copyVoice") {
    c = Command();
    c.type = CmdType::CopyVoice;
    c.src = Source::BankManager;
    c.a = j.i("fromBank");
    c.b = j.i("fromSlot", 1);
    c.c = j.i("toBank");
    c.data[0] = uint8_t(j.i("toSlot", 1));
    post(c);
  } else if (t == "newCartridge") send(CmdType::NewCartridge, Source::BankManager);
  else if (t == "ejectCartridge") send(CmdType::EjectCartridge, Source::BankManager);
  else if (t == "export") {
    const std::string w = j.str("what");
    const ExportKind k = w == "crt" ? ExportKind::Cartridge : w == "voice" ? ExportKind::Voice : ExportKind::Internal;
    send(CmdType::Export, Source::BankManager, int(k));
  } else if (t == "sendBank") send(CmdType::SendBank, Source::BankManager);
  else if (t == "requestDump") send(CmdType::RequestDump, Source::BankManager);
  else if (t == "snapshot") send(CmdType::RequestSnapshot, Source::Internal);
  else if (t == "startSplash") send(CmdType::StartSplash, Source::Internal);
  else if (t == "selectOp") send(CmdType::SelectOperator, Source::Programmer, j.i("op", 1));
  else if (t == "exportDone") {
    if (j.str("what") == "crt") send(CmdType::ClearCartridgeChanged, Source::BankManager);
  }
  else if (t == "importFile") {
    std::vector<uint8_t> bytes;
    if (!base64Decode(j.str("b64"), bytes)) bytes.clear();
    return importFile(j.str("name"), bytes);
  } else if (t == "importChoice") {
    if (pendingBank_.size() != 4096) return "[]";
    Command lb;
    lb.type = CmdType::LoadBank;
    lb.src = Source::BankManager;
    lb.a = j.str("target") == "int" ? kBankInt : kBankCrt;
    lb.b = pendingChecksumError_ ? 1 : 0;
    std::vector<uint8_t> payload(pendingBank_);
    std::string nm = pendingName_.substr(0, 64);
    payload.insert(payload.end(), nm.begin(), nm.end());
    payload.push_back(0);
    inst_.postPayload(lb, payload.data(), int(payload.size()));
    pendingBank_.clear();
  } else if (t == "importCancel") {
    pendingBank_.clear();
  }
  return "[]";
}

std::string Bridge::importFile(const std::string& name, const std::vector<uint8_t>& bytes) {
  // Cartridge name = file name without directories and extension, <= 64 chars (§4.5, §8).
  std::string stem = name;
  const size_t slash = stem.find_last_of("/\\");
  if (slash != std::string::npos) stem = stem.substr(slash + 1);
  const size_t dot = stem.find_last_of('.');
  if (dot != std::string::npos && dot > 0) stem = stem.substr(0, dot);
  if (stem.size() > 64) {
    size_t cut = 64;  // keep whole UTF-8 sequences
    while (cut > 0 && (uint8_t(stem[cut]) & 0xC0) == 0x80) --cut;
    stem = stem.substr(0, cut);
  }
  sysex::ScanResult r;
  const sysex::ScanKind kind = sysex::scanFile(bytes.data(), bytes.size(), r);
  std::string reply = "[{\"e\":\"import\",\"kind\":";
  if (kind == sysex::ScanKind::Voice) {
    Command c;
    c.type = CmdType::LoadVoice;
    c.src = Source::BankManager;
    c.b = r.checksumOk ? 0 : 1;
    inst_.postPayload(c, r.data, r.dataLen);
    reply += "\"voice\"";
  } else if (kind == sysex::ScanKind::Bank) {
    pendingBank_.assign(r.data, r.data + r.dataLen);
    pendingName_ = stem;
    pendingChecksumError_ = !r.checksumOk;
    reply += "\"bank\"";
  } else {
    Command c;
    c.type = CmdType::FileRejected;
    c.src = Source::BankManager;
    post(c);
    reply += "\"reject\"";
  }
  reply += ",\"name\":" + quote(name) + "}]";
  return reply;
}

std::string Bridge::drain(const std::function<void(ExportFile&&)>& onExport) {
  std::string out;
  Event e;
  while (inst_.pollEvent(e)) {
    if (e.type == EvType::ExportData) {
      ExportFile f;
      f.kind = ExportKind(e.a);
      f.name = textOf(e, 64);
      f.bytes.resize(e.len);
      const int n = inst_.readPayload(e, f.bytes.data(), int(f.bytes.size()));
      f.bytes.resize(size_t(n));
      if (onExport) onExport(std::move(f));
      continue;
    }
    if (e.slot != 0xFFFFFFFFu) inst_.readPayload(e, nullptr, 0);  // release any other payload
    if (e.type == EvType::StateDirty) {
      stateDirty_ = true;
      continue;
    }
    if (e.type == EvType::VoiceName && e.a >= 0 && e.a < 2 && e.b >= 1 && e.b <= 32) names_[e.a][e.b - 1] = fixed(e, 0, 10);
    if (e.type == EvType::Memory) {
      srcBank_ = e.b;
      srcSlot_ = e.c;
    }
    const std::string js = encodeEvent(e);
    if (js.empty()) continue;
    out += out.empty() ? "[" : ",";
    out += js;
  }
  if (!out.empty()) out += "]";
  return out;
}

std::string Bridge::voiceName(int bank, int slot) const {
  if (bank < 0 || bank > 1 || slot < 1 || slot > 32) return std::string();
  return names_[bank][slot - 1];
}

}  // namespace vdx::bridge
