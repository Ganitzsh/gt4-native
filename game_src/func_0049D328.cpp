/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 0049D328 (from its source): appends a command's DMA tags (one per item, then an end tag) to the
   packet at obj->out, with its state changes and callbacks; without the two prefetch hints (pref) of the packet. */
struct Item { unsigned addr; unsigned short count, event; };
struct Cmd { int a; float value; unsigned char flag, bits; unsigned short count,nc,nd; Item item[1]; };
struct Packet { unsigned a,b,c,d; };
struct Obj { unsigned char flag,p1,p2,valid; char pad4[16]; int count,x,y; float value; char pad24[28]; unsigned mask; char pad44[0x194]; unsigned long long bits; char pad1e0[8]; unsigned long long bits2; char pad1f0[8]; Packet *out; char pad1fc[32]; void (*callback)(unsigned short); char pad220[0xe00]; int nc,nd,ne; };
extern "C" void func_0049D080(Obj *,Cmd *) throw();
extern "C" int func_0049D1A0(Obj *,Cmd *,int) throw();
extern "C" void func_0049DA60(Obj *,Item *,unsigned char);
extern "C" void func_0049E480(Obj *);
extern "C" void func_0049E548(Obj *,int,float);
extern "C" void func_0049E5F8(Obj *,int);
extern "C" void func_0049D328(Cmd *cmd, Obj *obj, long unusedw0) {
 if(!cmd->count || !obj->count) return;
 Packet *p;
 if(obj->flag) func_0049E480(obj);
 if(cmd->flag) func_0049D080(obj,cmd);
 int n=obj->count;
 if(!obj->valid || obj->value!=cmd->value) {
 func_0049E548(obj,0xc1,cmd->value); obj->valid=1; n--;
 }
 unsigned a1=0x20000000;
 obj->nc+=cmd->nc;
 Item *item=cmd->item;
 int mode=2;
 obj->nd+=cmd->nd;
 obj->ne+=cmd->count;
 int left=cmd->count;
 unsigned a3=0x3f3f3f3f;
 if(!(cmd->bits&0x40)) a3=0xc0c0c0c0;
 obj->mask=a3;
 do {
 p=obj->out;
 p->a=item->count|0x30000000; p->b=item->addr&0xfffffff; p->c=a1; p->d=a3;
 p++;
 obj->out=p;
 if(cmd->bits&0x80) func_0049DA60(obj,item,cmd->bits);
 if(item->event && obj->callback) obj->callback(item->event);
 if(cmd->flag) { n+=60; mode=func_0049D1A0(obj,cmd,mode); }
 if(obj->bits & obj->bits2) { func_0049E5F8(obj,0xd9); n-=5; }
 a1=(obj->x+obj->y)|0x04000000;
 a3=n|0x14000000;
 item++;
 n=obj->count;
 } while(--left);
 p=obj->out;
 p->a=0x10000000; p->b=0; p->c=a1; p->d=a3;
 p++;
 obj->out=p;
}
