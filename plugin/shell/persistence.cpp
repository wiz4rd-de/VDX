#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "persistence.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <system_error>

#include "sysex/state.h"
#include "sysex/sysex.h"

namespace vdx::shell {

namespace fs = std::filesystem;

namespace {

std::string errnoText(int e) { return std::generic_category().message(e); }

std::string utf8(const fs::path& p) {
#if defined(__cpp_char8_t)
  const auto s = p.u8string();
  return std::string(s.begin(), s.end());
#else
  return p.u8string();
#endif
}

std::FILE* openFile(const fs::path& p, bool write) {
#if defined(_WIN32)
  return _wfopen(p.c_str(), write ? L"wb" : L"rb");
#else
  return std::fopen(p.c_str(), write ? "wb" : "rb");
#endif
}

// Writes `bytes` to `target` through `target.tmp` + rename. On failure the
// previous target is untouched; returns the reason.
bool writeAtomically(const fs::path& target, const std::vector<uint8_t>& bytes, std::string& reason) {
  fs::path tmp = target;
  tmp += ".tmp";
  errno = 0;
  std::FILE* f = openFile(tmp, true);
  if (!f) {
    reason = errnoText(errno ? errno : EIO) + " (" + utf8(tmp) + ")";
    return false;
  }
  const size_t n = bytes.empty() ? 0 : std::fwrite(bytes.data(), 1, bytes.size(), f);
  const int writeErr = (n != bytes.size()) ? (errno ? errno : EIO) : 0;
  const int closeErr = std::fclose(f) != 0 ? (errno ? errno : EIO) : 0;
  std::error_code ec;
  if (writeErr || closeErr) {
    reason = errnoText(writeErr ? writeErr : closeErr) + " (" + utf8(tmp) + ")";
    fs::remove(tmp, ec);
    return false;
  }
  fs::rename(tmp, target, ec);  // replaces an existing target on every platform
  if (ec) {
    reason = ec.message() + " (" + utf8(target) + ")";
    fs::remove(tmp, ec);
    return false;
  }
  return true;
}

fs::path pathFromEnv(const char* name) {
#if defined(_WIN32)
  std::wstring wname(name, name + std::char_traits<char>::length(name));
  const wchar_t* v = _wgetenv(wname.c_str());
  return (v && *v) ? fs::path(v) : fs::path();
#else
  const char* v = std::getenv(name);
  return (v && *v) ? fs::path(v) : fs::path();
#endif
}

}  // namespace

fs::path defaultStateDirectory() {
  const fs::path overridden = pathFromEnv("VDX_STATE_DIR");
  if (!overridden.empty()) return overridden;
#if defined(_WIN32)
  return pathFromEnv("APPDATA") / "VDX";
#elif defined(__APPLE__)
  return pathFromEnv("HOME") / "Library" / "Application Support" / "VDX";
#else
  return pathFromEnv("HOME") / ".config" / "VDX";
#endif
}

bool internalBankDump(const std::vector<uint8_t>& blob, std::vector<uint8_t>& out) {
  auto s = std::make_unique<state::Persist>();
  const state::ReadResult r = state::read(blob.data(), blob.size(), *s);
  if (r != state::ReadResult::Ok && r != state::ReadResult::Newer) return false;
  out.assign(sysex::kBankMsgBytes, 0);
  sysex::buildBankDump(s->intBank, 0, out.data());
  return true;
}

Persistence::Persistence(fs::path directory) : dir_(std::move(directory)) {}

bool Persistence::load(std::vector<uint8_t>& blob) const {
  std::FILE* f = openFile(statePath(), false);
  if (!f) return false;
  blob.clear();
  uint8_t buf[4096];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) blob.insert(blob.end(), buf, buf + n);
  const bool ok = !std::ferror(f);
  std::fclose(f);
  return ok;
}

void Persistence::markChanged(int64_t nowMs) {
  if (!dirty_) firstChangeMs_ = nowMs;
  dirty_ = true;
  lastChangeMs_ = nowMs;
}

bool Persistence::saveDue(int64_t nowMs) const {
  return dirty_ && (nowMs - lastChangeMs_ >= kQuietMs || nowMs - firstChangeMs_ >= kMaxDelayMs);
}

SaveResult Persistence::save(const std::vector<uint8_t>& blob) {
  dirty_ = false;
  SaveResult res;
  std::string reason;
  std::error_code ec;
  fs::create_directories(dir_, ec);
  if (ec || !fs::is_directory(dir_, ec)) {
    res.ok = false;
    res.error = "Could not save state: " + (ec ? ec.message() : std::string("not a directory")) + " (" +
                utf8(dir_) + ")";
    return res;
  }
  if (!writeAtomically(statePath(), blob, reason)) {
    res.ok = false;
    res.error = "Could not save state: " + reason;
    return res;
  }
  std::vector<uint8_t> bank;
  if (internalBankDump(blob, bank) && !writeAtomically(bankPath(), bank, reason)) {
    res.ok = false;
    res.error = "Could not save state: " + reason;
  }
  return res;
}

}  // namespace vdx::shell
