// Standalone state persistence (SPEC F-15, R-D2). JUCE-free so it can be unit
// tested on its own: the shell feeds it the state blob and the time.
//
//  * Directory: ~/Library/Application Support/VDX/, %APPDATA%\VDX\ or
//    ~/.config/VDX/ (the environment variable VDX_STATE_DIR overrides it),
//    created if missing.
//  * state.bin holds the blob; internal.syx (4104 bytes) holds the internal
//    bank as a bank dump. Both are written at the same time, each through a
//    temporary file that is renamed over the target, so a failed write leaves
//    the previous file intact.
//  * Timing: a save is due 1 s after the last change, and at least every 5 s
//    while changes continue. The shell saves once more on quit.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace vdx::shell {

// VDX_STATE_DIR if set and non-empty, else the platform directory (R-D2).
std::filesystem::path defaultStateDirectory();

// The 4104-byte bank dump of the internal bank stored in a state blob
// (docs/STATE.md). Returns false if the blob is not a valid state blob.
bool internalBankDump(const std::vector<uint8_t>& blob, std::vector<uint8_t>& out);

struct SaveResult {
  bool ok = true;
  std::string error;  // "Could not save state: <reason>" when !ok
};

class Persistence {
 public:
  static constexpr int64_t kQuietMs = 1000;     // save 1 s after the last change
  static constexpr int64_t kMaxDelayMs = 5000;  // ... and at least every 5 s

  explicit Persistence(std::filesystem::path directory = defaultStateDirectory());

  const std::filesystem::path& directory() const { return dir_; }
  std::filesystem::path statePath() const { return dir_ / "state.bin"; }
  std::filesystem::path bankPath() const { return dir_ / "internal.syx"; }

  // Reads state.bin; false if it does not exist or cannot be read.
  bool load(std::vector<uint8_t>& blob) const;

  // Scheduling. markChanged() on every change of the saved state.
  void markChanged(int64_t nowMs);
  bool dirty() const { return dirty_; }
  bool saveDue(int64_t nowMs) const;

  // Writes state.bin and internal.syx; clears the dirty flag (also on failure,
  // so a broken disk is retried on the next change instead of every tick).
  SaveResult save(const std::vector<uint8_t>& blob);

 private:
  std::filesystem::path dir_;
  bool dirty_ = false;
  int64_t firstChangeMs_ = 0;
  int64_t lastChangeMs_ = 0;
};

}  // namespace vdx::shell
