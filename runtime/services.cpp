#include "log.h"
#include "runtime.h"

namespace gt4::rt {

void iop_init();

void services_init() { iop_init(); }

}  // namespace gt4::rt
