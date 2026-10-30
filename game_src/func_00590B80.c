/* compiler: ee-gcc2.9-991111 */
/* Native version of 00590B80 _request_end (from its source): completes an MRPC request by its command (runs the
   client's callback under the client's $gp, or stores the result or status) and signals its semaphores. $gp is
   switched with gt4_get_gp()/gt4_set_gp() instead of moves to and from $28. */
struct Client {
    void *buf;
    int f4;
    int sema;
    int fC, f10;
    int size;
    void *gp;
    void (*func)(void *);
    void *arg;
    int result;
    int done_sema;
    int status;
};
struct Request {
    char pad[0x1C];
    struct Client *client;
    unsigned int cmd;
    int ret;
    int size;
};
extern void func_00590B60(void *);
extern void func_005ADCD0(int);

static inline void *SetGP(void *gp)
{
    void *old = gt4_get_gp();
    gt4_set_gp(gp);
    return old;
}

/* _request_end */
void func_00590B80(struct Request *req)
{
    struct Client *cl, *c;
    void *old;

    switch (req->cmd) {
    case 0x8000001A:
        cl = req->client;
        if (cl->func != 0) {
            old = SetGP(cl->gp);
            cl->func(cl->arg);
            SetGP(old);
        }
        func_005ADCD0(cl->done_sema);
        break;
    case 0x80000019:
        cl = req->client;
        cl->result = req->ret;
        cl->size = req->size;
        func_005ADCD0(cl->done_sema);
        break;
    case 0x8000001C:
        break;
    case 0x8000001D:
        cl = req->client;
        cl->status = req->ret;
        func_005ADCD0(cl->done_sema);
        break;
    }
    c = req->client;
    if (c->sema >= 0)
        func_005ADCD0(c->sema);
    func_00590B60(c->buf);
    c->buf = 0;
}
