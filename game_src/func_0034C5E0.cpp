/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Finds a point on the course for a signed distance and reports in which direction it was. The distance is turned
 * into a float and clamped to [-maxDist, maxDist], where maxDist is the course length
 * (DynamicsConductor__GetCourseLength) minus 20; when the byte at +0xCBDE is 1 the upper bound is instead the
 * value of virtual slot at vtable+0x158 for index count-2 (count comes from vtable+0x150) minus 20. It then calls
 * func_0046ADE8 on the sub-object at +0xDA50 with the kind (a kind of -1 is replaced by 6), the output pointers
 * for id, x, y, z and angle, the clamped distance and whether the mode was 1. Returns 0 if that fails, else 1, or
 * -1 when the requested distance was negative. The vtable is at +0x10140; the two virtual calls are small inline
 * helpers because each call needs the this-adjustment from the table entry. */
struct Obj { char pad[0xCBDE]; unsigned char mode; char pad2[0x10140-0xCBDF]; char *vt; };
struct Entry { short delta,index; };
struct VI { short delta,index; int (*fn)(void *); };
struct VF { short delta,index; float (*fn)(void *, int); };
extern "C" float DynamicsConductor__GetCourseLength(void *) throw();
extern "C" int func_0046ADE8(void *, int, int *, float *, float *, float *, float *, int, float);
static inline int count(Obj *conductor) {
    VI *v=(VI *)(conductor->vt+0x150);
    return v->fn((char*)conductor+v->delta);
}
static inline float limit(Obj *conductor, int i) {
    VF *v=(VF *)(conductor->vt+0x158);
    return v->fn((char*)conductor+v->delta,i);
}
extern "C" int func_0034C5E0(Obj *conductor, int *id, float *x, float *y, float *z, float *angle, int distance, int kind) {
    float dist = (float)distance;
    float maxDist = DynamicsConductor__GetCourseLength(conductor)-20.0f;
    float minDist = -maxDist;
    if (dist < minDist) dist = minDist;
    int modeIsOne = conductor->mode == 1;
    if (modeIsOne) maxDist = limit(conductor,count(conductor)-2)-20.0f;
    if (maxDist < dist) dist = maxDist;
    int minusOne = -1;
    int result = func_0046ADE8((char*)conductor+0xDA50, (kind ^ minusOne) ? kind : 6, id, x, y, z, angle, modeIsOne, dist);
    if (result) {
        result = 1;
        if (distance < 0) result = minusOne;
    }
    return result;
}
/* native correction of src/race/DynamicsConductor_DynamicsConductorBattle2P/func_0034C5E0.cpp (gt4-decomp tools/argcheck.py fix --kind classes --native): classes func_0046ADE8 */
