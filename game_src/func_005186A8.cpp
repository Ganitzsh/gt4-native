/* compiler: ee-gcc2.96-nsa-g */
/* Native code for 005186A8 (unmatched; from the decomp's draft, corrected against the original): for the
 * local owner's entry, walks its items; for each ready item (state 2) with pending stream data, sends up
 * to 255 units when units*1000 < rate*(now - item->time)*1.02 and the send succeeds, then updates the
 * item's time and stream; idle active items are released (state 1, func_00518540).
 * The draft wrote the factor as 1.02 (0x3FF051EB851EB852); the original loads the double at D_006C24D0
 * (lui $at, 0x6C / ld $a1, 0x24D0($at) at 0x518868), which is 0x3FF051EB851EB851, one ulp lower. */
typedef unsigned int u32;
typedef unsigned char u8;
typedef unsigned short u16;
struct Owner { char pad[0x104]; u32 id; };
struct Entry { char pad[0x10]; u32 first, count; };
struct Item { u8 state; char pad1[2]; u8 kind, active, use; char pad6[2]; u32 target; char padC[4]; void *data; char pad14[12]; int rate; char pad24[2]; u16 count; int time; char stream[8]; u32 size; char pad38[4]; const void *source; };
struct Clock { char pad[0x5C]; int time; };
struct Stack { u32 id, n, i; Entry *entry; };
extern Owner *D_00854150;
struct Net { char pad[0xA5F8]; int ready[1]; };
extern Net *D_0064B4B4;
extern Clock *D_0064B47C;
extern char D_00853E38[];
extern double D_006C24D0;
extern "C" Entry *func_0051B8A8(Owner *,u32);
extern "C" int func_00515F98(u32 *,Owner *) throw();
extern "C" Item *func_00515EE8(u32) throw();
extern "C" int func_00538530(void *,u32 *);
extern "C" int func_00518418(void *,u32,u32) throw();
extern "C" int func_00532588(Owner *,u32,int,int,void *);
extern "C" int func_005380D8(void *,u32);
extern "C" int func_00538418(void *,int);
extern "C" void *func_005A4724(void *,const void *,u32);
extern "C" void func_00531738(void **);
extern "C" int func_00538068(void *);
extern "C" void func_00518540(u32,u32) throw();
extern "C" void func_005186A8() {
    u32 sid, sn, si; Entry *se;
    Owner *owner = D_00854150;
    sid = 0x100;
    if (owner) {
        se = func_0051B8A8(owner,owner->id);
        if (se && se->first < 0x200 && func_00515F98(&sid,owner) == 0 && sid < 0x40) {
            si = 0;
            u32 next;
            if (se->count) do {
                u32 id = si + se->first;
                Item *item = func_00515EE8(id);
                if (item && item->state == 2) {
                    u32 target = item->target;
                    if (D_0064B4B4->ready[item->kind]) {
                        if (owner->id != sid) {
                            target = item->use ? sid : target;
                        }
                    }
                    if (item != (Item *)-0x2C) {
                        void *stream = item->stream;
                        sn = 0;
                        if (func_00538530(stream,&sn) == 0) {
                            if (sn) {
                                u32 count = item->count;
                                int oldtime = item->time;
                                u32 n = count >= sn ? sn : count;

                                int time = D_0064B47C->time;
                                sn = n < 0x100 ? n : 0xFF;
                                u32 bytes = sn * 1000;
                                int delta = time - oldtime;
                                double required = (double)bytes;
                                if (required < (double)(item->rate * delta) * 0x1.051eb851eb851p+0 && func_00532588(owner,target,14,func_00518418(D_00853E38,id,sn),D_00853E38) == 0) {
                                    item->time = D_0064B47C->time;
                                    func_005380D8(stream,sn);
                                    func_00538418(stream,1);
                                    func_005A4724(item->data,item->source,item->size);
                                }
                            } else if (item->active) {
                                func_00531738(&item->data);
                                func_00538068(stream);
                                item->data = 0;
                                item->state = 1;
                                func_00518540(target,id);
                            }
                        }
                    }
                }
                ++si;
            } while (si < se->count);
        }
    }
}
