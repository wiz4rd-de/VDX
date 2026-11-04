#include "editor.h"

#if JUCE_LINUX || JUCE_BSD
#include <dlfcn.h>
#endif

#include "processor.h"
#include "ui_resources.h"

namespace vdx::plugin {

namespace {

const juce::Colour kBody(0xff2b211b);    // --body
const juce::Colour kLegend(0xffe9e5cd);  // --legend

using Options = juce::WebBrowserComponent::Options;

#if JUCE_WEB_BROWSER_RESOURCE_PROVIDER_AVAILABLE
std::optional<juce::WebBrowserComponent::Resource> provide(const juce::String& path) {
  const EmbeddedFile* f = findEmbeddedFile(path.toStdString());
  if (f == nullptr) return std::nullopt;
  juce::WebBrowserComponent::Resource r;
  const auto* bytes = reinterpret_cast<const std::byte*>(f->data);
  r.data.assign(bytes, bytes + f->size);
  r.mimeType = mimeTypeFor(f->path);
  return r;
}
#endif

}  // namespace

// The web view. Only the embedded UI may load: every other navigation is
// refused, so the editor never touches the network (§3, §9.5).
class VdxEditor::Browser final : public juce::WebBrowserComponent {
 public:
  Browser(const Options& o, VdxProcessor& p) : juce::WebBrowserComponent(o), processor_(p) {}

  bool pageAboutToLoad(const juce::String& url) override {
    const bool allowed = url.startsWith(getResourceProviderRoot()) || url.startsWith("about:");
    if (allowed) processor_.pageNavigating();
    return allowed;
  }
  void newWindowAttemptingToLoad(const juce::String&) override {}
  bool pageLoadHadNetworkError(const juce::String&) override { return false; }

 private:
  VdxProcessor& processor_;
};

static Options makeOptions(VdxEditor& editor, VdxProcessor& processor) {
  juce::Component::SafePointer<VdxEditor> safe(&editor);
  Options o = Options{}
                  .withNativeIntegrationEnabled()
                  .withNativeFunction("vdxCommand",
                                      [safe, &processor](const juce::Array<juce::var>& args,
                                                         juce::WebBrowserComponent::NativeFunctionCompletion done) {
                                        if (safe != nullptr && args.size() > 0) {
                                          const std::string reply =
                                              processor.handleUiCommand(args[0].toString().toStdString());
                                          if (!reply.empty()) safe->sendEvents(reply);
                                        }
                                        done(juce::var());
                                      })
#if JUCE_WEB_BROWSER_RESOURCE_PROVIDER_AVAILABLE
                  .withResourceProvider([](const juce::String& path) { return provide(path); })
#endif
      ;
#if JUCE_WINDOWS
  // WebView2 (Edge), user data under the application data directory.
  const auto webViewData = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                               .getChildFile("VDX")
                               .getChildFile("WebView2");
  o = o.withBackend(Options::Backend::webview2)
          .withWinWebView2Options(Options::WinWebView2{}
                                      .withUserDataFolder(webViewData)
                                      .withStatusBarDisabled()
                                      .withBuiltInErrorPageDisabled()
                                      .withBackgroundColour(kBody));
#elif JUCE_LINUX || JUCE_BSD
  o = o.withLinuxWkWebViewOptions(Options::LinuxWkWebView{});  // WebKitGTK
#endif
  return o;
}

// §5.10: is the system web view (WebView2 / WebKitGTK / WKWebView) usable?
static bool webViewAvailable(const Options& o) {
  if (!juce::WebBrowserComponent::areOptionsSupported(o)) return false;
#if JUCE_LINUX || JUCE_BSD
  // JUCE loads WebKitGTK at runtime; without it the view would stay blank.
  for (const char* lib : {"libwebkit2gtk-4.1.so.0", "libwebkit2gtk-4.0.so.37"})
    if (void* h = dlopen(lib, RTLD_LAZY | RTLD_LOCAL)) {
      dlclose(h);
      return true;
    }
  return false;
#else
  return true;
#endif
}

juce::Point<int> VdxEditor::sizeForZoom(int zoom) {
  if (zoom >= 50 && zoom <= 200) return {kBaseWidth * zoom / 100, kBaseHeight * zoom / 100};
  // Fit: 100 % if it fits the screen, else the largest 10 % step that fits.
  juce::Rectangle<float> area(0.f, 0.f, float(kBaseWidth), float(kBaseHeight) + 64.f);
  if (const auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()) area = d->userBounds;
  const float availW = area.getWidth();
  const float availH = area.getHeight() - 40.f;  // window title bar
  for (int z = 100; z >= 50; z -= 10) {
    const int w = kBaseWidth * z / 100, h = kBaseHeight * z / 100;
    if (float(w) <= availW && float(h) <= availH) return {w, h};
  }
  return {kMinWidth, kMinHeight};
}

VdxEditor::VdxEditor(VdxProcessor& p) : juce::AudioProcessorEditor(p), processor_(p) {
  setResizable(true, false);
  setResizeLimits(kMinWidth, kMinHeight, kMaxWidth, kMaxHeight);
  if (auto* c = getConstrainer()) c->setFixedAspectRatio(double(kBaseWidth) / double(kBaseHeight));
  const auto size = sizeForZoom(processor_.zoomSetting());
  setSize(size.x, size.y);

  processor_.editorOpened(this);
  const Options options = makeOptions(*this, processor_);
  if (webViewAvailable(options)) {
    browser_ = std::make_unique<Browser>(options, processor_);
    addAndMakeVisible(*browser_);
    browser_->goToURL(juce::WebBrowserComponent::getResourceProviderRoot());
  } else {
    // §5.10
    fallback_ = std::make_unique<juce::Label>(juce::String(),
                                              "VDX needs Microsoft Edge WebView2 / WebKitGTK to show its panel.");
    fallback_->setJustificationType(juce::Justification::centred);
    fallback_->setColour(juce::Label::textColourId, kLegend);
    fallback_->setFont(juce::FontOptions(18.0f));
    addAndMakeVisible(*fallback_);
  }
  resized();
}

VdxEditor::~VdxEditor() {
  browser_.reset();
  processor_.editorClosed(this);
}

void VdxEditor::paint(juce::Graphics& g) { g.fillAll(kBody); }

void VdxEditor::resized() {
  if (browser_) browser_->setBounds(getLocalBounds());
  if (fallback_) fallback_->setBounds(getLocalBounds().reduced(16));
}

void VdxEditor::sendEvents(const std::string& jsonArray) {
  if (browser_ && !jsonArray.empty())
    browser_->emitEventIfBrowserIsVisible("vdxEvents", juce::var(juce::String::fromUTF8(jsonArray.c_str())));
}

void VdxEditor::focusWebView() {
  if (browser_ && browser_->isShowing()) browser_->grabKeyboardFocus();
}

void VdxEditor::zoomSettingChanged(int zoom) {
  if (zoom < 50 || zoom > 200) return;
  const auto size = sizeForZoom(zoom);
  setSize(size.x, size.y);
}

}  // namespace vdx::plugin
