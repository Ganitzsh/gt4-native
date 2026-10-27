/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native code for 004C7B38 (unmatched; from the decomp's draft, corrected against the original): builds
 * the per-line layout nodes for a text run (func_004C0D30), creating a single default node when there are
 * none (its start index is found by skipping characters whose class from func_004C4CB8 is one of
 * 1,2,4,6,8,10,0x44,0x46,0x48,0x4F), then for each node picks the item with the largest bottom value, merges
 * the glyph attribute sets and calls func_004C7478 / func_004C7140.
 * The draft merged `case 0` with `default`, so a character of class 0 also advanced the start index; in
 * the original jump table (0x6B9C30) entry 0 goes to 0x4C7C94, past the increment at 0x4C7C90 that the
 * default entries use, so class 0 stops the scan without counting the character. */
#include "types.h"
#include "m2c_macros.h"
#define NULL 0
extern "C" {
s32 func_004C0D30(s32,s32,void*,void**) throw();
int func_004C2758(int,int,int,void*) throw();
u32 func_004C4CB8(int) throw() __attribute__((const));
s32 func_004C7140(s32,s32,s32*,int,int,int,int,int,u8*,int) throw();
s32 func_004C7478(s32,s32,s32,void*,void*,u8*,int,s32*) throw();
void func_004C8BB0(s32,void*);
void *func_004C8C20(s32,int);
void func_004C8C38(s32,void*);
void func_004C94A0(unsigned*,const unsigned*,const unsigned*);
int func_004C9580(void*,int);
int func_004C9658(int,unsigned*);
}

struct Item { unsigned flags; union { unsigned char *data; struct { unsigned char b4,kind; short h6; } direct; } u; short h8; unsigned short h10; short h12; short pad; };
struct Node { unsigned char offset; char pad[3]; Item *items; unsigned short count; short pad2; Node *next; };
static inline int kind(Item *p) {
 if (p->flags & 0x100000) return 8;
 if (p->flags & 0xC00000) return p->h10 & 511;
 int hi=(p->u.data[4]&63)<<3;
 return (p->u.data[5] >> 5) | hi;
}
static inline short start(Item *p,int offset) {
 int h=offset*40;
 int r;
 if (p->flags & 0x100000) r = h + p->u.direct.h6*2;
 else if (p->flags & 0xC00000) r = h;
 else r = h + (p->u.data[5]&31)*2;
 return r;
}
static inline int adjust(int y,unsigned *bits,int id) {
 int r=y;
 if(func_004C9580(bits,id)) r=(short)(y+30);
 return r;
}
static inline short end(Item *p,int y) {
 short result=y;
 if (!(p->flags & 0x100000)) {
 short h;
 if (p->flags & 0xC00000) h=p->h12;
 else h=p->h8;
 result=y+h;
 }
 return result;
}
extern "C" int func_004C7B38(int arg0,int arg1,void *arg2,unsigned char *arg3,int arg4,Item *arg5) {
 unsigned sp10[12],sp40[12],sp70[12];
 Node *head;
 int off; Item *item;
 int k=-1;
 if(arg5) {
  if(arg5->flags & 0x100000) { if(arg5->u.direct.kind==16 || arg5->u.direct.kind==13) k=1; }
  else if(arg5->flags & 0xC00000) k=arg5->h10&511;
  else { int hi=(arg5->u.data[4]&63)<<3; k=(arg5->u.data[5]>>5)|hi; }
 }
 func_004C9658(k,sp10);
 int result=func_004C0D30(arg0,arg1,arg2,(void**)&head);
 if(result<0) goto error;
 off=0;
 if(arg3) off=*arg3;
 if(!head) {
  if(func_004C9580(arg2,8)!=1) return 0;
  result=0;
  for(;;) {
   switch(func_004C4CB8(((unsigned short*)arg1)[result])) {
    case 1: case 2: case 4: case 6: case 8: case 10: case 0x44: case 0x46: case 0x48: case 0x4F: result++; continue;
    case 0: break;
    default: result++; break;
   }
   break;
  }
  head=(Node*)func_004C8C20(arg0,16);
  if(!head) return 0x80000306;
  item=(Item*)func_004C8C20(arg0,16);
  if(!item) { func_004C8C38(arg0,head); return 0x80000306; }
  goto init;
  error: func_004C8BB0(arg0,head); return result;
  init: func_004C2758(1,result,0,item);
  head->items=item; head->count=1; head->offset=result; head->next=0;
 }
 for(Node *node=head;node;node=node->next) {
  int offset=(node->offset+off)&255;
  Item *best=node->items;
  Item *cur=best+1;
  int n=node->count-1;
  int raw=start(best,offset);
  int y=raw;
  int id=kind(best);
  if(func_004C9580(sp10,id)) y=(short)(raw+30);
  func_004C9658(id,sp70);
  int bottom=y;
  if(best->flags & 0xC00000) { y=(short)(y+30); bottom=y; }
  bottom=end(best,bottom);
  while(n-- != 0) {
   int craw=start(cur,offset);
   int cy=craw;
   int cid=kind(cur);
   if(func_004C9580(sp10,cid)) cy=(short)(craw+30);
   func_004C9658(cid,sp40);
   func_004C94A0(sp70,sp40,sp70);
   if(best->flags & 0xC00000) cy=(short)(cy+30);
   int cb=end(cur,cy);
   if(y<cy) y=cy;
   if(bottom<cb) { bottom=cb; best=cur; }
   cur++;
  }
  result=func_004C7478(arg0,arg4,arg1,sp70,node,arg3,off,(int*)best);
  if(result<0) goto error;
  result=func_004C7140(arg0,arg4,(int*)best,0,0,offset,y,bottom,arg3,off);
  if(result<0) goto error;
 }
 func_004C8BB0(arg0,head);
 return 0;
}
