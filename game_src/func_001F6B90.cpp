/* Native correction of src/online/unit_001DC8F0/func_001F6B90.cpp (matched): it calls 004F9F70(D_00645570), which
 * reads a second argument, and on the EE gets this function's own $a1 (the parameter the source calls `unused`),
 * never written before the call; natively it read a stack slot (tools/notpassed_triage.py). Forwarded explicitly.
 * Online play only. */
extern char D_00645570[];
extern char *func_004F9F70(void *, int);
extern void func_004FA048(void *, char *);
extern "C" char *func_005A609C(char *, const char *);

void func_001F6B90(void *ctx, int a1, char *name)
{
    char *entry = func_004F9F70(D_00645570, a1);
    if (entry) {
        func_005A609C(name, entry + 4);
    } else {
        name = 0;
    }
    func_004FA048(D_00645570, entry);
}
