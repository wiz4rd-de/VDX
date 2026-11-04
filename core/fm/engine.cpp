// Temporary silent engine used while the real engine is written (replaced in S1).
#include "fm/engine.h"

#include <cstring>

namespace vdx::fm {

struct Engine::Impl {
  double rate = kInternalRate;
  EngineDebug debug;
};

Engine::Engine() : impl_(new Impl) {}
Engine::~Engine() { delete impl_; }
void Engine::prepare(double r) { impl_->rate = r; }
double Engine::rate() const { return impl_->rate; }
void Engine::setVoice(const uint8_t*) {}
void Engine::setFunction(const FunctionState&) {}
void Engine::setMasterTune(int) {}
void Engine::setMode(EngineMode, bool, bool) {}
void Engine::noteOn(int, int) {}
void Engine::noteOff(int) {}
void Engine::sustain(bool) {}
void Engine::portamentoSwitch(bool) {}
void Engine::pitchBend(int) {}
void Engine::modWheel(int) {}
void Engine::breath(int) {}
void Engine::foot(int) {}
void Engine::aftertouch(int) {}
void Engine::allNotesOff() {}
void Engine::allSoundOff() {}
void Engine::render(int32_t* tap, float* out) {
  std::memset(tap, 0, sizeof(int32_t) * kBlock);
  std::memset(out, 0, sizeof(float) * kBlock);
}
void Engine::takeOperatorPeaks(int32_t p[6]) { for (int i = 0; i < 6; ++i) p[i] = 0; }
int Engine::activeVoices() const { return 0; }
EngineDebug& Engine::debug() { return impl_->debug; }
Engine::Impl& Engine::impl() { return *impl_; }

}  // namespace vdx::fm
