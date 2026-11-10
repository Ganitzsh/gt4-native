/* compiler: ee-gcc2.96-no-strict-aliasing */
/* MCarGarage::getColors (0013C500, matched in gt4-decomp src/gamedata/unit_0013B5F8/MCarGarage__getColors.cpp): a
 * script method, the list of a car's colours (Arcade's car screens, the garage).
 * The matched source ends by destroying its 64 colours from the last to the first, and writes the array's end as the
 * address of the local declared after it (`(V4 *)ph`), which is where the PS2's frame puts it. In this build's frame
 * `h` is not at `arr + 64`, so the loop stepped down from wherever `h` is, 16 bytes at a time, until it met `arr`:
 * through the address space, a few hundred milliseconds of calls to a destructor that does nothing for these
 * objects. The frame's callbacks had no safe point for that long and were delivered by freezing the thread (4 times
 * on the way to a time trial: docs/modern/PLAN.md P4, whose diagnostic named this function), and the car screen
 * stalled as long. gt4-native writes the end as what it is, `arr + 64`. Otherwise the function is GT4's. */
typedef int s32;
struct V4 { char pad[0x10]; };
struct H { void *p; char pad[0xC]; };
extern "C" {
void func_0013BD68(void *, int) throw();
void func_0013BDC0(void *, void *) throw();
s32 func_00145BE8(void *);
void func_00145C30(void *, V4 *, s32);
void func_002030A0(V4 *);
void func_00203118(V4 *, int);
void func_00203620(void *, int);
void func_00204558(void *, V4 *);
void func_002ED5C0(void *, int);
void *func_002EEEB0(void *, void *, void *);
void func_002EFBF0(void *);
void func_00309378(void *, s32);
void func_003285A8(void *);
void func_003285F8(void *);

void MCarGarage__getColors(void **arg0, void *arg1) {
    void *obj;
    V4 arr[64];
    H h;
    H tmp;
    V4 v;
    int i;
    func_0013BDC0(&obj, arg1);
    s32 n = func_00145BE8(obj);
    V4 *pp = arr;
    for (i = 63; i != -1; i--) { func_002030A0(pp); pp++; }
    func_00145C30(obj, arr, 0x40);
    H *ph = &h;
    func_002EFBF0(ph);
    H *p_tmp;
    if (n > 0) {
        p_tmp = &tmp;
        V4 *q;
        for (q = arr, i = n; i != 0; i--) {
            void *hp = ph->p;
            func_00204558(&v, q);
            func_002EEEB0(p_tmp, hp, &v);
            q++;
            func_00309378(p_tmp, 2);
            func_00203620(&v, 2);
        }
    }
    if (arg0 != &ph->p) {
        void *p = ph->p;
        if (p != 0) func_003285A8(p);
        if (*arg0 != 0) func_003285F8(*arg0);
        *arg0 = p;
    }
    func_002ED5C0(ph, 2);
    V4 *c = arr + 64;   /* (the matched source: (V4 *)ph, the next local on the PS2's frame) */
    if (arr != c) {
        do {
            c--;
            func_00203118(c, 2);
        } while (arr != c);
    }
    func_0013BD68(&obj, 2);
}
}
