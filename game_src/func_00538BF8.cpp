/* Native correction (width): func_00538B08 takes (void **, unsigned long size, int); the matched source declares the size int (one register on the EE), natively the callee would read the size's high half from the next slot. Declaring it unsigned long no longer matches on the EE. */

extern int func_00538B08(int *arg0, unsigned long arg1, int unusedw0);
extern void func_005A48D8(int arg0, int arg1, long long arg2);
extern int func_00538BF8(int *arg0, int arg1, long unusedw0)
{
  long long new_var;
  int var_v0 = 0x64;
  new_var = arg1;
  if (arg0 != 0)
  {
    *arg0 = 0;
    var_v0 = func_00538B08(arg0, arg1, unusedw0);
    if (var_v0 == 0)
    {
      int temp_v0 = *arg0;
      if (temp_v0 != 0)
      {
        long long se = (long long) ((int) new_var);
        func_005A48D8(temp_v0, 0, se);
        return 0;
      }
      var_v0 = 0x64;
    }
  }
  return var_v0;
}
