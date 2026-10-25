/* compiler: ee-gcc2.9-991111 */
/* native stand-in, not matched: Sony SDK liberx stop_module (modload.o, 3.0.2), called by
   ExecModuleLoad (func_00587C08) for the stop requests. Checks the module's state, rebuilds the
   argument vector ("self" or "other", then the NUL-separated strings of args), calls the module's
   entry with argc = 1 (stop) under the module's $gp, stores its result, then either marks the
   module resident (result & 3 != 1: error 0x81059034) or releases its library entries and marks it
   stopped. Written from the assembly (jump table D_006CEA80: states 1-2, 4-5, 6-7). Natively only
   meaningful for ERX modules, whose entries are MIPS code (only the Epson printing path loads them);
   the $gp switch is dropped. */

struct Module {
    char pad0[0xa];
    unsigned short flags;            /* 0x0A: state in bits 0-3, 0x10 = start done */
    char pad1[0x10];
    int (*entry)(int, int, char **, struct Module *); /* 0x1C, -1 = none */
    int gp;                          /* 0x20 */
    char pad2[0x10];
    char *libs;                      /* 0x34: exported library entries */
    int libsize;                     /* 0x38 */
    int x3c;                         /* 0x3C */
    int x40;                         /* 0x40 */
};

extern struct { int a, b; } D_0087E180;

int func_0057F260(const char *s);                 /* strlen */
char *func_005A609C(char *d, const char *s);       /* strcpy */
void *func_005A4724(void *d, const void *s, unsigned n); /* memcpy */
int func_0058B278(char *lib);                     /* ERX import stub: release library entries */
int func_0058B290(char *lib);                     /* ERX import stub: size of one entry block */
int func_0058B2A0(char *lib, int *out);           /* ERX import stub: query library */
void func_0058B2B0(int a, int b);                 /* ERX import stub */
void func_005ADB30(int thread, int prio);         /* ChangeThreadPriority */
void func_005ADF20(int mode);                     /* FlushCache */
int func_005B72A8(void);                          /* DIntr */
int func_005B72F8(void);                          /* EIntr */
void func_005B0750(const char *fmt, ...);         /* scePrintf */

int func_00588468(struct Module *m, int mode, int arglen, char *args, int *result)
{
    int local[2];
    unsigned short flags = m->flags;
    int state = flags & 0xF;
    int n, r, argc, intr;
    char *p, *end, *buf;
    char **argv;

    local[1] = (int)result;
    switch (state) {
    case 1: case 2:
        return 0x81059030;
    case 4: case 5:
        return 0x81059031;
    case 6: case 7:
        return 0x81059032;
    }
    if (!(flags & 0x10))
        return 0x81059035;

    if (m->libsize != 0) {
        n = m->libsize;
        p = m->libs;
        while (n > 0) {
            r = func_0058B2A0(p, local);
            if (r < 0)
                return r;
            if (r != local[0])
                return 0x81059038;
            r = func_0058B290(p);
            n -= r;
            p += r;
        }
    }

    /* count the strings of args */
    argc = 1;
    end = args + arglen;
    for (p = args; p < end; p += func_0057F260(p) + 1)
        argc++;

    buf = (char *)__builtin_alloca(((unsigned)(arglen + 16) >> 4) << 4);
    argv = (char **)__builtin_alloca(((unsigned)(argc * 4 + 0x13) >> 4) << 4);
    func_005A609C(buf, mode == 5 ? "other" : "self");
    argc = 1;
    argv[0] = buf;
    p = buf + func_0057F260(buf) + 1;
    if (arglen > 0) {
        argv[1] = p;
        func_005A4724(p, args, arglen);
        while (p < argv[1] + arglen) {
            argv[argc] = p;
            p += func_0057F260(p) + 1;
            argc++;
        }
    }
    argv[argc] = 0;

    m->flags &= 0xFFF0;
    m->flags |= (mode == 5) ? 4 : 5;
    r = 1;
    if ((int)m->entry != -1)
        r = m->entry(1, argc, argv, m);
    func_005ADB30(D_0087E180.a, D_0087E180.b);
    if ((int *)local[1] != 0)
        *(int *)local[1] = r;

    m->flags &= 0xFFF0;
    switch (r & 3) {
    case 1:
        break;
    case 0:
        m->flags |= 3;
        return 0x81059034;
    default:
        m->flags |= 0x13;
        return 0x81059034;
    }

    if (m->libsize != 0) {
        n = m->libsize;
        p = m->libs;
        intr = func_005B72A8();
        while (n > 0) {
            r = func_0058B278(p);
            if (r < 0) {
                func_005B0750("sceErxStopModule(): panic !!! library relase fail error=%d !!!\n", r);
                for (;;)
                    ;
            }
            r = func_0058B290(p);
            n -= r;
            p += r;
        }
        if (intr)
            func_005B72F8();
    }
    if (m->x40 != 0)
        func_0058B2B0(m->x3c, m->x40);
    func_005ADF20(2);
    m->flags |= (mode == 5) ? 6 : 7;
    return 0;
}
