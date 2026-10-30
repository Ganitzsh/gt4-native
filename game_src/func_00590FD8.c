/* compiler: ee-gcc2.9-991111 */
/* Native version of 00590FD8 sceSifMCallRpc (from its source): sends an MRPC call to the IOP and waits for it (mode
   0, the only one accepted); the client keeps the caller's $gp for its callback, now from gt4_get_gp() instead of reading $28. */
struct Srv { int p0,p4,p8,pc,p10; int self; int id; int cl; int rpc; int size; int recv; int rsize; int mode; int extra; };
struct Cl { int srv; int id; int sem; int pc,v10; int dest; int gp; int cb; void *cbarg; int extra; int sem2; };
struct Sema { int attr,max,init,d,e; const char *name; };
extern void func_005B0750(const char *, ...);
extern int func_005ADCA0(struct Sema *);
extern int func_005ADCB0(int);
extern int func_005ADCE0(int);
extern char D_00885AC0[];
extern struct Srv *func_00590AB8(void *);
extern void func_00590B60(struct Srv *);
extern int func_005B0DB0(int, struct Srv *, int, void *, void *, int);
extern void func_005B0F78(void *, int);
int func_00590FD8(struct Cl *cl, int rpc, int mode, void *send, int size, void *recv, int rsize, int cb, int cbarg) {
    struct Sema sp;
    struct Srv *s;
    int sem, gp;
    if (mode) { func_005B0750("mode must be 0 in this version.\n"); return -1; }
    func_005ADCE0(cl->sem2);
    if ((unsigned)send & 63) { func_005B0750("%s: send buffer addr is not 64 byte align. %08x=n", "sceSifMCallRpc", send); return -1; }
    if ((unsigned)recv & 63) { func_005B0750("%s: receive buffer addr is not 64 byte align. %08x=n", "sceSifMCallRpc", recv); return -1; }
    s = func_00590AB8(D_00885AC0);
    if (!s) return -1;
    cl->cbarg=(void *)cbarg; cl->id=s->id; cl->srv=(int)s; cl->cb=cb;
    gp=(int)gt4_get_gp();
    cl->gp=gp;
    s->rpc=rpc; s->self=(int)s; s->size=size; s->recv=(int)recv; s->rsize=rsize; s->cl=(int)cl; s->extra=cl->extra;
    if (send==recv) func_005B0F78(send,size>=rsize?size:rsize);
    else { if(size>0) func_005B0F78(send,size); if(rsize>0) func_005B0F78(recv,rsize); }
    if (mode & 1) {
        if (!cb) s->mode=0; else s->mode=1;
        cl->sem=-1;
        if (!func_005B0DB0(0x8000001A,s,64,send,(void *)cl->dest,size)) {func_00590B60(s);return -2;}
        return 0;
    }
    sp.init=0; sp.max=1; sp.name="SceMrpcCall";
    sem=func_005ADCA0(&sp); cl->sem=sem;
    if(sem<0){func_00590B60(s);return -3;}
    s->mode=1;
    if(!func_005B0DB0(0x8000001A,s,64,send,(void *)cl->dest,size)){func_00590B60(s);func_005ADCB0(sem);return -2;}
    func_005ADCE0(sem);func_005ADCB0(sem);return 0;
}
