/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native correction of the provisional draft drafts/00453a58/00453a58_490e14206ce7eac5.cpp (argument order, argcheck.py orders): func_0057D8A0 (sincos) takes (float *sine, float *cosine, float x); the draft declared the float first, which on the EE is the same call ($f12, $a0, $a1) but natively put x in the pointers' stack slots. */
struct Quad {
    float x0, y0, x1, y1, x2, y2, x3, y3;
};

extern "C" void func_0057D8A0(float *, float *, float);
extern "C" void func_0049B540(int, Quad *, Quad *, int);

extern "C" void func_00453A58(float ang, float size, float b, float c) {
    float sc[2];
    Quad q2;
    Quad q;
    float h = size * 0.5f;
    func_0057D8A0(&sc[0], &sc[1], ang);
    sc[0] = sc[0] * h;
    sc[1] = sc[1] * h;
    float e = sc[1] + sc[0];
    float d = sc[1] - sc[0];
    q.x0 = h - d;
    q.x3 = h - e;
    q.x1 = h + e;
    q.x2 = h + d;
    q.y1 = size + 2.0f * q.x0 + c;
    q.y0 = size + 2.0f * q.x3 + c;
    q.y2 = size + 2.0f * q.x1 + c;
    q.y3 = size + 2.0f * q.x2 + c;
    q2 = q;
    q.x1 = b;
    q.x2 = b;
    q.y2 = b;
    q.y3 = b;
    q.x0 = 0.0f;
    q.y0 = 0.0f;
    q.y1 = 0.0f;
    q.x3 = 0.0f;
    func_0049B540(4, &q2, &q, 0);
}
