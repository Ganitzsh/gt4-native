/* Native side of the differential harness: runs one decomp function (linked in as func_ADDR) on
 * trials read from a file and writes what it returned and what it left in guest memory.
 *
 * 32-bit process. Guest memory is mapped at the same addresses the original uses, so raw pointers
 * in the decomp sources mean what they meant on the PS2. One fork per trial: a fault is a result.
 *
 * in : u32 base, u32 size, u32 ntrials, then per trial u32 args[4] + size bytes of memory
 * out: per trial u32 status (1 ran, 0 faulted), u32 ret_lo, u32 ret_hi, size bytes of memory
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdint.h>
#include <xmmintrin.h>

/* RET_T is the function's return type (-DRET_T=float for f32); default: a 32-bit integer. */
#ifndef RET_T
#define RET_T int
#endif
extern RET_T func_UNDER_TEST(unsigned a0, unsigned a1, unsigned a2, unsigned a3);

static uint32_t bits_of(RET_T v)
{
    uint32_t b = 0;
    memcpy(&b, &v, sizeof v < 4 ? sizeof v : 4);
    return b;
}

int main(int argc, char **argv)
{
    if (argc < 3) return 2;
    /* The PS2 FPU has no denormals: flush them (FTZ and DAZ) so SSE arithmetic agrees. */
    _mm_setcsr(_mm_getcsr() | 0x8040);
    FILE *in = fopen(argv[1], "rb"), *out = fopen(argv[2], "wb");
    uint32_t base, size, n;
    if (!in || !out || fread(&base, 4, 1, in) != 1 || fread(&size, 4, 1, in) != 1 || fread(&n, 4, 1, in) != 1) return 2;
    void *mem = mmap((void *)(uintptr_t)base, size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (mem != (void *)(uintptr_t)base) { fprintf(stderr, "cannot map guest memory at %#x\n", base); return 3; }
    uint32_t args[4];
    uint8_t *buf = malloc(size);
    for (uint32_t t = 0; t < n; t++) {
        if (fread(args, 4, 4, in) != 4 || fread(buf, 1, size, in) != size) return 2;
        memcpy(mem, buf, size);
        int fd[2];
        if (pipe(fd)) return 2;
        fflush(out);
        pid_t pid = fork();
        if (pid == 0) {
            close(fd[0]);
            alarm(2);
            RET_T r = func_UNDER_TEST(args[0], args[1], args[2], args[3]);
            uint32_t hdr[3] = {1, bits_of(r), 0};
            if (write(fd[1], hdr, 12) != 12) _exit(1);
            size_t done = 0;
            while (done < size) { ssize_t w = write(fd[1], (uint8_t *)mem + done, size - done); if (w <= 0) _exit(1); done += w; }
            _exit(0);
        }
        close(fd[1]);
        uint8_t *res = malloc(12 + size);
        size_t got = 0;
        ssize_t r;
        while ((r = read(fd[0], res + got, 12 + size - got)) > 0) got += r;
        close(fd[0]);
        int st;
        waitpid(pid, &st, 0);
        if (got == 12 + (size_t)size) fwrite(res, 1, got, out);
        else {
            uint32_t hdr[3] = {0, 0, 0};
            fwrite(hdr, 4, 3, out);
            memset(res, 0, size);
            fwrite(res, 1, size, out);
        }
        free(res);
    }
    fclose(out);
    return 0;
}
