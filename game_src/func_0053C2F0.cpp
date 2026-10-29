/* Native copy of the provisional draft for 0053C2F0: the init() callback gets action and body, which the EE forwards to it in $a1 and $a2 (passthrough_lint) */
typedef long long s64;
typedef int Config[3];
struct Work {
    int handle, ctx, kind; char padc[0x2024];
    int active, status; char pad2038[0xc]; int flag;
    char pad2048[0x34]; char request[0x800]; char host[0x400]; char service[0x200];
};
struct Obj { char pad[0x44]; Work *work; int arg; };
struct Api {
    char pad0[0x14];
    int (*open)(Work *, s64, int, Config *);
    void (*close)(int);
    int (*write)(int, char *, int, int *);
    char pad20[0x4c]; void (*init)(s64 *);
    char pad70[8]; int (*format)(char *, s64);
};
extern Api *D_0064B4B0;
extern char D_0086B540[];
extern char D_006C3EF8[], D_006C3F08[], D_006C3F20[];
extern "C" int func_0053BC28(char *, int, char *) throw();
extern "C" int func_0053B6E8(char *, char *, s64 *, int *) throw();
extern "C" int func_00540BF8(int *, unsigned, int);
extern "C" void func_00540EB0(int);
extern "C" void func_0057DA20(char *, char *, ...);
extern "C" int func_0057F260(const char *) throw();
extern "C" void *func_005A48D8(void *, int, unsigned);
extern "C" void func_005A6AB0(void *, const void *, int) throw();
extern "C" int func_0053C2F0(Obj *o, char *action, char *body) {
    Config config;
    s64 token;
    char buf[0x10];
    int port, written;
    ((void (*)(s64 *, char *, char *))D_0064B4B0->init)(&token, action, body);
    Work *w0 = o->work;
    w0->active = 1;
    w0->status = 0;
    w0->flag = 0;
    int r = func_0053BC28(D_0086B540, w0->kind, w0->request);
    if (r != 0) return r;
    r = func_0053B6E8(o->work->request, o->work->host, &token, &port);
    if (r != 0) return r;
    Work *w1 = o->work;
    int kind = w1->kind;
    if ((kind & *(int *)D_0086B540) == 0) return 0xd6dd;
    if (kind != 0x1000 && !(kind & 0x800)) return 2;
    r = func_00540BF8(&w1->ctx, 11, o->arg);
    if (r != 0) return r;
    if (o->work->ctx == 0) return r;
    if (o->work->kind == 0x1000) func_005A6AB0(o->work->service, D_006C3EF8, 0x200);
    else func_005A6AB0(o->work->service, D_006C3F08, 0x200);
    Config *pc = &config;
    func_005A48D8(pc, 0, 0xc);
    config[1] = 0x800;
    config[0] = 0;
    Api **api = &D_0064B4B0;
    r = (*api)->open(o->work, token, port, pc);
    if (r == 0) {
        (*api)->format(buf, token);
        int base = func_0057F260(body) + 0xd7;
        int size = func_0057F260(o->work->service);
        size += func_0057F260(action) * 2;
        Work *w2 = o->work;
        char *service = w2->service;
        func_0057DA20(w2->request, D_006C3F20, w2->host, buf, port, base + size, service, action, action, service, body, action);
        int length = func_0057F260(o->work->request);
        r = (*api)->write(o->work->handle, o->work->request, length, &written);
        if (r != 0 || (r = 0, func_0057F260(o->work->request) != written)) {
            (*api)->close(o->work->handle);
            goto cleanup;
        }
    } else {
cleanup:
        func_00540EB0(o->work->ctx);
        return r;
    }
    return 0;
}
