// SysEx data: pack/unpack, messages, checksum, CRC32, file scan (AC-D4.1),
// exact round trip (AC-D3.1), default bank (AC-D7.1 names, §15.3), INIT VOICE (AC-P10.1).
#include <filesystem>
#include <fstream>
#include <iterator>

#include "rig.h"
#include "sysex/default_bank.h"
#include "sysex/sysex.h"

using namespace rig;
using namespace vdx;
using namespace vdx::params;

namespace {

std::vector<uint8_t> readFile(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

std::vector<std::string> listSyx(const std::string& dir) {
  std::vector<std::string> out;
  std::error_code ec;
  if (!std::filesystem::is_directory(dir, ec)) return out;
  for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
    std::string ext = e.path().extension().string();
    for (auto& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
    if (e.is_regular_file(ec) && ext == ".syx") out.push_back(e.path().string());
  }
  return out;
}

const Ev* exportOf(Rig& r, ExportKind k) {
  r.log.clear();
  r.simple(CmdType::Export, Source::BankManager, int(k));
  return r.last(EvType::ExportData);
}

void loadBank(Rig& r, int bank, const uint8_t* packed, const char* name = "BANK") {
  std::vector<uint8_t> p(packed, packed + 4096);
  for (const char* c = name; *c; ++c) p.push_back(uint8_t(*c));
  p.push_back(0);
  r.postPayload(cmd(CmdType::LoadBank, Source::BankManager, bank, 0), p.data(), int(p.size()));
  r.render(64);
}

// R-P10 INIT VOICE, written out by hand (OP6 first).
std::vector<uint8_t> initVoiceBytes() {
  std::vector<uint8_t> v;
  for (int op = 6; op >= 1; --op) {
    const uint8_t o[21] = {99, 99, 99, 99, 99, 99, 99, 0, 0, 0, 0, 0, 0, 0, 0, 0, uint8_t(op == 1 ? 99 : 0), 0, 1, 0, 7};
    v.insert(v.end(), o, o + 21);
  }
  const uint8_t g[19] = {99, 99, 99, 99, 50, 50, 50, 50, 0, 0, 1, 35, 0, 0, 0, 1, 0, 3, 24};
  v.insert(v.end(), g, g + 19);
  const char* name = "INIT VOICE";
  v.insert(v.end(), name, name + 10);
  return v;
}

}  // namespace

TEST_CASE("sysex: checksum and CRC32 reference values", "[sysex]") {
  const uint8_t d[3] = {1, 2, 3};
  CHECK(sysex::checksum(d, 3) == ((-6) & 0x7F));
  const char* s = "123456789";
  CHECK(sysex::crc32(reinterpret_cast<const uint8_t*>(s), 9) == 0xCBF43926u);
  CHECK(sysex::crc32(nullptr, 0) == 0u);
}

TEST_CASE("sysex: pack/unpack layout (SPEC 4.6) and raw packed copy", "[sysex]") {
  uint8_t raw[155];
  sysex::initVoiceRaw(raw);
  // Distinct values in every field.
  for (int op = 1; op <= 6; ++op) {
    const int b = opBase(op);
    raw[b + kOpLeftCurve] = 1;
    raw[b + kOpRightCurve] = 2;
    raw[b + kOpRateScaling] = 5;
    raw[b + kOpDetune] = 11;
    raw[b + kOpAms] = 3;
    raw[b + kOpKvs] = 6;
    raw[b + kOpMode] = 1;
    raw[b + kOpCoarse] = 17;
    raw[b + kOpLevel] = 77;
    raw[b + kOpFine] = 42;
  }
  raw[kFeedback] = 5;
  raw[kOscKeySync] = 1;
  raw[kLfoWave] = 4;
  raw[kPitchModSens] = 6;
  raw[kLfoKeySync] = 0;
  uint8_t p[128];
  sysex::packCurrent(raw, p);
  CHECK(p[11] == ((2 << 2) | 1));
  CHECK(p[12] == ((11 << 3) | 5));
  CHECK(p[13] == ((6 << 2) | 3));
  CHECK(p[14] == 77);
  CHECK(p[15] == ((17 << 1) | 1));
  CHECK(p[16] == 42);
  CHECK(p[111] == ((1 << 3) | 5));
  CHECK(p[116] == ((6 << 4) | (4 << 1) | 0));
  CHECK(p[117] == 24);
  CHECK(std::string(reinterpret_cast<const char*>(p + 118), 10) == "INIT VOICE");
  sysex::Voice v;
  sysex::unpackVoice(p, v);
  CHECK(std::vector<uint8_t>(v.raw, v.raw + 155) == std::vector<uint8_t>(raw, raw + 155));
  // Stray bits are kept only in the raw packed copy; unedited packing returns it.
  p[11] |= 0x70;
  p[15] |= 0x40;
  sysex::unpackVoice(p, v);
  uint8_t q[128];
  sysex::packVoice(v, q);
  CHECK(std::memcmp(p, q, 128) == 0);
  // Editing packs current values with unused bits = 0.
  sysex::setByte(v, opParam(6, kOpLevel), 50);
  sysex::packVoice(v, q);
  CHECK((q[11] & 0x70) == 0);
  CHECK((q[15] & 0x40) == 0);
  CHECK(q[14] == 50);
}

TEST_CASE("sysex: message builders and parser (SPEC 4.7)", "[sysex]") {
  uint8_t raw[155];
  sysex::initVoiceRaw(raw);
  uint8_t m[4104];
  REQUIRE(sysex::buildVoiceDump(raw, 3, m) == 163);
  CHECK(m[0] == 0xF0);
  CHECK(m[1] == 0x43);
  CHECK(m[2] == 0x03);
  CHECK(m[3] == 0x00);
  CHECK(m[4] == 0x01);
  CHECK(m[5] == 0x1B);
  CHECK(m[162] == 0xF7);
  sysex::Parsed p;
  REQUIRE(sysex::parseMessage(m, 163, p));
  CHECK(p.kind == sysex::MsgKind::Voice);
  CHECK(p.device == 3);
  CHECK(p.checksumOk);
  m[161] ^= 1;
  REQUIRE(sysex::parseMessage(m, 163, p));
  CHECK_FALSE(p.checksumOk);
  CHECK_FALSE(sysex::parseMessage(m, 162, p));  // wrong length
  m[1] = 0x41;
  CHECK_FALSE(sysex::parseMessage(m, 163, p));  // wrong manufacturer

  REQUIRE(sysex::buildBankDump(sysex::defaultBankPacked(), 0, m) == 4104);
  CHECK(std::memcmp(m, sysex::defaultBankSyx(), 4104) == 0);
  REQUIRE(sysex::parseMessage(m, 4104, p));
  CHECK(p.kind == sysex::MsgKind::Bank);

  REQUIRE(sysex::buildParamChange(0, 0, 155, 0x2A, m) == 7);
  const uint8_t want[7] = {0xF0, 0x43, 0x10, 0x01, 155 - 128, 0x2A, 0xF7};
  CHECK(std::memcmp(m, want, 7) == 0);
  REQUIRE(sysex::parseMessage(m, 7, p));
  CHECK(p.kind == sysex::MsgKind::ParamChange);
  CHECK(p.group == 0);
  CHECK(p.param == 155);
  CHECK(p.value == 0x2A);
  sysex::buildParamChange(5, 2, 69, 50, m);
  const uint8_t want2[7] = {0xF0, 0x43, 0x15, 0x08, 69, 50, 0xF7};
  CHECK(std::memcmp(m, want2, 7) == 0);

  REQUIRE(sysex::buildDumpRequest(0, 9, m) == 5);
  const uint8_t req[5] = {0xF0, 0x43, 0x20, 0x09, 0xF7};
  CHECK(std::memcmp(m, req, 5) == 0);
  REQUIRE(sysex::parseMessage(m, 5, p));
  CHECK(p.kind == sysex::MsgKind::DumpRequest);
  CHECK(p.format == 9);

  // F-13 filter
  p.device = 2;
  CHECK_FALSE(sysex::acceptReceived(p, false, true, 3));
  CHECK(sysex::acceptReceived(p, true, false, 3));
  CHECK_FALSE(sysex::acceptReceived(p, true, false, 4));
  CHECK(sysex::acceptReceived(p, true, true, 4));
}

TEST_CASE("AC-D4.1 file import (R-D4)", "[sysex]") {
  sysex::ScanResult r;
  // 0 bytes: reject
  CHECK(sysex::scanFile(nullptr, 0, r) == sysex::ScanKind::Reject);
  // 1 MiB + 1: reject (even with a valid bank at the start)
  std::vector<uint8_t> big(sysex::kMaxFileBytes + 1, 0);
  std::memcpy(big.data(), sysex::defaultBankSyx(), 4104);
  CHECK(sysex::scanFile(big.data(), big.size(), r) == sysex::ScanKind::Reject);
  big.resize(sysex::kMaxFileBytes);
  CHECK(sysex::scanFile(big.data(), big.size(), r) == sysex::ScanKind::Bank);
  // 4096-byte headerless bank
  std::vector<uint8_t> hl(sysex::defaultBankPacked(), sysex::defaultBankPacked() + 4096);
  REQUIRE(sysex::scanFile(hl.data(), hl.size(), r) == sysex::ScanKind::Bank);
  CHECK(r.checksumOk);
  CHECK(std::memcmp(r.data, hl.data(), 4096) == 0);
  // junk before a voice dump
  uint8_t raw[155];
  sysex::initVoiceRaw(raw);
  raw[kAlgorithm] = 17;
  uint8_t vm[163];
  sysex::buildVoiceDump(raw, 0, vm);
  std::vector<uint8_t> junk = {0x00, 0x12, 0xF0, 0x41, 0x10, 0xF7, 0x99, 0xF0, 0x43, 0x01};  // incl. a foreign and an aborted message
  junk.insert(junk.end(), vm, vm + 163);
  junk.push_back(0x55);
  REQUIRE(sysex::scanFile(junk.data(), junk.size(), r) == sysex::ScanKind::Voice);
  CHECK(r.dataLen == 155);
  CHECK(r.checksumOk);
  CHECK(std::memcmp(r.data, raw, 155) == 0);
  // two concatenated banks: the first one
  uint8_t second[4104];
  std::vector<uint8_t> other(4096, 0x11);
  sysex::buildBankDump(other.data(), 0, second);
  std::vector<uint8_t> two(sysex::defaultBankSyx(), sysex::defaultBankSyx() + 4104);
  two.insert(two.end(), second, second + 4104);
  REQUIRE(sysex::scanFile(two.data(), two.size(), r) == sysex::ScanKind::Bank);
  CHECK(std::memcmp(r.data, sysex::defaultBankPacked(), 4096) == 0);
  // bad checksum: loaded, flagged
  vm[161] ^= 0x01;
  REQUIRE(sysex::scanFile(vm, 163, r) == sysex::ScanKind::Voice);
  CHECK_FALSE(r.checksumOk);
  // no usable message (a WAV header): reject
  const char wav[] = "RIFF\x24\x08\x00\x00WAVEfmt ";
  CHECK(sysex::scanFile(reinterpret_cast<const uint8_t*>(wav), sizeof wav, r) == sysex::ScanKind::Reject);

  // Core side: a voice with a bad checksum loads and shows CHECK SUM ERROR!; a reject shows FORMAT CONFLICT!.
  Rig rg;
  rg.postPayload(cmd(CmdType::LoadVoice, Source::BankManager, 0, 1), r.data, 155);
  rg.render(64);
  CHECK(rg.raw(kAlgorithm) == 17);
  CHECK(rg.edited());
  CHECK(rg.l2 == "CHECK SUM ERROR!");
  rg.simple(CmdType::FileRejected, Source::BankManager);
  CHECK(rg.l2 == "FORMAT CONFLICT!");
}

namespace {

void roundTripBank(const std::vector<uint8_t>& file, const std::string& what) {
  INFO(what);
  sysex::ScanResult s;
  REQUIRE(sysex::scanFile(file.data(), file.size(), s) != sysex::ScanKind::Reject);
  if (s.kind == sysex::ScanKind::Voice) {
    Rig r;
    r.postPayload(cmd(CmdType::LoadVoice, Source::BankManager, 0, s.checksumOk ? 0 : 1), s.data, 155);
    r.render(64);
    const Ev* ex = exportOf(r, ExportKind::Voice);
    REQUIRE(ex != nullptr);
    REQUIRE(ex->payload.size() == 163);
    CHECK(std::memcmp(ex->payload.data() + 6, s.data, 155) == 0);
    return;
  }
  // Insert as cartridge, export.
  Rig r;
  r.global(Global::ProtectInt, 0);
  loadBank(r, kBankCrt, s.data, "ROUNDTRIP");
  const Ev* ex = exportOf(r, ExportKind::Cartridge);
  REQUIRE(ex != nullptr);
  REQUIRE(ex->payload.size() == 4104);
  CHECK(std::memcmp(ex->payload.data() + 6, s.data, 4096) == 0);
  CHECK(std::string(ex->e.text) == "ROUNDTRIP.syx");
  // Load into internal, export.
  loadBank(r, kBankInt, s.data);
  ex = exportOf(r, ExportKind::Internal);
  REQUIRE(ex != nullptr);
  CHECK(std::memcmp(ex->payload.data() + 6, s.data, 4096) == 0);
  // Select every voice and store it back unchanged: still identical.
  r.press(kBtnMemCrt);
  r.global(Global::ProtectCrt, 0);
  for (int slot = 1; slot <= 32; ++slot) {
    r.press(slot);
    r.press(kBtnStore);
    r.press(slot);
  }
  ex = exportOf(r, ExportKind::Cartridge);
  REQUIRE(ex != nullptr);
  CHECK(std::memcmp(ex->payload.data() + 6, s.data, 4096) == 0);
  // Through the state blob.
  StateBlob blob;
  r.inst.saveState(blob);
  Rig r2;
  REQUIRE(r2.inst.loadState(blob));
  r2.render(64);
  ex = exportOf(r2, ExportKind::Cartridge);
  REQUIRE(ex != nullptr);
  CHECK(std::memcmp(ex->payload.data() + 6, s.data, 4096) == 0);
  ex = exportOf(r2, ExportKind::Internal);
  REQUIRE(ex != nullptr);
  CHECK(std::memcmp(ex->payload.data() + 6, s.data, 4096) == 0);
  // Each voice as a single-voice file: export, re-import, export again.
  for (int slot = 1; slot <= 32; ++slot) {
    r2.press(slot);
    ex = exportOf(r2, ExportKind::Voice);
    REQUIRE(ex != nullptr);
    const std::vector<uint8_t> first = ex->payload;
    sysex::ScanResult v;
    REQUIRE(sysex::scanFile(first.data(), first.size(), v) == sysex::ScanKind::Voice);
    r2.postPayload(cmd(CmdType::LoadVoice, Source::BankManager), v.data, 155);
    r2.render(64);
    ex = exportOf(r2, ExportKind::Voice);
    REQUIRE(ex != nullptr);
    CHECK(ex->payload == first);
  }
  // A full file that is a valid 4104-byte dump on device 0 with a good checksum comes back byte-identical.
  if (file.size() == 4104 && file[2] == 0 && s.checksumOk) {
    ex = exportOf(r2, ExportKind::Internal);
    REQUIRE(ex != nullptr);
    CHECK(ex->payload == file);
  }
}

}  // namespace

TEST_CASE("AC-D3.1 exact round trip: banks/vdx-default.syx", "[sysex]") {
  const auto file = readFile(sourceDir() + "/banks/vdx-default.syx");
  REQUIRE(file.size() == 4104);
  roundTripBank(file, "banks/vdx-default.syx");
}

TEST_CASE("AC-D3.1 exact round trip: synthetic bank with out-of-range values, stray bits, name bytes 0x00/0x7F",
          "[sysex]") {
  const auto file = readFile(sourceDir() + "/tests/golden/synthetic_bank.syx");
  REQUIRE(file.size() == 4104);
  // The fixture really contains what it claims.
  const uint8_t* d = file.data() + 6;
  CHECK(d[0] > 99);
  CHECK((d[11] & 0x70) != 0);
  CHECK(d[110] > 31);
  CHECK(d[118] == 0x00);
  CHECK(d[119] == 0x7F);
  roundTripBank(file, "tests/golden/synthetic_bank.syx");
  // Effective values clamp; raw bytes are kept (§4.1).
  Rig r;
  sysex::ScanResult s;
  sysex::scanFile(file.data(), file.size(), s);
  loadBank(r, kBankCrt, s.data);
  r.press(kBtnMemCrt);
  r.press(1);
  CHECK(r.raw(kAlgorithm) == 127);
  r.press(kBtnEdit);
  CHECK(r.l1.substr(0, 5) == "ALG32");
  CHECK(r.l2.substr(0, 6) == "ALGORI");
  r.press(32);
  r.press(kBtnFunction);
  CHECK(r.l2 == "NAME=   A  B   C");
}

TEST_CASE("AC-D3.1 exact round trip: banks/external (skipped if absent)", "[sysex]") {
  const auto files = listSyx(sourceDir() + "/banks/external");
  if (files.empty()) SKIP("banks/external is empty (tools/fetch_banks.sh downloads it)");
  for (const auto& f : files) {
    const auto bytes = readFile(f);
    sysex::ScanResult s;
    if (sysex::scanFile(bytes.data(), bytes.size(), s) == sysex::ScanKind::Reject) continue;
    roundTripBank(bytes, f);
  }
}

TEST_CASE("AC-D7.1 default bank names and embedded copy (SPEC 15.3)", "[sysex]") {
  const auto file = readFile(sourceDir() + "/banks/vdx-default.syx");
  REQUIRE(file.size() == 4104);
  // The committed .inc embedded in the core equals the committed .syx.
  CHECK(std::memcmp(file.data(), sysex::defaultBankSyx(), 4104) == 0);
  sysex::Parsed p;
  REQUIRE(sysex::parseMessage(file.data(), 4104, p));
  CHECK(p.kind == sysex::MsgKind::Bank);
  CHECK(p.checksumOk);
  const char* names[32] = {"INIT VOICE", "E.PIANO   ", "BELL      ", "BASS      ", "BRASS     ", "ORGAN     ",
                           "MARIMBA   ", "STRINGS   ", "PAD       "};
  const auto init = initVoiceBytes();
  for (int s = 0; s < 32; ++s) {
    INFO("slot " << s + 1);
    char n[10];
    sysex::packedName(sysex::defaultBankPacked() + s * 128, n);
    CHECK(std::string(n, 10) == (s < 9 ? names[s] : "INIT VOICE"));
    sysex::Voice v;
    sysex::unpackVoice(sysex::defaultBankPacked() + s * 128, v);
    if (s == 0 || s >= 9) {
      CHECK(std::vector<uint8_t>(v.raw, v.raw + 155) == init);
    } else {
      // Every original voice has at least one carrier with a real output level.
      const uint8_t carriers = sysex::carrierMask(v.raw[kAlgorithm]);
      int loudest = 0;
      for (int op = 1; op <= 6; ++op)
        if (carriers & (1 << (op - 1))) loudest = std::max(loudest, int(v.raw[opParam(op, kOpLevel)]));
      CHECK(loudest >= 90);
      for (int i = 0; i < 155; ++i) CHECK(int(v.raw[i]) == effective(i, v.raw[i]));  // all in range
    }
  }
  // Names as the core reports them.
  Rig r;
  r.post(cmd(CmdType::RequestSnapshot, Source::Internal));
  r.render(64);
  int seen = 0;
  for (const auto& x : r.log) {
    if (x.e.type != EvType::VoiceName || x.e.a != kBankInt) continue;
    const int s = x.e.b - 1;
    CHECK(std::string(x.e.text) == (s < 9 ? names[s] : "INIT VOICE"));
    ++seen;
  }
  CHECK(seen == 32);
}

TEST_CASE("AC-P10.1 VOICE INIT gives the R-P10 bytes", "[panel]") {
  Rig r;
  r.press(5);  // BRASS, so every byte differs from INIT VOICE somewhere
  r.press(kBtnFunction);
  r.press(10);
  r.press(kBtnYes);
  r.press(kBtnYes);
  CHECK(std::vector<uint8_t>(r.ps().edit.raw, r.ps().edit.raw + 155) == initVoiceBytes());
  CHECK(r.edited());
  CHECK(r.core().opMask == 0x3F);
  // Programmer Init voice and new cartridge slots use the same values.
  Rig r2;
  r2.press(4);
  r2.simple(CmdType::InitVoice);
  CHECK(std::vector<uint8_t>(r2.ps().edit.raw, r2.ps().edit.raw + 155) == initVoiceBytes());
  r2.newCartridge();
  sysex::Voice v;
  sysex::unpackVoice(r2.ps().crtBank + 31 * 128, v);
  CHECK(std::vector<uint8_t>(v.raw, v.raw + 155) == initVoiceBytes());
  CHECK(std::string(r2.ps().crtName, r2.ps().crtNameLen) == "UNTITLED");
}

TEST_CASE("sysex: export file names are sanitised (F-12.5)", "[sysex]") {
  Rig r;
  r.press(2);  // E.PIANO
  const Ev* ex = exportOf(r, ExportKind::Voice);
  REQUIRE(ex != nullptr);
  CHECK(std::string(ex->e.text) == "E_PIANO.syx");
  CHECK(ex->payload.size() == 163);
  ex = exportOf(r, ExportKind::Internal);
  REQUIRE(ex != nullptr);
  CHECK(std::string(ex->e.text) == "INTERNAL.syx");
  loadBank(r, kBankCrt, sysex::defaultBankPacked(), "My Bank (v2)/\xC3\xA9");
  ex = exportOf(r, ExportKind::Cartridge);
  REQUIRE(ex != nullptr);
  CHECK(std::string(ex->e.text) == "My Bank _v2____.syx");
  // Exporting does not clear the changed flag by itself; the shell confirms the write.
  r.global(Global::ProtectCrt, 0);
  r.press(kBtnMemCrt);
  r.press(kBtnStore);
  r.press(3);
  CHECK(r.ps().crtChanged == 1);
  r.simple(CmdType::ClearCartridgeChanged, Source::BankManager);
  CHECK(r.ps().crtChanged == 0);
}
