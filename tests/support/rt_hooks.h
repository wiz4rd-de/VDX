// Real-time safety probes for AC-E15.2: counts heap allocations (operator
// new everywhere; malloc on Linux and macOS), mutex locks (POSIX) and writes
// to stdout/stderr between begin() and end().
#pragma once

namespace vdxtest::rt {

struct Counters {
  long allocs = 0;
  long locks = 0;
  long logBytes = 0;
};

void begin();
Counters end();
// Which probes this platform supports (for reporting).
const char* coverage();

}  // namespace vdxtest::rt
