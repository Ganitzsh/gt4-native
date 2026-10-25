/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native stand-in (not matched; closest draft 12 of 91 instructions off): blends n angles in
 * place, a[i] = wrap(a[i]) * (1 - t) + wrap(b[i]) * t, where wrap(x) = x - 2pi * floor(x / 2pi)
 * (func_00422A20 is the float-to-int floor) and a is first moved by 2pi towards b when they are
 * more than pi apart (a - b >= pi: a -= 2pi; a - b < -pi: a += 2pi). pi is 0x40490FDA, as in the
 * original. Parameter order (a, b, n, t): the image has no direct caller; t is the float argument
 * ($f12), a, b, n are $a0-$a2. */
extern "C" int func_00422A20(float);

extern "C" void func_0042B2D0(float *a, float *b, int n, float t)
{
    union { unsigned int u; float f; } pi_bits;
    pi_bits.u = 0x40490FDAu;
    const float pi = pi_bits.f;
    const float lo = 0.0f;
    const float two_pi = pi + pi;
    const float span = two_pi - lo;
    const float neg_pi = -pi;
    if (n <= 0)
        return;
    int count = n;
    do {
        float d = *a - lo;
        if (span != lo)
            d = d - span * (float)func_00422A20(d / span);
        float x = d + lo;
        d = *b++ - lo;
        if (span != lo)
            d = d - span * (float)func_00422A20(d / span);
        float y = d + lo;
        float yt = y * t;
        float diff = x - y;
        if (pi <= diff)
            x = x - two_pi;
        else if (diff < neg_pi)
            x = x + two_pi;
        *a++ = x * (1.0f - t) + yt;
    } while (--count != 0);
}
