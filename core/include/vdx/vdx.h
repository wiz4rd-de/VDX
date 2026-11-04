// VDX — public API of vdx_core (SPEC §9.4).
// The Instrument signatures listed in §9.4 are binding; everything marked
// "extension" is additional API used by the shells (plugin, standalone, web).
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace vdx {

enum class EngineMode : uint8_t { MkI = 0, Modern = 1 };

struct Settings {
  EngineMode engine = EngineMode::MkI;
  bool dac = true;
  bool hostRate = false;
  bool classicVelocity = false;
};

constexpr double kInternalRate = 49096.0;  // R-E1
constexpr int kBlock = 64;                  // N
constexpr int kVoices = 16;                 // R-E11

// Where a change came from. Drives transmit (F-13.6), host reports (R-S4)
// and the F-3 / R-S2 panel rules.
enum class Source : uint8_t {
  None = 0,
  Panel = 1,        // front panel (buttons, YES/NO, data entry, CC6/96/97)
  Programmer = 2,   // programmer controls, voice menu, operator menu
  Host = 3,         // host automation / host program
  SysEx = 4,        // received SysEx parameter change / dump
  Midi = 5,         // other received MIDI (CC5, CC7, program change, notes)
  Keyboard = 6,     // computer keyboard
  BankManager = 7,  // bank manager overlay
  State = 8,        // state load / reset
  Internal = 9,
};

// Front-panel inputs. 1..32 are the numbered buttons.
enum ButtonId : int32_t {
  kBtnMemInt = 33,
  kBtnMemCrt = 34,
  kBtnEdit = 35,      // EDIT/COMPARE
  kBtnFunction = 36,
  kBtnStore = 37,
  kBtnProtInt = 38,
  kBtnProtCrt = 39,
  kBtnOpSelect = 40,
  kBtnNo = 41,        // NO / -1
  kBtnYes = 42,       // YES / +1
};

// Panel globals (§4.3).
enum class Global : int32_t {
  MasterTune = 0,   // 0..127, 64 = 0 cents
  MidiChannel = 1,  // 1..16
  Omni = 2,         // 0/1
  SysInfo = 3,      // 0/1 (SYS INFO AVAIL)
  ProtectInt = 4,   // 0/1
  ProtectCrt = 5,   // 0/1
  Volume = 6,       // 0..127
  Count = 7
};

// Settings (§4.4) that live in state. Engine/Dac/HostRate/ClassicVelocity map
// onto vdx::Settings; the others are UI settings the core stores for the UI.
enum class SettingId : int32_t {
  Engine = 0,          // 0 Mk I, 1 Modern
  Dac = 1,             // 0/1
  HostRate = 2,        // 0/1
  ClassicVelocity = 3, // 0/1
  KeyboardPlay = 4,    // 0/1
  Zoom = 5,            // 0 = fit (never set), else 50..200 in steps of 10
  OpOrder = 6,         // 0 OP1->OP6, 1 OP6->OP1
  Count = 7
};

enum BankId : int32_t { kBankInt = 0, kBankCrt = 1 };

enum class ExportKind : int32_t { Internal = 0, Cartridge = 1, Voice = 2 };

enum class CmdType : uint16_t {
  Nop = 0,
  // Performance (computer keyboard, on-screen wheels). src = Keyboard/Programmer.
  NoteOn,          // a = note, b = velocity 1..127
  NoteOff,         // a = note
  Controller,      // a = CC number, b = value (on-screen wheel, keyboard sustain/mod)
  PitchBend,       // a = 0..16383
  MidiBytes,       // payload: a raw MIDI byte stream (web MIDI input), parsed like midi()
  ReleaseAll,      // release every note started with src = Keyboard, centre pitch bend
  // Front panel. src = Panel.
  Button,          // a = 1..42 (ButtonId), b = 1 pressed / 0 released
  DataEntry,       // f = slider position 0..1
  // Parameters.
  SetVoiceParam,   // a = 0..155, b = value
  SetFunctionParam,// a = 64..77, b = value
  SetGlobal,       // a = Global, b = value
  SetSetting,      // a = SettingId, b = value
  // Programmer operator / voice menus (R-U2, R-U3).
  WriteOperator,   // a = op 1..6, data[0..20] = the operator's 21 bytes
  SwapOperators,   // a = op, b = op
  EgCopy,          // a = from op, b = to op
  InitVoice,       // as FUNCTION 10 without confirmation
  Randomise,       // a = seed
  ToggleCompare,   // as EDIT/COMPARE
  // Voices and banks.
  SelectVoice,     // a = bank, b = slot 1..32 (bank manager double-click / edit-buffer drop)
  ProgramChange,   // a = 0..127 (host program; same as MIDI program change)
  CopyVoice,       // a = from bank, b = from slot, c = to bank, data[0] = to slot
  LoadVoice,       // payload = 155 unpacked bytes; b = 1 if checksum mismatch (file-loaded voice)
  LoadBank,        // a = target bank, b = 1 if checksum mismatch; payload = 4096 packed + name (NUL-terminated, <= 64)
  FileRejected,    // show FORMAT CONFLICT!
  NewCartridge,
  EjectCartridge,
  Export,          // a = ExportKind -> ExportData event
  ClearCartridgeChanged, // after a successful cartridge export
  SendBank,        // bank manager "Send bank (MIDI)"
  RequestDump,     // bank manager "Request dump (MIDI)"
  // Lifecycle.
  StartSplash,     // F-1: show the splash, then PLAY
  RequestSnapshot, // R-S5: emit the complete state, then SnapshotEnd
  SelectOperator,  // a = op 1..6 (Alt+1..6, R-U4): selected operator, no mode change
  Count
};

// Commands are fixed-size PODs (one producer thread, lock-free queue).
struct Command {
  CmdType type = CmdType::Nop;
  Source src = Source::None;
  uint8_t pad = 0;
  int32_t a = 0, b = 0, c = 0;
  float f = 0.f;
  uint32_t slot = 0xFFFFFFFFu;  // payload slot (internal)
  uint32_t len = 0;             // payload length (internal)
  uint8_t data[36] = {};        // small inline payload
};

enum class EvType : uint16_t {
  None = 0,
  VoiceParam,      // a = 0..155, b = value (effective), src; text = value string (R-P8)
  FunctionParam,   // a = 64..77, b = value, src; text = value string
  GlobalValue,     // a = Global, b = value, src; text = value string
  SettingValue,    // a = SettingId, b = value
  Lcd,             // text[0..15] line 1, text[16..31] line 2; a = cursor row, b = cursor col, c = cursor on, d = lit
  Led,             // a = 1..32 (0 = blank), b = decimal point
  Lamps,           // a = numbered buttons (bit i-1), b = mode buttons (bit k-33 for ButtonId k), c = blink mask (same layout as b)
  PanelState,      // a = PanelMode, b = page, c = selected op 1..6, d = flags (PanelFlags)
  DataEntryPos,    // f = thumb position 0..1
  Wheels,          // a = pitch bend 0..16383, b = mod wheel 0..127 (follow incoming MIDI)
  VoiceName,       // a = bank, b = slot 1..32; text = 10-char display name
  Cartridge,       // a = present, b = changed; text = name
  Memory,          // a = selected memory, b = voice source bank, c = voice source slot, d = edited
  OpFrequency,     // a = op 1..6; text = ratio or fixed frequency as on the LCD
  LfoHz,           // text = LFO frequency "x.xx Hz"
  Meter,           // f = unused; data in text: 6 x int16 little endian, dBFS x 100 (>= -6000)
  Notice,          // a = NoticeCode, b = bank / detail; text = detail
  ExportData,      // a = ExportKind; payload = file bytes; text = sanitised file name
  StateDirty,      // saved state changed (shell persistence)
  SnapshotEnd,     // end of a RequestSnapshot burst
  ParamText,       // display string of an unchanged parameter whose string depends on another
                   // (R-P8): a = voice index 0..155 or function 64..77, b = value, c = 0 voice / 1 function; text
  Count
};

enum PanelMode : int32_t { kModePlay = 0, kModeEdit = 1, kModeEditName = 2, kModeFunction = 3, kModeSplash = 4 };

enum PanelFlags : int32_t {
  kFlagComparing = 1 << 0,
  kFlagEdited = 1 << 1,
  kFlagDialog = 1 << 2,
  kFlagMessage = 1 << 3,
  kFlagTransposeArmed = 1 << 4,
};

enum NoticeCode : int32_t {
  kNoticeProtected = 1,     // b = bank (a write was refused, bank manager notice)
  kNoticeDumpReceived = 2,  // b = 0 voice, 1 bank
  kNoticeCompleted = 3,     // an action completed (send bank)
};

struct Event {
  EvType type = EvType::None;
  Source src = Source::None;
  uint8_t pad = 0;
  int32_t a = 0, b = 0, c = 0, d = 0;
  float f = 0.f;
  uint32_t slot = 0xFFFFFFFFu;  // payload slot (internal)
  uint32_t len = 0;             // payload length
  char text[64] = {};
};

struct StateBlob {
  std::vector<uint8_t> bytes;
};

class Core;

class Instrument {
 public:
  explicit Instrument(double hostSampleRate, int maxBlock);  // allocates everything here
  ~Instrument();
  Instrument(const Instrument&) = delete;
  Instrument& operator=(const Instrument&) = delete;

  void prepare(double hostSampleRate, int maxBlock);  // may allocate; not on audio thread
  bool post(const Command&);                          // one producer thread
  bool pollEvent(Event&);                             // one consumer thread
  void midi(const uint8_t* bytes, int len, int sampleOffset);  // audio thread, incl. SysEx
  void render(float* L, float* R, int n);             // audio thread, no alloc/lock/log
  int latencySamples() const;
  void saveState(StateBlob&) const;
  bool loadState(const StateBlob&);

  // ---- extensions ------------------------------------------------------
  // Producer thread: post a command with a payload copied into a preallocated slot.
  bool postPayload(Command, const uint8_t* data, int len);
  // Consumer thread: copy an event's payload and release its slot. Returns length.
  int readPayload(const Event&, uint8_t* dst, int cap);
  // Single consumer of transmitted MIDI bytes (complete messages, in order).
  int readMidiOut(uint8_t* dst, int cap);
  // Audio thread only: apply a command immediately (plugin: host automation and
  // host programs at the start of a block).
  void applyNow(const Command&);
  // Audio thread only: core access for the plugin host-parameter mirror and tests.
  Core& core();
  const Core& core() const;
  double hostRate() const;
  // Number of events dropped because the event queue was full.
  uint32_t droppedEvents() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace vdx
