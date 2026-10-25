/* compiler: ee-gcc2.9-991111 */
/* Native correction (width): func_00590520 takes (int, int b, int *, unsigned char *); the matched source declares b unsigned long (the 64-bit header union travels in one register on the EE), natively that is 8 bytes and shifts len and data. The header's bits all sit in the low word, so passing it as int keeps the value. */
typedef struct Bits {
    unsigned long f0 : 14;
    unsigned long f1 : 2;
    unsigned long f2 : 8;
    unsigned long f3 : 8;
} Bits;
typedef union HdrU {
    Bits b;
    unsigned long l;
} HdrU;
typedef struct Buf {
    int len1;
    int len0;
    char data[0x28];
} Buf;

extern void *func_005A4724(void *, const void *, unsigned);
extern int func_00590520(int, int, int *, Buf *);

int func_0058F638(int a0, int len1, void *src2, int len0, void *src1)
{
    HdrU h;
    Buf buf;
    int n;
    char *p;

    n = 0x28;
    h.b.f0 = 0xB;
    h.b.f1 = 1;
    h.b.f2 = 3;
    h.b.f3 = 1;
    buf.len0 = len0;
    func_005A4724(buf.data, src1, len0);
    buf.len1 = len1;
    len0 += (int)&buf;
    func_005A4724((char *)len0 + 8, src2, len1);
    return func_00590520(a0, (int)h.l, &n, &buf);
}
