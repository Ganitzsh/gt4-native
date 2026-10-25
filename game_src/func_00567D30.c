#include "types.h"
void *memcpy(void *, const void *, unsigned int);
#include "m2c_macros.h"

s32 func_00567D30(void **arg0, int unused0, int unused1) {
    return M2C_FIELD(*arg0, s32 (**)(void **, int, int), 0x28)(arg0, unused0, unused1);
}
/* native correction of src/libstream/unit_005676B0/func_00567D30.c: returns the result of the function pointer it calls, and passes it the three arguments its registers still hold on the PS2 */
