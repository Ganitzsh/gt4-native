#include "audio.h"
#include "link.h"
#include "log.h"
#include "runtime.h"

namespace gt4::rt {

void iop_init();
void gs_init();

void services_init() {
    if (*g_config.link_name) link_open(g_config.link_name);
    if (*g_config.capture_path) link_capture(g_config.capture_path);
    audio_init();
    iop_init();
    gs_init();
}

}  // namespace gt4::rt
