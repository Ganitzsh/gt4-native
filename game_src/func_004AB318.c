/* compiler: ee-gcc2.9-991111 */
/* Native stand-in for 004AB318 (no matched source yet; the closest draft differs only in its ld/lbu
 * registers and order): packs the GS registers selected by the bits of `bits` as A+D qwords
 * {value, register address} at `out`, from `values` (one u64 per bit) and `codes` (one register
 * address byte per bit); returns the end of the packet. The provisional draft stored each value at
 * out - 16 before advancing, one qword early, so every register received its neighbour's value. */
unsigned int func_004AB318(unsigned int out, unsigned long bits, unsigned char *codes, unsigned long *values) {
    do {
        values++;
        if ((bits & 1UL) == 1UL) {
            unsigned long value = values[-1];
            unsigned long code = *codes;
            out += 16;
            *(unsigned long *)(out - 16) = value;
            *(unsigned long *)(out - 8) = code;
        }
        bits >>= 1;
        codes++;
    } while (bits != 0);
    return out;
}
