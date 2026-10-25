#pragma once
// Runtime log: stderr and build/game/run.log (or GT4_LOG). printf-style; one line per call.
#include <cstdarg>

namespace gt4::rt {

void log_open(const char *path);
void log_info(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void log_warn(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void log_error(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
[[noreturn]] void fatal(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

}  // namespace gt4::rt
