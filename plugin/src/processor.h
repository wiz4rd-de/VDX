// VDX AudioProcessor (SPEC §4.9, §9.3, §9.4, R-S4, F-15): one vdx::Instrument,
// the 174 host parameters, 32 host programs, state, the UI bridge and — in the
// standalone — persistence and the idle pump.
//
// Threads
//  * Audio thread (processBlock; in the standalone also the idle pump, never
//    concurrently): applies host parameter changes and host programs with
//    Instrument::applyNow at the start of each block, feeds MIDI, renders,
//    drains transmitted MIDI.
//  * Message thread: the only producer of Instrument::post/postPayload (UI
//    commands, shell actions) and, through one 60 Hz timer, the only consumer
//    of Instrument::pollEvent: core changes are reported to the host (R-S4),
//    events go to the editor, exports and persistence are handled.
#pragma once

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "bridge.h"
#include "host_params.h"
#include "vdx/vdx.h"

namespace vdx::shell {
class Persistence;
}

namespace vdx::plugin {

class VdxEditor;

class VdxProcessor final : public juce::AudioProcessor, private juce::Timer {
 public:
  VdxProcessor();
  ~VdxProcessor() override;

  // ---- AudioProcessor ------------------------------------------------------
  const juce::String getName() const override { return "VDX"; }
  void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
  void releaseResources() override {}
  bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
  void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
  using juce::AudioProcessor::processBlock;
  // Bypass mutes the output; the instrument keeps running (notes, UI, timers).
  void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
  using juce::AudioProcessor::processBlockBypassed;

  bool hasEditor() const override { return true; }
  double getTailLengthSeconds() const override { return 0.0; }
  bool acceptsMidi() const override { return true; }
  bool producesMidi() const override { return true; }
  bool isMidiEffect() const override { return false; }

  int getNumPrograms() override { return 32; }
  int getCurrentProgram() override;
  void setCurrentProgram(int index) override;
  const juce::String getProgramName(int index) override;
  void changeProgramName(int, const juce::String&) override {}

  void getStateInformation(juce::MemoryBlock& destData) override;
  void setStateInformation(const void* data, int sizeInBytes) override;

  // ---- message thread --------------------------------------------------------
  // The timer body: drains core events (host reports, UI events, exports,
  // persistence). Tests call it directly.
  void drainCoreEvents();
  // A UI command (docs/PROTOCOL.md). Returns the JSON event array to forward
  // to the UI ("" for none).
  std::string handleUiCommand(const std::string& json);
  // Editor lifecycle (VdxEditor).
  void editorOpened(VdxEditor* editor);
  void editorClosed(VdxEditor* editor);
  void pageNavigating();  // the web view is (re)loading: hold UI events until it talks again
  // Zoom setting as last reported by the core (0 = fit).
  int zoomSetting() const { return zoom_; }

  vdx::Instrument& instrument() { return *instrument_; }
  bool isStandaloneBuild() const { return standalone_; }

  // ---- standalone shell ------------------------------------------------------
  struct ShellHooks {
    std::function<void()> openDevices;            // show the audio/MIDI device selector
    std::function<bool()> midiOutputAvailable;    // a MIDI output device is selected
    std::function<bool()> audioRunning;           // an audio device is running
  };
  void setShellHooks(ShellHooks hooks) { hooks_ = std::move(hooks); }
  void sendEnvToUi();             // env event (device changes)
  void saveStandaloneStateNow();  // F-15: once more on quit

  // ---- tests -------------------------------------------------------------------
  // Called for every core event drained by drainCoreEvents().
  std::function<void(const vdx::Event&)> eventObserver;
  // Number of setValueNotifyingHost calls made for core changes (R-S4).
  int hostReportsMade() const { return hostReports_; }

 private:
  juce::AudioProcessorEditor* createEditor() override;
  void timerCallback() override { drainCoreEvents(); }

  class IdlePump;

  // Audio thread.
  void beginBlock();
  void applyHostChanges();
  void renderSilent(int numSamples);
  void collectMidiOut(juce::MidiBuffer* out);
  int hostValue(int hostIndex) const;

  // Message thread.
  void reportToHost(int hostIndex, int value);
  void sendToUi(const std::string& jsonArray);
  void sendNotice(const std::string& code, const std::string& text);
  void exportFile(vdx::ExportKind kind, const std::string& name, std::vector<uint8_t> bytes);
  void pickFile();
  void maybeSaveStandaloneState(bool force);
  void postCommand(vdx::CmdType type, vdx::Source src, int a = 0, int b = 0);
  std::string envJson() const;

  const bool standalone_;
  std::unique_ptr<vdx::Instrument> instrument_;
  std::unique_ptr<vdx::bridge::Bridge> bridge_;

  // Host parameters (host order) and their audio-thread mirror.
  std::vector<juce::RangedAudioParameter*> params_;
  std::vector<juce::AudioParameterInt*> ints_;    // per host index, or nullptr
  std::vector<juce::AudioParameterBool*> bools_;  // per host index, or nullptr
  std::array<int, kHostParamCount> lastSeen_{};                 // audio thread
  std::array<std::atomic<int>, kHostParamCount> echo_;          // value reported by the plugin itself
  std::array<int, kHostParamCount> pendingReport_{};            // message thread (coalescing)
  std::array<bool, kHostParamCount> hasPendingReport_{};
  int hostReports_ = 0;
  std::atomic<int> pendingProgram_{-1};

  // Program list cache (message thread writes, any thread reads).
  juce::CriticalSection namesLock_;
  std::array<juce::String, 32> programNames_;
  std::atomic<int> srcBank_{0}, srcSlot_{1};

  // Editor / UI (message thread).
  VdxEditor* editor_ = nullptr;
  bool uiAlive_ = false;
  bool splashStarted_ = false;
  int zoom_ = 0;
  std::string uiBatch_;
  std::unique_ptr<juce::FileChooser> chooser_;
  bool chooserActive_ = false;
  uint32_t droppedSeen_ = 0;
  bool programReported_ = false;

  // Audio thread scratch.
  std::vector<float> scratch_;
  std::vector<uint8_t> midiOut_;

  // Plugin: re-apply the "keyboard play off" default after a first-run load.
  std::atomic<bool> keyboardDefaultPending_{false};

  // Standalone.
  ShellHooks hooks_;
  std::unique_ptr<vdx::shell::Persistence> persistence_;
  std::string lastSaveError_;
  std::unique_ptr<IdlePump> pump_;
  std::atomic<bool> busy_{false};             // processBlock / idle pump exclusion
  std::atomic<uint32_t> lastProcessMs_{0};

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VdxProcessor)
};

}  // namespace vdx::plugin
