/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native code for 00462288 (matched; corrected for the native calling convention): finds the 24-byte record
 * of the 00461E50(12) list whose name (field +4) hashes (0044FEC0) to *key, stores its index and returns a
 * copy (the first record, index 0, when none does). 0044FEC0 returns a 64-bit hash (unsigned long, rotated
 * by 7 with dsll/dsrl32 over all 64 bits); the matched source declares it int, but on the EE the bnel at
 * 0x4622E4 compares the whole $v0 with the s64 loaded by the ld at 0x4622E0. Natively the hash lost its high
 * word and never matched the key; the declaration now returns unsigned long. */
typedef unsigned int u32;
typedef long s64;
struct Record { int x[6]; };
struct Obj { Record *base; u32 count; };
extern "C" void func_00461E50(Obj *, int) throw();
extern "C" unsigned long func_0044FEC0(int);
extern "C" Record func_00462288(s64 *key, u32 *index) {
    Obj obj;
    u32 i = 0;
    func_00461E50(&obj, 12);
    if (obj.count) {
    u32 off = 0;
    do {
        Record *r = (Record *)(off + (u32)obj.base);
        if (*key == func_0044FEC0(r->x[1])) {
            if (index) *index = i;
            return *(Record *)(off + (u32)obj.base);
        }
    } while (++i < obj.count && (off += 24, 1));
    }
    if (index) *index = 0;
    return *obj.base;
}
