#include "types.h"

struct Rep {
    s32 len;
    s32 cap;
    s32 ref;
    s32 sel;
};

struct S00659988 {
    const char *name;
};

struct Str {
    char *p;
    char pad[12];
};

union U {
    Str s;
    s32 h[4];
};

struct VEntry {
    short delta;
    short index;
    void (*fn)(void *, void *);
};

/* Layout backed by include/gt4/hObject.h and bastring.h:
 *   VObj: HObjectHeader vtable slot 3 (0x18 / 8) is toString(Str *) const
 *   Rep: GCC 2.96 basic_string header at p - sizeof(Rep)
 */
struct VObj {
    char pad0[4];
    VEntry *vtbl;
};

extern char D_0068E7F8[];

extern "C" struct S00659988 *func_005C11A8(void);
extern "C" void func_00326798(void *p, s32 size, s32 align, const char *name);
extern "C" int func_004487D8(const char *arg0, s32 *dest);
extern "C" void *func_002FE278(void *arg0, s32 arg1);
extern "C" void func_002FC870(void *arg0, int arg1);
extern "C" void func_003285A8(s32 arg0);
extern "C" void func_003285F8(s32 arg0);

static inline void vcall(void *ret, VObj *o) {
    VEntry *e = &o->vtbl[3];
    e->fn(ret, (char *)o + e->delta);
}

static inline const char *c_str(Str *s) {
    const char *p;
    s32 len = ((Rep *)s->p)[-1].len;
    if (len == 0) p = D_0068E7F8;
    else {
        s->p[len] = 0;
        p = s->p;
    }
    return p;
}

static inline void release(Str *s) {
    Rep *q = (Rep *)s->p - 1;
    if (--q->ref == 0) {
        s32 cap = q->cap + 0x10;
        func_00326798(q, cap, 4, func_005C11A8()->name);
    }
}


extern Rep D_00659FA8;
extern "C" char *func_005C2560(Rep *arg0);
extern "C" s32 func_0057F260(const char *arg0);
extern "C" void func_005C2630(Str *arg0, s32 pos, s32 n, const char *text, s32 length);
extern "C" void *func_00314B20(void *arg0, Str *arg1);
extern "C" void func_00312318(void *arg0, s32 arg1);
static inline void empty(Str *s) {
    Rep *q = &D_00659FA8;
    char *p;
    if (q->sel != 0) {
        p = func_005C2560(q);
    } else {
        q->ref = q->ref + 1;
        p = (char *)(q + 1);
    }
    s->p = p;
}

struct Entry { char label[64]; float score; };
extern "C" void func_0013BDC0(void *, void *);
extern "C" s32 func_00147D80(s32);
extern "C" void *func_00441248(s32);
extern "C" void func_0013BD68(void *, s32);
extern "C" const char *func_00445350(void *);
extern "C" float func_00130540(void *);
extern "C" s32 func_00448908(s32 *);
extern "C" void func_002ED618(void *, void *);
extern "C" s32 hArray__size(s32);
extern "C" Entry *func_00578CF0(s32, s32);
extern "C" VObj **HArray__operator_index(void *, s32);
extern "C" char *func_005A609C(char *, const char *);
extern "C" float func_001305A0(const char *);
extern "C" s32 func_00130660(const void *, const void *);
extern "C" void func_005A4BF8(void *, s32, s32, s32 (*)(const void *, const void *)) throw();
extern "C" s32 func_00130618(Entry *, s32, s32, float);
extern "C" const char *func_00447E18(const char *);
extern "C" s32 func_0057F238(const char *, const char *);
extern "C" void func_00130690(s32, s32, s32 *, s32, s32);
extern "C" void func_001307E8(s32 *, s32, Entry *, s32);
extern "C" s32 func_00444190(void *);
extern "C" s32 func_004443A8(void *, const char *);
extern "C" s32 func_00445828(void *);
extern "C" float func_00579398(float);
extern "C" void SPEC_DATABASE__CarEquipments__setVariationOrder(void *, s32);
extern "C" void *func_002EFBF0(void *);
extern "C" void func_002EEEB0(void *, s32, void *);
extern "C" void func_00309378(void *, s32);
extern "C" void func_002ED5C0(void *, s32);
extern "C" void free(void *);
static inline s32 integer(VObj *o) {
    VEntry *e = &o->vtbl[11];
    return ((s32 (*)(void *))e->fn)((char *)o + e->delta);
}
static inline float real(VObj *o) {
    VEntry *e = &o->vtbl[12];
    return ((float (*)(void *))e->fn)((char *)o + e->delta);
}
extern "C" void MCarData__PickUpEnemyCars(s32 *arg0, s32 argc, VObj **args) {
    if (argc >= 2) {
        U input;
        s32 dest[8];
        U result;
        s32 indices[5];
        s32 weights[5];
        union Work {
            s32 record[96];
            struct { U list, temp, value; Str text; } output;
        } work;
        float offset = 0.0f;
        if (argc >= 3) offset = real(args[2]);
        float tolerance = 0x1.0624dcp-9f;
        if (argc >= 4) tolerance = real(args[3]);
        s32 count = 5;
        if (argc >= 5) count = integer(args[4]);
        s32 dirt = 0;
        if (argc >= 6) dirt = integer(args[5]) != 0;
        if (count >= 6) count = 5;
        s32 seen = 0, kept = 0;
        func_0013BDC0(&input, args);
        void *car = func_00441248(func_00147D80(input.h[0]));
        func_0013BD68(&input, 2);
        const char *ownLabel = func_00445350(car);
        float ownScore = func_00130540(car);
        func_004487D8(ownLabel, dest);
        s32 category = func_00448908(dest);
        if (category == 3) category = 0;
        func_002ED618(&input, args + 1);
        s32 total = hArray__size(input.h[0]);
        Entry *entries = func_00578CF0(0x40, total * 68);
        if (total > 0) { Entry *next = entries;
        do {
            VObj **node = HArray__operator_index(&input, seen);
            ++seen;
            vcall(&result.s, *node);
            const char *label = c_str(&result.s);
            release(&result.s);
            func_004487D8(label, dest);
            s32 candidate = func_00448908(dest);
            if (candidate == 3) candidate = 0;
            s32 eligible = category == candidate;
            if (category == 2 && candidate == 1) eligible = 1;
            if (eligible) {
                ++kept;
                func_005A609C(next->label, label);
                next->score = func_001305A0(label);
                ++next;
            }
        } while(seen < total);
        }
        float score = ownScore + offset;
        func_005A4BF8(entries, kept, 68, func_00130660);
        s32 low = func_00130618(entries, 0, kept, score + tolerance);
        s32 high = func_00130618(entries, 0, kept, score - tolerance);
        s32 span = high - low;
        while (span < count + 1 && high < kept - 1) { ++span; ++high; }
        while (span < count + 1 && low > 0) { --low; ++span; }
        s32 excluded = -1;
        for (s32 i = low; i < high; ++i) {
            if (!func_0057F238(func_00447E18(entries[i].label), ownLabel)) {
                excluded = i;
                break;
            }
        }
        func_00130690(low, high, indices, count, excluded);
        if (dirt) func_001307E8(indices, count, entries, excluded);
        for (s32 i = 0; i < count; ++i) {
            func_00444190(work.record);
            func_004443A8(work.record, entries[indices[i]].label);
            weights[i] = (s32)func_00579398((float)func_00445828(work.record));
            SPEC_DATABASE__CarEquipments__setVariationOrder(work.record, 2);
        }
        for (s32 i = 1; i < count; ++i) {
            s32 value = indices[i];
            s32 j = i - 1;
            while (j >= 0 && indices[j] < value) {
                indices[j + 1] = indices[j];
                --j;
            }
            indices[j + 1] = value;
        }
        func_002EFBF0(&result);
        for (s32 i = 0; i < count; ++i) {
            s32 index = indices[i];
            if (index >= 0) {
                U *list = &work.output.list;
                func_002EFBF0(list);
                s32 h = list->h[0];
                const char *label = entries[index].label;
                U *temp = &work.output.temp;
                Str *text = &work.output.text;
                empty(text);
                func_005C2630(text, 0, -1, label, func_0057F260(label));
                U *value = &work.output.value;
                func_00314B20(value, text);
                func_002EEEB0(temp, h, value);
                func_00309378(temp, 2);
                func_00312318(value, 2);
                release(text);
                s32 intHandle = list->h[0];
                func_002FE278(value, weights[i]);
                func_002EEEB0(temp, intHandle, value);
                func_00309378(temp, 2);
                func_002FC870(value, 2);
                func_002EEEB0(temp, result.h[0], list);
                func_00309378(temp, 2);
                func_002ED5C0(list, 2);
            }
        }
        s32 newVal = result.h[0];
        if (arg0 != result.h) {
            if (newVal) func_003285A8(newVal);
            s32 oldVal = *arg0;
            if (oldVal) func_003285F8(oldVal);
            *arg0 = newVal;
        }
        free(entries);
        func_002ED5C0(&result, 2);
        func_002ED5C0(&input, 2);
    }
}
/* native correction of src/gamedata/unit_0012D680/MCarData__PickUpEnemyCars.cpp (gt4-decomp tools/argcheck.py fix --kind classes --native): classes func_00130618 */
