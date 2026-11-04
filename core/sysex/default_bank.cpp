#include "sysex/default_bank.h"

namespace vdx::sysex {

namespace {
const uint8_t kDefaultBank[4104] = {
#include "sysex/default_bank.inc"
};
}  // namespace

const uint8_t* defaultBankSyx() { return kDefaultBank; }
const uint8_t* defaultBankPacked() { return kDefaultBank + 6; }

}  // namespace vdx::sysex
