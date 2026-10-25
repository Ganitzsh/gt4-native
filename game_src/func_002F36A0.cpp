extern "C" void func_003065F0(int arg0, void *arg1);
extern "C" void func_00310BF0(void *arg0, int arg1);
extern "C" void func_00310FF0(void *arg0, int unused0, int unused1);

extern "C" int func_002F36A0(int arg0, int unused0, int unused1) {
    char buf[8];
    func_00310FF0(buf, unused0, unused1);
    func_003065F0(arg0, buf);
    func_00310BF0(buf, 2);
    return 0;
}
/* native correction of src/adhoc/hClass/func_002F36A0.cpp: returns 0, the $v0 left by func_00310BF0(buf, 2) -> func_003038E0(.., 0) -> func_00323B60(.., 0) -> func_00328798(.., 0), which ends with v0 = arg1 & 1. */
