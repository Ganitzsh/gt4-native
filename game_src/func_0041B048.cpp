/* compiler: ee-gcc2.96-no-strict-aliasing */
extern "C" {
void func_0041BB18(char *, char *, char *, float);
void func_0041BE58(char *, char *, char *, float);
void func_0041B048(char *a, char *b, char *c, float f) {
    func_0041BE58(a, b, c, f);
    func_0041BB18(a + 0x70, b + 0x70, c + 0x70, f);
    func_0041BB18(a + 0xC0, b + 0xC0, c + 0xC0, f);
    func_0041BB18(a + 0x110, b + 0x110, c + 0x110, f);
    func_0041BB18(a + 0x160, b + 0x160, c + 0x160, f);
    func_0041BB18(a + 0x1B0, b + 0x1B0, c + 0x1B0, f);
    func_0041BB18(a + 0x200, b + 0x200, c + 0x200, f);
    func_0041BB18(a + 0x250, b + 0x250, c + 0x250, f);
    func_0041BB18(a + 0x2A0, b + 0x2A0, c + 0x2A0, f);
    func_0041BE58(a + 0x2F0, b + 0x2F0, c + 0x2F0, f);
    func_0041BB18(a + 0x360, b + 0x360, c + 0x360, f);
    func_0041BB18(a + 0x3B0, b + 0x3B0, c + 0x3B0, f);
    func_0041BB18(a + 0x400, b + 0x400, c + 0x400, f);
    func_0041BB18(a + 0x450, b + 0x450, c + 0x450, f);
    func_0041BB18(a + 0x4A0, b + 0x4A0, c + 0x4A0, f);
    func_0041BB18(a + 0x4F0, b + 0x4F0, c + 0x4F0, f);
    func_0041BB18(a + 0x540, b + 0x540, c + 0x540, f);
    func_0041BB18(a + 0x590, b + 0x590, c + 0x590, f);
    func_0041BB18(a + 0x5E0, b + 0x5E0, c + 0x5E0, f);
    func_0041BB18(a + 0x630, b + 0x630, c + 0x630, f);
}
}
/* native correction of src/race/unit_00415E90/func_0041B048.cpp (gt4-decomp tools/argcheck.py fix --kind classes --native): classes func_0041BB18; classes func_0041BE58 */
