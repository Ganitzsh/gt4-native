struct Head { char pad[28]; void *a1c,*a20; int pad24; void *a28; int a2c; };
struct Node { char pad[20]; void *a14; int a18; };
extern int func_005428E8(int);
extern void func_00542DA8(void *,unsigned,void *,int);
extern void func_005446A0(const void *,const void *);
extern void func_00544718(void *,void *);
extern void func_005468D0(void *,int,unsigned);
extern void func_00546A60(void *,void *,unsigned);
extern int func_00546CA0();
static inline unsigned tagged(const unsigned *w, unsigned key) { return (*w & 0x1fffffffu) | (key & 0xe0000000u); }
int func_00544488(struct Head *h,struct Node *n,unsigned key,void *data,unsigned len) {
 char q0[64],q40[64],q80[64],qc0[64]; unsigned word;
 int status=0;
 word=0;
 if (!h) return -1;
 switch(key & 0xe0000000u) {
 case 0xe0000000u: func_00546CA0(h->a1c,h->a20,data,data,len); break; /* native: the callee reads $a3, $t0 (data, len on the EE) */
 case 0x80000000u: func_00544718(q0,q40); func_00546CA0(q0,q40,data,data,len); break;
 case 0x60000000u:
 if(n && func_005428E8(n->a18)==0) { func_005468D0(n->a14,n->a18,key); func_00546A60(n->a14,data,len); }
 else status=-1;
 break;
 case 0x40000000u: status=-1; break;
 case 0x20000000u:
 if(func_005428E8(h->a2c)==0) { func_005468D0(h->a28,h->a2c,key); func_00546A60(h->a28,data,len); }
 else status=-1;
 break;
 case 0: break;
 default: status=-1;
 }
 if(len && status==0) {
 unsigned mode,value;
 func_00542DA8(data,len,&word,4);
 value=tagged(&word,key); word=value;
 mode=key & 0xe0000000u;
 if(key!=value && mode==0xe0000000u) {
 func_005446A0(data,h->a1c);
 func_00542DA8(data,len,&word,4);
 value=tagged(&word,key); word=value;
 }
 if(key!=value) status=-2;
 }
 return status;
}
