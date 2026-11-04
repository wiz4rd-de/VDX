// AC-F15.1: the standalone persistence module (plugin/shell/persistence.*),
// JUCE-free, driven with a real vdx::Instrument and a simulated clock.
#include <catch_amalgamated.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>

#include "persistence.h"
#include "sysex/state.h"
#include "sysex/sysex.h"
#include "vdx/params.h"
#include "vdx/vdx.h"

namespace fs = std::filesystem;
using vdx::shell::Persistence;

namespace {

void setEnv(const char* name, const std::string& value) {
#if defined(_WIN32)
  _putenv_s(name, value.c_str());
#else
  if (value.empty()) unsetenv(name);
  else setenv(name, value.c_str(), 1);
#endif
}

std::vector<uint8_t> readFile(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

struct TempDir {
  fs::path path;
  TempDir() {
    std::random_device rd;
    path = fs::temp_directory_path() / ("vdx-persist-" + std::to_string(rd()) + std::to_string(rd()));
  }
  ~TempDir() {
    std::error_code ec;
    fs::permissions(path, fs::perms::owner_all, fs::perm_options::add, ec);
    fs::remove_all(path, ec);
  }
};

void post(vdx::Instrument& inst, vdx::CmdType t, vdx::Source s, int a = 0, int b = 0) {
  vdx::Command c;
  c.type = t;
  c.src = s;
  c.a = a;
  c.b = b;
  inst.post(c);
}

void press(vdx::Instrument& inst, int button) {
  post(inst, vdx::CmdType::Button, vdx::Source::Panel, button, 1);
  post(inst, vdx::CmdType::Button, vdx::Source::Panel, button, 0);
}

// Renders a little and forwards StateDirty events to the scheduler at `nowMs`.
int pump(vdx::Instrument& inst, Persistence& p, int64_t nowMs) {
  float buf[512];
  for (int i = 0; i < 4; ++i) inst.render(buf, nullptr, 512);
  int dirty = 0;
  vdx::Event e;
  while (inst.pollEvent(e)) {
    if (e.slot != 0xFFFFFFFFu) inst.readPayload(e, nullptr, 0);
    if (e.type == vdx::EvType::StateDirty) {
      p.markChanged(nowMs);
      ++dirty;
    }
  }
  return dirty;
}

std::vector<uint8_t> blobOf(vdx::Instrument& inst) {
  vdx::StateBlob b;
  inst.saveState(b);
  return b.bytes;
}

int editOutputLevelOp1(const std::vector<uint8_t>& blob) {
  auto s = std::make_unique<vdx::state::Persist>();
  REQUIRE(vdx::state::read(blob.data(), blob.size(), *s) == vdx::state::ReadResult::Ok);
  return s->edit.raw[vdx::params::opParam(1, vdx::params::kOpLevel)];
}

}  // namespace

TEST_CASE("AC-F15.1 standalone persistence: edits then quit within 1 s; a failed write keeps the previous file") {
  TempDir tmp;
  const fs::path dir = tmp.path / "nested" / "VDX";  // created if missing
  setEnv("VDX_STATE_DIR", dir.string());
  REQUIRE(vdx::shell::defaultStateDirectory() == dir);
  Persistence p;
  REQUIRE(p.directory() == dir);
  setEnv("VDX_STATE_DIR", "");

  vdx::Instrument inst(48000.0, 512);
  std::vector<uint8_t> loaded;
  CHECK_FALSE(p.load(loaded));  // first run: no state.bin
  pump(inst, p, 0);
  CHECK_FALSE(p.dirty());

  // Edits at t = 0 and t = 400 ms: OP1 output level, then store the edit
  // buffer into internal slot 7 (protect off).
  const int olIndex = vdx::params::opParam(1, vdx::params::kOpLevel);
  post(inst, vdx::CmdType::SetGlobal, vdx::Source::Programmer, int(vdx::Global::ProtectInt), 0);
  post(inst, vdx::CmdType::SetVoiceParam, vdx::Source::Programmer, olIndex, 50);
  CHECK(pump(inst, p, 0) > 0);
  CHECK(p.dirty());
  post(inst, vdx::CmdType::SetVoiceParam, vdx::Source::Programmer, olIndex, 42);
  press(inst, vdx::kBtnMemInt);  // EDIT -> PLAY
  press(inst, vdx::kBtnStore);   // store dialog
  press(inst, 7);                // -> INT 7
  CHECK(pump(inst, p, 400) > 0);

  // Timing (F-15): 1 s after the last change, at least every 5 s.
  CHECK_FALSE(p.saveDue(900));
  CHECK_FALSE(p.saveDue(1399));
  CHECK(p.saveDue(1400));
  {
    Persistence q(tmp.path / "timing");
    for (int64_t t = 0; t <= 4500; t += 500) {
      q.markChanged(t);
      CHECK_FALSE(q.saveDue(t));
    }
    q.markChanged(5000);
    CHECK(q.saveDue(5000));
  }

  // Quit at t = 900 ms (within 1 s of the last edit): the final save.
  REQUIRE_FALSE(fs::exists(p.statePath()));
  const std::vector<uint8_t> blob = blobOf(inst);
  const auto saved = p.save(blob);
  REQUIRE(saved.ok);
  CHECK_FALSE(p.dirty());

  const auto stateBin = readFile(p.statePath());
  CHECK(stateBin == blob);
  CHECK(editOutputLevelOp1(stateBin) == 42);

  const auto syx = readFile(p.bankPath());
  REQUIRE(syx.size() == 4104);
  const uint8_t header[6] = {0xF0, 0x43, 0x00, 0x09, 0x20, 0x00};
  CHECK(std::equal(header, header + 6, syx.begin()));
  CHECK(syx.back() == 0xF7);
  auto s = std::make_unique<vdx::state::Persist>();
  REQUIRE(vdx::state::read(blob.data(), blob.size(), *s) == vdx::state::ReadResult::Ok);
  CHECK(std::equal(s->intBank, s->intBank + 4096, syx.begin() + 6));
  // Packed OP1 output level of slot 7: voice offset 6 * 128, OP1 block at 85, byte 14.
  CHECK(syx[6 + 6 * 128 + 85 + 14] == 42);
  CHECK(syx[6 + 4096] == vdx::sysex::checksum(syx.data() + 6, 4096));

  // Next start loads it.
  std::vector<uint8_t> again;
  REQUIRE(p.load(again));
  CHECK(again == blob);

  // A failed write leaves the previous files intact and reports the reason.
  post(inst, vdx::CmdType::SetVoiceParam, vdx::Source::Programmer, olIndex, 11);
  press(inst, vdx::kBtnMemInt);
  press(inst, vdx::kBtnStore);
  press(inst, 7);
  pump(inst, p, 2000);
  const std::vector<uint8_t> newer = blobOf(inst);
  REQUIRE(newer != blob);
  fs::create_directories(fs::path(p.statePath().string() + ".tmp"));  // the temporary file cannot be created
  const auto failed = p.save(newer);
  CHECK_FALSE(failed.ok);
  CHECK(failed.error.rfind("Could not save state: ", 0) == 0);
  CHECK(failed.error.size() > std::string("Could not save state: ").size());
  CHECK(readFile(p.statePath()) == blob);
  CHECK(readFile(p.bankPath()) == syx);

  // internal.syx alone failing also keeps its previous content.
  fs::remove_all(fs::path(p.statePath().string() + ".tmp"));
  fs::create_directories(fs::path(p.bankPath().string() + ".tmp"));
  const auto failed2 = p.save(newer);
  CHECK_FALSE(failed2.ok);
  CHECK(readFile(p.statePath()) == newer);
  CHECK(readFile(p.bankPath()) == syx);

  // Recovered disk: both files written again.
  fs::remove_all(fs::path(p.bankPath().string() + ".tmp"));
  REQUIRE(p.save(newer).ok);
  CHECK(editOutputLevelOp1(readFile(p.statePath())) == 11);
  CHECK(readFile(p.bankPath())[6 + 6 * 128 + 85 + 14] == 11);
}

TEST_CASE("AC-F15.1 standalone persistence: an unwritable state directory is reported, VDX keeps running") {
  TempDir tmp;
  fs::create_directories(tmp.path);
  const fs::path blocker = tmp.path / "file";
  { std::ofstream(blocker) << "x"; }
  Persistence p(blocker / "VDX");  // parent is a file: the directory cannot be created
  vdx::Instrument inst(48000.0, 512);
  const auto r = p.save(blobOf(inst));
  CHECK_FALSE(r.ok);
  CHECK(r.error.rfind("Could not save state: ", 0) == 0);
}
