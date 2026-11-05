/* Native correction of src/ui/mRenderContextPS2/mRenderContextPS2__virtual_72.cpp (vtable 00663BE0, slot 73): a
 * method called as (this, eight floats), whose source leaves `this` out. The EE does not notice ($a0 vs $f12-$f19);
 * natively the callee read `this` as its first float and passed every rectangle coordinate one place off to
 * 004A6788. Found by tools/vtable_this_lint.py. */
extern "C" void func_004A6788(float, float, float, float, float, float, float, float);
extern "C" void mRenderContextPS2__virtual_72(void *self, float a, float b, float c, float d, float e, float f, float g,
                                              float h) {
    func_004A6788(a, b, c, d, e, f, g, h);
}
