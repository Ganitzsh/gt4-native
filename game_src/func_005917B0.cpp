/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native correction (width): _IO_seekoff takes (fp, long offset, int dir, int mode) and returns long; the matched source declares the offset int (one register on the EE), natively the callee would read 8 bytes and dir/mode one slot off. Declaring it long adds a sign extension on the EE. */
/* libio (GNU iostream library, gcc 2000-10-03 snapshot): istream::seekg.
 * licence: libio (GPLv2 with the libio special exception, see THIRD_PARTY.md) */
typedef unsigned char u8;

struct Buf;

struct State {
    Buf *buf;
    char pad[0x16];
    u8 flags;
};

struct Stream {
    State *state;
};

extern "C" long _IO_seekoff(Buf *, long, int, int);

extern "C" Stream *func_005917B0(Stream *s, int a, int b) {
    if (_IO_seekoff(s->state->buf, a, b, 1) == -1)
        s->state->flags |= 4;
    return s;
}
