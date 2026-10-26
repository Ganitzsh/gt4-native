/* Native stopgap for 005637A8 (libmpeg: scan the bitstream for the next picture; matched in the
 * decomp): reports the sequence end, so every movie ends before its first picture. The original
 * finds start codes with IPU FDEC commands and reads the results back from IPU_CMD / IPU_CTRL;
 * natively those registers are plain memory and nothing decodes, so the scan never ended (the
 * decoder thread spun at priority 114 and the boot stayed on a black screen). It first sleeps
 * 0.2 s: mMovieFace only reports a movie's end after it has seen it playing (mMoviePS2 slot 18
 * watches the stream's +0x404 flag go on, then off, once per frame). Movies need an MPEG-2 path
 * of their own: an IPU emulation or a replacement of the player at the mpegif level
 * (docs/IOP_SERVICES.md, MPG1/MPG2). */
extern "C" int func_00578B10(int usec); /* DelayThread */

extern "C" int func_005637A8(void) {
    func_00578B10(200000);
    return 0;
}
