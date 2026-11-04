// The VDX standalone app (SPEC §1, §4.4, F-1, F-15): JUCE's StandalonePluginHolder
// runs the processor on the audio/MIDI devices, a DocumentWindow hosts the
// editor. Built only into the Standalone target (JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP).
//
//  * Devices: the system default output and all MIDI inputs on first run; the
//    choices persist in JUCE's standalone properties file. The device
//    selector opens from Settings ("Audio/MIDI devices…"), so the window has no
//    extra Options button.
//  * State: state.bin / internal.syx through the processor's persistence
//    (F-15). JUCE's own plugin-state saving ("filterState") is never used.
#include <juce_audio_plugin_client/juce_audio_plugin_client.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

#include "editor.h"
#include "processor.h"

namespace vdx::plugin {

class StandaloneWindow final : public juce::DocumentWindow {
 public:
  explicit StandaloneWindow(juce::AudioProcessor& processor)
      : juce::DocumentWindow("VDX", juce::Colour(0xff2b211b), juce::DocumentWindow::allButtons),
        processor_(processor) {
    setUsingNativeTitleBar(true);
    auto* editor = processor.createEditorAndMakeActive();
    setContentOwned(editor, true);
    setResizable(true, false);
    // §5.1: fixed aspect ratio, 640 × 500 … 2560 × 2000 (native title bar: window = content).
    setResizeLimits(VdxEditor::kMinWidth, VdxEditor::kMinHeight, VdxEditor::kMaxWidth, VdxEditor::kMaxHeight);
    getConstrainer()->setFixedAspectRatio(double(VdxEditor::kBaseWidth) / double(VdxEditor::kBaseHeight));
    centreWithSize(getWidth(), getHeight());
    setVisible(true);
    // Computer keyboard play (on in the standalone) works without a first click.
    juce::Component::SafePointer<VdxEditor> safe(dynamic_cast<VdxEditor*>(editor));
    juce::MessageManager::callAsync([safe] {
      if (safe != nullptr) safe->focusWebView();
    });
  }

  ~StandaloneWindow() override {
    // The processor must forget its active editor before the editor is deleted.
    if (auto* editor = dynamic_cast<juce::AudioProcessorEditor*>(getContentComponent()))
      processor_.editorBeingDeleted(editor);
    clearContentComponent();
  }

  void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }

 private:
  juce::AudioProcessor& processor_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StandaloneWindow)
};

class StandaloneApp final : public juce::JUCEApplication, private juce::ChangeListener {
 public:
  StandaloneApp() {
    juce::PropertiesFile::Options o;
    o.applicationName = "VDX";
    o.filenameSuffix = ".settings";
    o.osxLibrarySubFolder = "Application Support";
#if JUCE_LINUX || JUCE_BSD
    o.folderName = "~/.config";
#endif
    properties_.setStorageParameters(o);
  }

  const juce::String getApplicationName() override { return "VDX"; }
  const juce::String getApplicationVersion() override { return JucePlugin_VersionString; }
  bool moreThanOneInstanceAllowed() override { return true; }  // §3: independent processes
  void anotherInstanceStarted(const juce::String&) override {}

  void initialise(const juce::String&) override {
    auto* settings = properties_.getUserSettings();
    settings->removeValue("filterState");  // state lives in state.bin only (F-15)
    const bool firstRun = settings->getXmlValue("audioSetup") == nullptr;

    holder_ = std::make_unique<juce::StandalonePluginHolder>(settings, false, juce::String(), nullptr,
                                                             juce::Array<juce::StandalonePluginHolder::PluginInOuts>(),
                                                             false);
    auto& devices = holder_->deviceManager;
    if (firstRun) {  // §4.4: all MIDI inputs on first run
      for (const auto& d : juce::MidiInput::getAvailableDevices()) devices.setMidiInputDeviceEnabled(d.identifier, true);
      holder_->saveAudioDeviceState();
      settings->saveIfNeeded();
    }
    holder_->player.setMidiOutput(devices.getDefaultMidiOutput());
    devices.addChangeListener(this);

    if (auto* p = processor()) {
      VdxProcessor::ShellHooks hooks;
      hooks.openDevices = [this] {
        if (holder_) holder_->showAudioSettingsDialog();
      };
      hooks.midiOutputAvailable = [this] { return holder_ && holder_->deviceManager.getDefaultMidiOutput() != nullptr; };
      hooks.audioRunning = [this] { return holder_ && holder_->deviceManager.getCurrentAudioDevice() != nullptr; };
      p->setShellHooks(std::move(hooks));
    }
    window_ = std::make_unique<StandaloneWindow>(*holder_->processor);
  }

  void shutdown() override {
    if (holder_) {
      holder_->deviceManager.removeChangeListener(this);
      if (auto* p = processor()) p->saveStandaloneStateNow();  // F-15: once more on quit
    }
    window_ = nullptr;  // deletes the editor before the processor
    holder_ = nullptr;  // saves the device setup
    properties_.saveIfNeeded();
  }

  void systemRequestedQuit() override { quit(); }

 private:
  VdxProcessor* processor() const {
    return holder_ ? dynamic_cast<VdxProcessor*>(holder_->processor.get()) : nullptr;
  }

  void changeListenerCallback(juce::ChangeBroadcaster*) override {
    if (!holder_) return;
    holder_->player.setMidiOutput(holder_->deviceManager.getDefaultMidiOutput());
    holder_->saveAudioDeviceState();
    properties_.saveIfNeeded();
    if (auto* p = processor()) p->sendEnvToUi();
  }

  juce::ApplicationProperties properties_;
  std::unique_ptr<juce::StandalonePluginHolder> holder_;
  std::unique_ptr<StandaloneWindow> window_;
};

}  // namespace vdx::plugin

JUCE_CREATE_APPLICATION_DEFINE(vdx::plugin::StandaloneApp)
