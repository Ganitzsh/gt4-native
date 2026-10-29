#pragma once
// runtime/log.h for eeref: messages go to stderr; fatal ends the replay.
namespace gt4::rt {

void log_info(const char *fmt, ...);
[[noreturn]] void fatal(const char *fmt, ...);

}  // namespace gt4::rt
