// VDX editor (SPEC §5.1, §5.10, §14 UI bridge): a WebBrowserComponent showing
// the embedded UI (ui/dist) with native integration — the native function
// `vdxCommand(json)` and the batched event `vdxEvents` (docs/PROTOCOL.md).
// Without a usable system web view it shows a plain label; the engine, MIDI,
// automation and state keep working.
#pragma once

#include <memory>
#include <string>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>

namespace vdx::plugin {

class VdxProcessor;

class VdxEditor final : public juce::AudioProcessorEditor {
 public:
  static constexpr int kBaseWidth = 1280;  // §5.1 canvas
  static constexpr int kBaseHeight = 1000;
  static constexpr int kMinWidth = 640, kMinHeight = 500;
  static constexpr int kMaxWidth = 2560, kMaxHeight = 2000;

  explicit VdxEditor(VdxProcessor& processor);
  ~VdxEditor() override;

  void paint(juce::Graphics&) override;
  void resized() override;

  // Message thread: a JSON array of UI events.
  void sendEvents(const std::string& jsonArray);
  // The zoom setting changed (50..200 %): the window takes zoom × 1280 × 1000.
  void zoomSettingChanged(int zoomPercent);

  // §5.1: zoom size, or on first open 100 % if that fits the screen, else the
  // largest 10 % step that fits.
  static juce::Point<int> sizeForZoom(int zoomPercent);

  bool hasWebView() const { return browser_ != nullptr; }
  // Gives the web view keyboard focus (standalone window opening).
  void focusWebView();

 private:
  class Browser;
  VdxProcessor& processor_;
  std::unique_ptr<Browser> browser_;
  std::unique_ptr<juce::Label> fallback_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VdxEditor)
};

}  // namespace vdx::plugin
