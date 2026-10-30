/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 0055E6D0 (from its source): copies the audio transfer payload in with interrupts disabled
   (DIntr), marks it dirty and queues its transfer; re-enables interrupts with gt4_interrupts_enable() instead of the
   `ei` instruction when they were enabled. */
typedef unsigned char u8;

struct AudioTransferState {
    int argumentForContextWait;
    u8 unknown04[0x3C];
    u8 payload[0x3B8];
    int dirty;
};

extern AudioTransferState D_008735C0;
extern u8 D_00873DC0[];

extern "C" void func_00578500(int argument);
extern "C" int func_005B72A8(void);
extern "C" int func_00578168(AudioTransferState *, int, int, const void *, int);

extern "C" void func_0055E6D0(const void *source) {
    // The global's first word is passed through to this helper; its role is unknown.
    func_00578500(D_008735C0.argumentForContextWait);
    int interruptsWereEnabled = func_005B72A8();
    __builtin_memcpy(D_008735C0.payload, source, 0x3B8);
    if (interruptsWereEnabled != 0) {
        gt4_interrupts_enable();
    }
    D_008735C0.dirty = 1;
    func_00578168(&D_008735C0, 4, 1, D_00873DC0, 0x80);
}
