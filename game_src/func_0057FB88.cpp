// Native version of litodp (0057FB88, soft-float library: signed 64-bit integer -> double). The
// kept draft (build/swarm/a6/0057fb88_best.cpp) declared func_0057F490 (the magnitude and sign -> double
// packer) as returning double, but its native code returns the double's bits as a 64-bit integer
// (edx:eax): the double read from st(0) was garbage. Here the packer returns the bits and this
// function returns them as a double (st(0)), as every caller declares it (libgcc's DFtype).
extern "C" unsigned long func_0057F490(unsigned long magnitude, unsigned long sign);
extern "C" double func_0057FB88(long value) {
    const unsigned long sign = (unsigned long)value >> 63;
    const unsigned long magnitude = sign ? -(unsigned long)value : (unsigned long)value;
    union { unsigned long l; double d; } u;
    u.l = func_0057F490(magnitude, sign);
    return u.d;
}
