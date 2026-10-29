/* compiler: ee-gcc2.96-exceptions */
/* Native copy of the provisional draft for 0053C0E8: the init() callback gets kind and arg, which the EE forwards to it in $a1 and $a2 (passthrough_lint) */
struct Conn {
 void *session; int handle; char p8[0x2028];
 int async,zero; char p2038[0xc]; int flag;
 char p2048[0x34]; char request[0x800]; char host[0x800];
};
struct Self { char p[0x44]; Conn *conn; int arg; };
struct Address { unsigned long value; char pad[8]; };
struct Params { int zero; union { int size; void *ptr; }; int other; };
struct Iface {
 char p[0x14]; int (*open)(Conn *,unsigned long,int,Params *);
 void (*close)(void *);
 int (*write)(void *,char *,int,int *);
 char q[0x4c]; void (*init)(void *);
 char r[8]; void (*format)(char *,unsigned long);
};
extern Iface *D_0064B4B0;
extern char D_0086B540[];
extern char D_006C3D28[];
extern "C" int func_0053BC28(char *,int,char *) throw();
extern "C" int func_0053B6E8(char *,char *,Address *,int *) throw();
extern "C" int func_00540BF8(int *,unsigned int,int);
extern "C" void func_00540EB0(int);
extern "C" int func_0057F260(const char *) throw();
extern "C" void *func_005A48D8(void *,int,unsigned int);
extern "C" void func_0057DA20(char *,const char *,...);
extern "C" int func_0053C0E8(Self *self,int kind,char *arg) {
 Params params;
 Address address;
 char text[32];
 int length,written;
 ((void (*)(void *,int,char *))D_0064B4B0->init)(&address,kind,arg);
 { Conn *c=self->conn;
 c->async=0;
 c->zero=0;
 c->flag=0;
 }
 int r=func_0053BC28(D_0086B540,kind,self->conn->request);
 if(r) return r;
 r=func_0053B6E8(self->conn->request,self->conn->host,&address,&length);
 if(r) return r;
 if(!(kind & *(int *)D_0086B540)) return 0xd6dd;
 if(kind!=0x1000 && !(kind&0x800)) return 2;
 r=func_00540BF8(&self->conn->handle,10,self->arg);
 if(r) return r;
 if(!self->conn->handle) return r;
 Params *pr=&params;
 func_005A48D8(pr,0,12);
 pr->size=0x800;
 pr->zero=0;
 r=(D_0064B4B0)->open(self->conn,address.value,length,pr);
 if(r==0) {
  (D_0064B4B0)->format(text,address.value);
  int size=func_0057F260(arg);
  func_0057DA20(self->conn->request,D_006C3D28,self->conn->host,text,length,size+0x11c,arg);
  int n=func_0057F260(self->conn->request);
  r=(D_0064B4B0)->write(self->conn->session,self->conn->request,n,&written);
  if(r==0 && func_0057F260(self->conn->request)==written) goto ok;
  (D_0064B4B0)->close(self->conn->session);
 }
 func_00540EB0(self->conn->handle);
 return r;
ok:
 return 0;
}
