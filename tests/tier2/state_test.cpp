// State blob (SPEC §4.8, R-D1, R-D6): AC-D1.1, AC-D1.2, AC-D6.1.
#include "rig.h"
#include "sysex/default_bank.h"
#include "sysex/state.h"
#include "sysex/sysex.h"

using namespace rig;
using namespace vdx;
using namespace vdx::params;

namespace {

bool samePersist(const state::Persist& a, const state::Persist& b) { return std::memcmp(&a, &b, sizeof a) == 0; }

void putU32(std::vector<uint8_t>& b, size_t at, uint32_t v) {
  for (int k = 0; k < 4; ++k) b[at + size_t(k)] = uint8_t(v >> (8 * k));
}

void fixCrc(std::vector<uint8_t>& b) { putU32(b, b.size() - 4, sysex::crc32(b.data(), b.size() - 4)); }

// Every §4.8 field set to a non-default value, through the public command paths.
void nonDefaultState(Rig& r) {
  r.global(Global::ProtectInt, 0);
  r.global(Global::ProtectCrt, 0);
  // internal bank: store an edited voice into INT 20
  r.press(4);
  r.host(opParam(2, kOpLevel), 33);
  r.press(kBtnStore);
  r.press(20);
  // cartridge: present, named, changed
  std::vector<uint8_t> p(sysex::defaultBankPacked(), sysex::defaultBankPacked() + 4096);
  p[5 * 128 + 118] = 'Q';
  const char* name = "Some cartridge name";
  p.insert(p.end(), name, name + std::strlen(name) + 1);
  r.postPayload(cmd(CmdType::LoadBank, Source::BankManager, kBankCrt), p.data(), int(p.size()));
  r.render(64);
  r.press(kBtnMemCrt);
  r.press(6);  // CRT 6 = the modified slot (voice source CRT 6, stored voice)
  r.host(kAlgorithm, 30);
  r.press(kBtnStore);
  r.press(7);  // changed flag
  // recall buffer = an edited voice, then a selection
  r.host(kFeedback, 2);
  r.press(9);  // recall <- edited; edit buffer = CRT 9
  r.host(kLfoSpeed, 12);  // EDITED
  // function parameters, globals, settings
  for (int n = kFnFirst; n <= kFnLast; ++n) r.hostFn(n, functionMax(n) - (n % 2));
  r.global(Global::MasterTune, 100);
  r.global(Global::MidiChannel, 11);
  r.global(Global::Omni, 0);
  r.global(Global::SysInfo, 1);
  r.global(Global::Volume, 77);
  const int settings[7] = {1, 0, 1, 1, 0, 130, 1};
  for (int s = 0; s < 7; ++s) r.simple(CmdType::SetSetting, Source::Programmer, s, settings[s]);
  r.press(kBtnMemInt);  // selected memory INT, voice source CRT 9
}

}  // namespace

TEST_CASE("AC-D1.1 every state field survives saveState -> loadState", "[state]") {
  Rig a;
  nonDefaultState(a);
  // The raw packed copy of the edit buffer (stray bits) survives too.
  {
    auto& ps = a.core().ps;
    ps.edit.packed[11] |= 0x70;
    ps.edit.packedValid = 1;
    ps.stored.packed[13] |= 0x60;
    ps.stored.packedValid = 1;
    ps.recall.raw[kName] = 0x7F;
    a.core().refresh();
    a.render(64);
  }
  const state::Persist before = a.ps();
  CHECK(before.edited == 1);
  CHECK(before.crtPresent == 1);
  CHECK(before.crtChanged == 1);
  CHECK(before.srcBank == kBankCrt);
  CHECK(before.srcSlot == 9);
  CHECK(before.selMem == kBankInt);
  StateBlob blob;
  a.inst.saveState(blob);
  REQUIRE(blob.bytes.size() == state::blobBytesV1());
  CHECK(std::memcmp(blob.bytes.data(), "VDX1", 4) == 0);
  CHECK(blob.bytes[4] == 1);
  CHECK(blob.bytes[5] == 0);
  CHECK(Core::validate(blob.bytes.data(), blob.bytes.size()) == Core::LoadResult::Ok);

  Rig b;
  REQUIRE(b.inst.loadState(blob));
  b.render(64);
  CHECK(samePersist(b.ps(), before));
  CHECK(b.core().lastLoad == Core::LoadResult::Ok);
  // Settings reach the engine settings struct.
  CHECK(b.inst.core().settings().engine == EngineMode::Modern);
  CHECK_FALSE(b.inst.core().settings().dac);
  CHECK(b.inst.core().masterVolume() == 77);
  // A save of the loaded state is byte-identical.
  StateBlob again;
  b.inst.saveState(again);
  CHECK(again.bytes == blob.bytes);
  // Panel mode and dialogs are not saved: the loaded instance is in PLAY.
  CHECK(b.panelMode == kModePlay);
}

TEST_CASE("AC-D1.2 corrupt blobs reset to first-run state with STATE RESET; zero-length silently", "[state]") {
  Rig good;
  good.press(5);
  good.host(kAlgorithm, 9);
  StateBlob blob;
  good.inst.saveState(blob);
  const std::vector<uint8_t> valid = blob.bytes;

  struct Case {
    const char* what;
    std::vector<uint8_t> bytes;
  };
  std::vector<Case> cases;
  {
    auto b = valid;
    b[100] ^= 0x01;  // bad CRC
    cases.push_back({"bad CRC", b});
  }
  {
    auto b = valid;
    b[0] = 'X';
    fixCrc(b);
    cases.push_back({"bad magic", b});
  }
  {
    auto b = valid;
    b.resize(b.size() / 2);
    cases.push_back({"truncated", b});
  }
  {
    auto b = valid;
    b.resize(6);
    cases.push_back({"truncated header", b});
  }
  {
    auto b = valid;
    b[4] = 0;
    b[5] = 0;
    fixCrc(b);
    cases.push_back({"version 0", b});
  }
  for (const auto& c : cases) {
    INFO(c.what);
    CHECK(Core::validate(c.bytes.data(), c.bytes.size()) == Core::LoadResult::Reset);
    Rig r;
    r.press(7);
    r.host(kFeedback, 5);
    StateBlob s;
    s.bytes = c.bytes;
    CHECK_FALSE(r.inst.loadState(s));
    r.render(64);
    CHECK(r.core().lastLoad == Core::LoadResult::Reset);
    Rig fresh;
    CHECK(samePersist(r.ps(), fresh.ps()));
    // The message appears after the splash.
    r.simple(CmdType::StartSplash, Source::Internal);
    CHECK(r.l1 == "      VDX       ");
    r.ms(1550);
    CHECK(r.l1 == "INTERNAL VOICE  ");
    CHECK(r.l2 == "  STATE RESET   ");
    r.ms(2100);
    CHECK(r.l2 == "INT 1 INIT VOICE");
  }
  // Zero-length: first-run state, no message.
  {
    Rig r;
    r.press(7);
    StateBlob empty;
    CHECK(Core::validate(nullptr, 0) == Core::LoadResult::Empty);
    CHECK(r.inst.loadState(empty));
    r.render(64);
    CHECK(r.core().lastLoad == Core::LoadResult::Empty);
    Rig fresh;
    CHECK(samePersist(r.ps(), fresh.ps()));
    r.splashAndWait();
    CHECK(r.l2 == "INT 1 INIT VOICE");
  }
  // After the splash has run, a reset shows the message at once.
  {
    Rig r;
    r.splashAndWait();
    StateBlob s;
    s.bytes = cases[0].bytes;
    r.inst.loadState(s);
    r.render(64);
    CHECK(r.l2 == "  STATE RESET   ");
  }
}

TEST_CASE("AC-D1.2 a blob from a newer version loads banks, buffers and function parameters only", "[state]") {
  Rig a;
  nonDefaultState(a);
  StateBlob blob;
  a.inst.saveState(blob);
  const state::Persist src = a.ps();
  // A hypothetical version 2: same prefix, a different (longer) rest.
  std::vector<uint8_t> v2(blob.bytes.begin(), blob.bytes.begin() + long(state::kHeaderBytes + state::prefixBytes()));
  v2[4] = 2;
  for (int i = 0; i < 40; ++i) v2.push_back(uint8_t(0xA0 + i));
  v2.resize(v2.size() + 4);
  fixCrc(v2);
  CHECK(Core::validate(v2.data(), v2.size()) == Core::LoadResult::Ok);
  Rig b;
  StateBlob s;
  s.bytes = v2;
  REQUIRE(b.inst.loadState(s));
  b.render(64);
  const state::Persist& got = b.ps();
  CHECK(std::memcmp(got.intBank, src.intBank, 4096) == 0);
  CHECK(std::memcmp(got.crtBank, src.crtBank, 4096) == 0);
  CHECK(got.crtPresent == src.crtPresent);
  CHECK(std::memcmp(&got.edit, &src.edit, sizeof got.edit) == 0);
  CHECK(std::memcmp(&got.stored, &src.stored, sizeof got.stored) == 0);
  CHECK(std::memcmp(&got.recall, &src.recall, sizeof got.recall) == 0);
  CHECK(got.edited == src.edited);
  CHECK(std::memcmp(got.fn, src.fn, sizeof got.fn) == 0);
  // The rest keeps its first-run values.
  Rig fresh;
  CHECK(std::memcmp(got.globals, fresh.ps().globals, sizeof got.globals) == 0);
  CHECK(std::memcmp(got.settings, fresh.ps().settings, sizeof got.settings) == 0);
  // A newer blob too short for the prefix is a reset.
  std::vector<uint8_t> shortV2(v2.begin(), v2.begin() + 100);
  shortV2.resize(104);
  fixCrc(shortV2);
  CHECK(Core::validate(shortV2.data(), shortV2.size()) == Core::LoadResult::Reset);
}

TEST_CASE("AC-D6.1 empty storage starts in the R-D6 first-run state", "[state]") {
  Rig r;
  const state::Persist& ps = r.ps();
  CHECK(std::memcmp(ps.intBank, sysex::defaultBankPacked(), 4096) == 0);
  CHECK(ps.crtPresent == 0);
  for (int n = kFnFirst; n <= kFnLast; ++n) CHECK(int(ps.fn[n - kFnFirst]) == functionDefault(n));
  CHECK(ps.fn[kFnBendRange - kFnFirst] == 2);
  CHECK(ps.fn[kFnWheelRange - kFnFirst] == 99);
  CHECK(ps.fn[kFnWheelAssign - kFnFirst] == 1);
  CHECK(ps.globals[int(Global::MasterTune)] == 64);
  CHECK(ps.globals[int(Global::MidiChannel)] == 1);
  CHECK(ps.globals[int(Global::Omni)] == 1);
  CHECK(ps.globals[int(Global::SysInfo)] == 0);
  CHECK(ps.globals[int(Global::ProtectInt)] == 1);
  CHECK(ps.globals[int(Global::ProtectCrt)] == 1);
  CHECK(ps.globals[int(Global::Volume)] == 127);
  CHECK(ps.settings[int(SettingId::Engine)] == 0);
  CHECK(ps.settings[int(SettingId::Dac)] == 1);
  CHECK(ps.settings[int(SettingId::HostRate)] == 0);
  CHECK(ps.settings[int(SettingId::ClassicVelocity)] == 0);
  CHECK(ps.settings[int(SettingId::Zoom)] == 0);
  CHECK(ps.settings[int(SettingId::OpOrder)] == 0);
  uint8_t init[155];
  sysex::initVoiceRaw(init);
  CHECK(std::memcmp(ps.edit.raw, init, 155) == 0);
  CHECK(std::memcmp(ps.stored.raw, init, 155) == 0);
  CHECK(std::memcmp(ps.recall.raw, init, 155) == 0);
  CHECK(ps.edited == 0);
  CHECK(ps.srcBank == kBankInt);
  CHECK(ps.srcSlot == 1);
  CHECK(ps.selMem == kBankInt);
  CHECK(r.core().opMask == 0x3F);
  // A zero-length blob (new plugin instance) gives the same state.
  Rig z;
  StateBlob empty;
  z.inst.loadState(empty);
  z.render(64);
  CHECK(samePersist(z.ps(), ps));
  // The splash then shows PLAY with the voice source's memory selected.
  r.splashAndWait();
  CHECK(r.l1 == "INTERNAL VOICE  ");
  CHECK(r.l2 == "INT 1 INIT VOICE");
  CHECK(r.led == 1);
}

TEST_CASE("state: layout offsets match docs/STATE.md", "[state]") {
  CHECK(state::blobBytesV1() == 9155);
  CHECK(state::prefixBytes() == 9128);
  Rig r;
  r.global(Global::ProtectInt, 0);
  std::vector<uint8_t> p(sysex::defaultBankPacked(), sysex::defaultBankPacked() + 4096);
  const char* name = "NAMED";
  p.insert(p.end(), name, name + 6);
  r.postPayload(cmd(CmdType::LoadBank, Source::BankManager, kBankCrt), p.data(), int(p.size()));
  r.render(64);
  r.press(kBtnMemCrt);
  r.press(7);
  r.host(kFeedback, 3);
  r.hostFn(kFnAtAssign, 5);
  r.global(Global::Volume, 99);
  r.simple(CmdType::SetSetting, Source::Programmer, int(SettingId::Zoom), 120);
  StateBlob b;
  r.inst.saveState(b);
  REQUIRE(b.bytes.size() == 9155);
  const auto& d = b.bytes;
  CHECK(std::memcmp(d.data() + 8, sysex::defaultBankPacked(), 4096) == 0);
  CHECK(d[4104] == 1);
  CHECK(d[4106] == 5);
  CHECK(std::memcmp(d.data() + 4107, "NAMED", 5) == 0);
  CHECK(std::memcmp(d.data() + 4171, sysex::defaultBankPacked(), 4096) == 0);
  CHECK(d[8267 + kFeedback] == 3);
  CHECK(d[8551] == 1);
  CHECK(d[9120] == kBankCrt);
  CHECK(d[9121] == 7);
  CHECK(d[9122 + 13] == 5);
  CHECK(d[9136] == kBankCrt);
  CHECK(d[9137 + int(Global::Volume)] == 99);
  CHECK(d[9144 + int(SettingId::Zoom)] == 120);
  const uint32_t crc = uint32_t(d[9151]) | (uint32_t(d[9152]) << 8) | (uint32_t(d[9153]) << 16) | (uint32_t(d[9154]) << 24);
  CHECK(crc == sysex::crc32(d.data(), 9151));
}

TEST_CASE("state: serialize is bounded and StateDirty follows saved-state changes", "[state]") {
  CHECK(state::blobBytesV1() <= kMaxStateBytes);
  Rig r;
  r.log.clear();
  r.press(kBtnEdit);  // panel mode only: not saved
  CHECK(r.count(EvType::StateDirty) == 0);
  r.press(kBtnMemInt);
  r.log.clear();
  r.press(3);  // voice selection: saved
  CHECK(r.count(EvType::StateDirty) == 1);
  r.log.clear();
  r.global(Global::Volume, 50);
  CHECK(r.count(EvType::StateDirty) == 1);
}
