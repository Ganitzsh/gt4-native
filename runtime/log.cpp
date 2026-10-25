#include "log.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <mutex>

namespace gt4::rt {
namespace {

FILE *g_file = nullptr;
std::mutex g_mutex;

void emit(const char *level, const char *fmt, va_list ap) {
    char line[2048];
    vsnprintf(line, sizeof line, fmt, ap);
    std::lock_guard<std::mutex> lock(g_mutex);
    const double t = GetTickCount64() / 1000.0;
    fprintf(stderr, "[%9.3f] %s %s\n", t, level, line);
    if (g_file) {
        fprintf(g_file, "[%9.3f] %s %s\n", t, level, line);
        fflush(g_file);
    }
}

}  // namespace

void log_open(const char *path) {
    if (path && *path) g_file = fopen(path, "w");
}

#define GT4_LOG_FN(name, level)    \
    void name(const char *fmt, ...) { \
        va_list ap;                   \
        va_start(ap, fmt);            \
        emit(level, fmt, ap);         \
        va_end(ap);                   \
    }
GT4_LOG_FN(log_info, "info ")
GT4_LOG_FN(log_warn, "WARN ")
GT4_LOG_FN(log_error, "ERROR")

void fatal(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    emit("FATAL", fmt, ap);
    va_end(ap);
    if (g_file) fclose(g_file);
    fflush(stderr);
    ExitProcess(3);
}

}  // namespace gt4::rt
