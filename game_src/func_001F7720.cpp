extern char D_00645570[];
extern int func_004FD660(void *, int, int, int);
extern void mUpdateContext__Sync(int);
extern int func_001F1368(void *);

int func_001F7720(void *ctx, int a, int b, int c)
{
    while (!func_004FD660(D_00645570, a, b, c))
        mUpdateContext__Sync(1);
    return func_001F1368(ctx);
}
/* native correction of src/online/unit_001DC8F0/func_001F7720.cpp: returns int, the result of the tail call to func_001F1368 (declared returning int) */
