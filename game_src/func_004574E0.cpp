/* Native correction of src/engine/DriverCallback/func_004574E0.c: a model callback's level of detail by distance,
 * slot 3 of the vtables 0067E1F0, 006885B8 and 00688660, called as (this, distance); its source leaves `this` out.
 * Natively it read `this` as the distance (a tiny float) and always chose level 3. Takes the object here; the one
 * direct caller, 004540A8 (game_src), passes its own. Found by tools/vtable_this_lint.py. */
extern "C" int func_00454FB0(float);
extern "C" int func_004574E0(void *self, float distance) {
    return func_00454FB0(distance);
}
