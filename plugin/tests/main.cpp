// Plugin test runner: JUCE needs its message manager for timers and
// parameter notifications; the tests drive the processor headlessly.
#include <catch_amalgamated.hpp>
#include <juce_events/juce_events.h>

int main(int argc, char* argv[]) {
  juce::ScopedJuceInitialiser_GUI juce;
  return Catch::Session().run(argc, argv);
}
