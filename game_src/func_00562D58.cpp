/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native stub of 00562D58: the original issues an IPU command. Not run natively: it is reached only from the ipum still-picture
   decoder 0054C8F0, which runtime/native/ipu_image.cpp replaces with FFmpeg, so nothing here drives the IPU or
   its DMA channels, which nothing models; if it is ever reached, gt4_not_native stops the game with its caller
   (runtime/missing.cpp). */
extern "C" void func_00562D58() {
    gt4_not_native(0x00562D58, __builtin_return_address(0),
                   "IPU path not used natively: reached only from 0054C8F0, which runtime/native/ipu_image.cpp replaces");
}
