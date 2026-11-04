// Temporary minimal Core (replaced by the panel/data slice S2/S3).
#include "core.h"

#include <cstring>

namespace vdx {

struct Core::Impl {
  EventSink* ev;
  MidiOutSink* out;
  fm::Engine engine;
  Settings settings;
  double rate = kInternalRate;
  bool dirty = false;
};

Core::Core(EventSink& e, MidiOutSink& m) : impl_(new Impl) { impl_->ev = &e; impl_->out = &m; }
Core::~Core() { delete impl_; }
void Core::prepare(double r) { impl_->rate = r; impl_->engine.prepare(r); }
double Core::engineRate() const { return impl_->rate; }
void Core::command(const Command&, const uint8_t*, int) {}
void Core::midiMessage(const uint8_t*, int) {}
void Core::renderBlock(int32_t* tap, float* out) { impl_->engine.render(tap, out); }
const Settings& Core::settings() const { return impl_->settings; }
int Core::masterVolume() const { return 127; }
size_t Core::serialize(uint8_t*, size_t) const { return 0; }
Core::LoadResult Core::deserialize(const uint8_t*, size_t len) { return len ? LoadResult::Reset : LoadResult::Empty; }
Core::LoadResult Core::validate(const uint8_t*, size_t len) { return len ? LoadResult::Reset : LoadResult::Empty; }
bool Core::takeStateDirty() { bool d = impl_->dirty; impl_->dirty = false; return d; }
fm::Engine& Core::engine() { return impl_->engine; }
Core::Impl& Core::impl() { return *impl_; }
const Core::Impl& Core::impl() const { return *impl_; }

}  // namespace vdx
