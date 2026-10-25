/* compiler: ee-gcc2.9-991111 */
/* Native stand-in (not matched): libcdvd's callback thread "SceCdCallbackThread" (cdvd000.o,
 * entry 0057FFB0, created by sceCdInitEeCB at 00580080, see func_00580080.c; the inventory glues
 * both into one 368-byte range). Endless loop: wait on the callback semaphore; if the reason is
 * -1, clear the state and exit-delete the thread; print the callback and reason when the debug
 * level is set; call the user callback (sceCdCallback's, 0057FEA8) with the reason; clear the busy
 * flag. The original switches $gp to the callback's own $gp (0x00874D84) around the call, which
 * has no meaning natively. */
typedef void (*CdCallback)(int);

extern volatile int D_00655ED0;      /* debug level */
extern volatile int D_00655ED4;      /* callback thread id */
extern volatile int D_00655EE0;      /* callback semaphore */
extern volatile int D_00655EF4;      /* callback busy */
extern volatile int D_00655F20;      /* reason (-1: exit the thread) */
extern volatile int D_00655F24;      /* reason passed to the callback */
extern CdCallback D_00874D80;        /* the user's callback */
extern volatile int D_00874D94;

int func_005ADCE0(int sema);         /* WaitSema */
void func_005ADAE0(void);            /* ExitDeleteThread */
void func_005B0750(const char *fmt, ...);

void func_0057FFB0(void)
{
    for (;;) {
        func_005ADCE0(D_00655EE0);
        if (D_00655F20 == -1) {
            D_00655EF4 = 0;
            D_00655F20 = 0;
            D_00655ED4 = 0;
            D_00874D94 = 0;
            func_005ADAE0();
        }
        if (D_00655ED0 > 0)
            func_005B0750("sceCdCbfunc= %d sceCdCbfunc_num= %d\n", (int)D_00874D80, D_00655F24);
        if (D_00874D80 != 0 && D_00655F24 != 0)
            D_00874D80(D_00655F24);
        D_00655EF4 = 0;
    }
}
