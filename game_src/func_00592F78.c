/* Native correction (width): _IO_seekoff takes (fp, long offset, int dir, int mode) and returns long; the matched source declares the offset int (one register on the EE), natively the callee would read 8 bytes and dir/mode one slot off. Declaring it long adds a sign extension on the EE. */
/* libio (GNU iostream library, gcc 2000-10-03 snapshot): ostream::seekp.
 * licence: libio (GPLv2 with the libio special exception, see THIRD_PARTY.md) */
typedef unsigned char u8;

struct File { void *fd; char pad[0x16]; u8 flags; };
long _IO_seekoff(void *, long, int, int);

struct File **func_00592F78(struct File **h, int a, int b) {
    if (_IO_seekoff((*h)->fd, a, b, 2) == -1)
        (*h)->flags |= 4;
    return h;
}
