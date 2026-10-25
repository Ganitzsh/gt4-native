/* compiler: ee-gcc2.96-nsa-nosib */
/* Native stand-in (not matched; the closest draft differs only by an assembler encoding): sets a
 * parameter of light `id` (101-108; the light records are 208 bytes each, at scratchpad
 * 0x70002000 + id * 208 - 0x4E30). Parameters 0, 1, 4, 5 and 13 take a 16-byte vector (read
 * unaligned from value) and go to func_004ABEC0, 6-10 take the float at value and go to
 * func_004ABE10 (both get the scratchpad base first); other ids and parameters do nothing. */
typedef unsigned int u128 __attribute__((mode(TI)));

extern "C" void func_004ABEC0(void *ctx, void *light, int param, u128 value);
extern "C" void func_004ABE10(void *ctx, void *light, int param, float value);

extern "C" void func_004A5BF8(int id, int param, void *value)
{
    char *spr = (char *)0x70002000;
    char *light;
    if ((unsigned int)(id - 101) >= 8u)
        return;
    light = spr + id * 208 - 0x4E30;
    switch ((unsigned int)param) {
    case 0: case 1: case 4: case 5: case 13: {
        u128 v;
        const unsigned char *s = (const unsigned char *)value;
        unsigned char *d = (unsigned char *)&v;
        for (int k = 0; k < 16; k++)
            d[k] = s[k];
        func_004ABEC0(spr, light, param, v);
        break;
    }
    case 6: case 7: case 8: case 9: case 10:
        func_004ABE10(spr, light, param, *(float *)value);
        break;
    default:
        break;
    }
}
