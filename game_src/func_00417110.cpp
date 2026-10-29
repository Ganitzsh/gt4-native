/* compiler: ee-gcc2.96-nogcse */
/* Native copy of the provisional draft for 00417110: the first segment getter gets f, which the EE forwards to it in $f12 (passthrough_lint) */
struct E { short delta, index; float (*fn)(void *); };
struct Seg { char p[0x20]; E *vt; char q[0x2c]; };
struct Body { char p[0x250]; Seg x, y, z; };
struct Obj { char p[0x268]; float angle; };
extern "C" float func_0059D2A0(float);
extern "C" float func_0057D1F0(float) throw();
extern "C" float *func_004233C0(float *, float, float, float);
static inline float value(Seg *s, float f) { E *e = (E *)((char *)s->vt + 0x28); return ((float (*)(void *, float))e->fn)((char *)s + e->delta, f); }
static inline float sqrtf_(float a) { float r; __asm__("sqrt.s %0, %1" : "=f"(r) : "f"(a)); return r; }
static inline float minf(float a, float b) { float r; __asm__("min.s %0, %1, %2" : "=f"(r) : "f"(a), "f"(b)); return r; }
extern "C" float func_00417110(Obj *o, Body *body, float f) {
    float r[2];
    float v[4];
    float x, y, z;
    float current = o->angle;
    float a = value(&body->x, f);
    float b = value(&body->y, f);
    float c = value(&body->z, f);
    float t = (a + c) * -2.0f;
    y = a * a + b * b + c * c - f * f - 2.0f * a * c;
    float d = sqrtf_(b * b * t * t - a * 16.0f * c * y);
    float inv = 1.0f / (a * 8.0f * c);
    r[0] = (-b * t - d) * inv;
    r[1] = (-b * t + d) * inv;
    if (r[0] >= -1.0f && r[0] <= 1.0f && (r[1] < -1.0f || r[1] > 1.0f)) x = r[0];
    else {
        x = r[1];
        y = r[0];
        if (x >= -1.0f && x <= 1.0f && (y < -1.0f || y > 1.0f)) ;
        else if (y >= -1.0f && y <= 1.0f && x >= -1.0f && x <= 1.0f) x = minf(y, r[1]);
        else return current;
    }
    x = __builtin_fabsf(0x1.921fb4p+0f - func_0059D2A0(x));
    if (x > 0x1.921fb4p+0f) {
        t = 0.5f;
        t *= b;
        t /= func_0057D1F0(0x1.921fb4p+1f - x);
        float *p = v;
        func_004233C0(p, c + t, f, a + t);
        x = *p;
        if (x > 0.0f) current -= x;
    }
    return current;
}
