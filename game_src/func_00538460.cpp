/* Native correction (width): func_00538530 takes a pointer first; the matched source declares it long (one register on the EE, 8 bytes natively), which shifts the size pointer after it. Declaring it 32-bit changes 2 EE instructions. */
struct Obj { unsigned active, pos, end, total; char *data; };
extern "C" int func_005380B8(Obj *) throw();
extern "C" int func_00538228(Obj *, const char *, unsigned) throw();
extern "C" int func_00538338(char *, Obj *, unsigned) throw();
extern "C" int func_00538530(int, unsigned *) throw();
extern "C" int func_00538460(unsigned handle, int mode, char *buf) {
    unsigned size = 0;
    int result = 2;
    Obj *s = (Obj *)handle;
    if (s && (mode == 0 || buf)) {
        if (s->active) {
            result = func_00538530((long)(int)s, &size);
            if (result) return result;
            if (size == 0) {
                s->active = 0;
                s->pos = 0;
            } else {
                result = 0;
                if (mode == 1) {
                result = func_00538338(buf, s, size);
                if (result) return result;
                result = func_005380B8(s);
                if (result) return result;
                result = func_00538228(s, buf, size);
                if (result) return result;
                }
            }
        }
        result = 0;
    }
    return result;
}
