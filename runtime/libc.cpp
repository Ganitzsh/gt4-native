// The C library functions of the image (newlib, partly hand-written MIPS assembly) replaced by the
// host's: same behaviour, and the assembly ones have no source to compile.
#include <cstddef>
#include <cstring>

extern "C" {
void *gt4_replace_memcpy(void *d, const void *s, size_t n) { return std::memcpy(d, s, n); }
void *gt4_replace_memmove(void *d, const void *s, size_t n) { return std::memmove(d, s, n); }
void *gt4_replace_memset(void *d, int c, size_t n) { return std::memset(d, c, n); }
void *gt4_replace_memchr(const void *s, int c, size_t n) { return const_cast<void *>(std::memchr(s, c, n)); }
char *gt4_replace_strcpy(char *d, const char *s) { return std::strcpy(d, s); }
char *gt4_replace_strncpy(char *d, const char *s, size_t n) { return std::strncpy(d, s, n); }
char *gt4_replace_strcat(char *d, const char *s) { return std::strcat(d, s); }
char *gt4_replace_strncat(char *d, const char *s, size_t n) { return std::strncat(d, s, n); }
char *gt4_replace_strchr(const char *s, int c) { return const_cast<char *>(std::strchr(s, c)); }
int gt4_replace_strcmp(const char *a, const char *b) { return std::strcmp(a, b); }
size_t gt4_replace_strlen(const char *s) { return std::strlen(s); }
// libscf's BCD helpers (clock values)
int gt4_replace_frombcd(int x) { return (x >> 4) * 10 + (x & 0xF); }
int gt4_replace_tobcd(int x) { return ((x / 10) << 4) | (x % 10); }
// 0057F188: newlib's hand-written memcmp (unnamed in the decomp's tables)
int gt4_replace_func_0057F188(const void *a, const void *b, size_t n) { return std::memcmp(a, b, n); }
}
