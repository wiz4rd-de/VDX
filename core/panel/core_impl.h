// vdx::Core::Impl — all synth, data and front-panel state (SPEC §9.4), driven
// on the audio thread only. Member functions are spread over:
//   core/core.cpp           Core API, command dispatch, first-run state, state blob
//   core/panel/panel.cpp    front-panel state machine (F-2 … F-11, F-14, R-P*)
//   core/panel/lcd.cpp      LCD text (§5.3), LED and lamps (§5.4)
//   core/panel/midi.cpp     MIDI receive (R-M1), SysEx (F-13), transmit (R-M3)
//   core/panel/refresh.cpp  engine sync, event emission, timers, snapshot
// Real-time rules (R-E15): fixed arrays only, no allocation, locks or exceptions.
#pragma once

#include <cstdint>

#include "core.h"
#include "fm/engine.h"
#include "sysex/state.h"
#include "sysex/voice.h"
#include "vdx/params.h"
#include "vdx/vdx.h"

namespace vdx {

enum class Dialog : uint8_t { None = 0, Store, EgCopy, ProtInt, ProtCrt, Confirm };
enum class Msg : uint8_t { None = 0, Protected, FormatConflict, ChecksumError, MidiReceived, Completed, StateReset, NotReady };

// The value a front-panel page edits (F-2 YES/NO and data entry).
struct PageValue {
  enum Kind : uint8_t { None = 0, Voice, Function, FnBit, GlobalV };
  enum Style : uint8_t { Num = 0, OnOff, Choice };
  Kind kind = None;
  Style style = Num;
  int index = 0;  // voice index 0..155, function number 64..77, or Global
  int bit = 0;    // FnBit: assign bit 0..2
  int min = 0, max = 0;
};

struct LcdState {
  char l1[17];
  char l2[17];
  int row, col;
  bool cursor;
};

struct Core::Impl {
  Impl(EventSink& e, MidiOutSink& m);

  // ---- wiring --------------------------------------------------------------
  EventSink* ev;
  MidiOutSink* out;
  fm::Engine engine;
  Settings settings;
  double rate = kInternalRate;

  // ---- saved state (§4.8) and the operator mask -----------------------------
  state::Persist ps;
  state::Persist shadowPs;   // last refreshed copy (StateDirty)
  state::Persist scratchPs;  // deserialize staging
  uint8_t opMask = 0x3F;

  // ---- front panel -----------------------------------------------------------
  int mode = kModePlay;      // kModePlay / kModeEdit / kModeEditName / kModeFunction
  int editPage = 7;          // 7..32
  int fnPage = 1;            // 1..32 (never 12/13)
  int selOp = 1;             // 1..6
  int egStage = 0;           // 0..3 (pages 21/22)
  int pegStage = 0;          // 0..3 (pages 29/30)
  int scaleSide = 0;         // 0 L, 1 R (pages 24/25)
  int b17Sub = 0;            // 0 mode, 1 sync
  int b8Sub = 0;             // 0 CH, 1 SYS INFO, 2 TRANSMIT
  int nameCursor = 0;        // 0..9
  bool comparing = false;
  bool armed = false;        // key transpose (F-5)
  Dialog dialog = Dialog::None;
  Msg msg = Msg::None;
  int64_t msgUntil = 0;
  bool splash = false;
  bool splashDone = false;
  int64_t splashUntil = 0;
  bool stateResetPending = false;
  int held = 0;              // kBtnYes / kBtnNo while auto-repeating
  int64_t nextRepeat = 0;

  // ---- performance -------------------------------------------------------------
  uint8_t kbNotes[128] = {};   // notes started with Source::Keyboard
  uint8_t swallowed[128] = {}; // note-ons eaten by key transpose (their note-off too)
  bool kbSustain = false;
  int bend = 8192;
  int modWheel = 0;
  // Probe: note-ons handed to the engine (tests observe routing without audio).
  uint32_t noteOnsToEngine = 0;
  uint32_t noteOffsToEngine = 0;
  int lastEngineNote = -1;
  int lastEngineVelocity = 0;
  int breathV = 0, footV = 0, aftertouchV = 0;
  bool sustainOn = false, portaOn = true;
  uint32_t allSoundOffs = 0, allNotesOffs = 0;

  // ---- clock (engine-rate samples) -------------------------------------------
  int64_t now = 0;
  int64_t tSplash = 0, tMsg = 0, tRepeatDelay = 0, tRepeatPeriod = 0, tThrottle = 0, tMeter = 0;
  int64_t nextMeter = 0;

  // ---- transmit (F-13.6): ids 0..155 voice, 156..169 function 64..77 ----------
  static constexpr int kTxIds = 170;
  int64_t lastTx[kTxIds] = {};
  bool txEver[kTxIds] = {};
  bool txPending[kTxIds] = {};

  // ---- per-change context --------------------------------------------------------
  Source src = Source::None;  // source reported with parameter events
  bool tx = false;            // changes are front-panel/programmer edits (transmit)

  // ---- emission shadows (diff-based events) ----------------------------------------
  uint8_t lastEff[156] = {};
  uint8_t lastFn[14] = {};
  uint8_t lastGlobals[7] = {};
  uint8_t lastSettings[7] = {};
  char lastOpFreq[6][12] = {};
  int lastLfoSpeed = -1;
  char lastNames[2][32][10] = {};
  bool lastNamesValid[2] = {false, false};
  int lastCrtPresent = -1, lastCrtChanged = -1;
  char lastCrtName[65] = {};
  int lastMem[4] = {-1, -1, -1, -1};
  LcdState lastLcd = {};
  bool lastLcdValid = false;
  int lastLed[2] = {-1, -1};
  int lastLamps[3] = {-1, -1, -1};
  int lastPanel[4] = {-1, -1, -1, -1};
  int lastDeKey = -1, lastDeVal = -1;
  bool lastMeterSilent = false;
  // engine shadows
  bool engineValid = false;
  uint8_t engVoice[156] = {};
  fm::FunctionState engFn;
  int engTune = -1;
  int engMode[3] = {-1, -1, -1};

  uint8_t msgBuf[4104] = {};  // outgoing dumps / export payloads

  uint32_t rngState = 1;
  bool dirtyForInstrument = false;
  Core::LoadResult lastLoad = Core::LoadResult::Empty;

  // ===== core.cpp ====================================================================
  void firstRun();
  void resetPanel();  // F-1.5 defaults (not the splash)
  void setTimers();
  void onCommand(const Command& c, const uint8_t* payload, int len);
  void applySettingsStruct();

  // ===== panel.cpp ===================================================================
  // inputs
  void buttonDown(int id, bool allowHold);
  void buttonUp(int id);
  void dataEntry(float pos);
  bool yesNo(bool yes);  // returns true when the step may auto-repeat
  void repeatStep();
  // pages
  bool pageValue(PageValue& pv) const;
  int getPageValue(const PageValue& pv) const;
  void setPageValue(const PageValue& pv, int v);
  void showEditPage(int page);
  void showParamPage(int index);
  int fnPageFor(int n, int oldV, int newV) const;
  // edits
  void writeVoice(int index, int value);  // clamps; EDITED for 0..154
  void writeFn(int n, int value);
  void setGlobal(int g, int value);
  void setSetting(int s, int value);
  void programmerVoice(int index, int value);
  void programmerFn(int n, int value);
  void externalVoice(int index, int value);
  void externalFn(int n, int value);
  void panelVoice(int index, int value);
  void touchProgrammer();
  void cancelDialog();
  void endNaming();
  void endCompare();
  void leaveEdit();
  // voices and banks
  uint8_t* bankData(int bank);
  bool bankProtected(int bank) const;
  void selectVoice(int bank, int slot, bool setSelMem);
  void loadEditBuffer(const sysex::Voice& v, bool captureRecall);
  void programChange(int program);
  void doStore(int slot);
  void doEgCopy(int fromOp, int toOp);
  void executeConfirm();
  void writeOperator(int op, const uint8_t* bytes21);
  void swapOperators(int a, int b);
  void initVoiceCmd();
  void randomise(int seed);
  void toggleCompare();
  void copyVoice(int fromBank, int fromSlot, int toBank, int toSlot);
  void loadVoiceFile(const uint8_t* data, int len, bool badSum);
  void loadBankFile(int bank, bool badSum, const uint8_t* data, int len);
  void newCartridge();
  void ejectCartridge();
  void exportData(int kind);
  void sendBank();
  void startSplash();
  void endSplash();
  void showMessage(Msg m);
  void notice(int code, int b);
  uint32_t nextRandom();

  // ===== lcd.cpp =====================================================================
  void renderLcd(LcdState& o) const;
  void renderBase(LcdState& o) const;
  void playLines(LcdState& o, bool compare) const;
  void editL1(char* l1) const;
  void editL2(char* l2) const;
  void fnLines(LcdState& o) const;
  void lamps(int& num, int& modeMask, int& blink) const;
  bool showsOp() const;

  // ===== midi.cpp ====================================================================
  void midiMessage(const uint8_t* m, int len);
  void noteOn(int note, int vel, Source s);
  void noteOff(int note, Source s);
  void controller(int cc, int v, bool fromMidi, Source s);
  void releaseAll();
  void sysexReceived(const uint8_t* m, int len);
  void queueTx(int id);
  void sendParam(int id);
  void flushTx();
  void sendMidi(const uint8_t* msg, int len);
  void pushWheels();

  // ===== refresh.cpp =================================================================
  void refresh();
  void syncEngine();
  void emitSnapshot();
  void processTimers();
  void meters();
  void editEffective(uint8_t* eff156) const;
  void push(const Event& e);
  void syncShadows();  // adopt the current state as emitted (no events)
};

}  // namespace vdx
