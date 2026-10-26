/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native stand-in (not matched): Polyphony's printf engine (vsprintf-style, output through a
 * callback), from the provisional draft, with the argument list walked the native way. On the EE
 * every variadic argument fills an 8-byte slot; natively the list is i386's: int, char, short and
 * pointers take 4 bytes, long (64-bit in the EE data model), long long and double take 8. */
typedef unsigned char u8;
typedef unsigned long long u64;
struct Ctx { char pad[0x74]; int enabled; char pad78[0x28]; int state; };
typedef int (*Out)(int,char*,Ctx*);
extern "C" int func_00493438(u8**,int*) throw();
extern "C" int func_00493B78(u8**) throw();
extern "C" int func_00493468(Out,char**,Ctx*,int,int,int,const char*) throw();
extern "C" int func_00493918(Out,char**,Ctx*,int,int,int,u64,int,int) throw();
extern "C" int func_00493A28(Out,char**,Ctx*,int,int,int,u64,int) throw();
extern "C" int func_00493AB0(Out,char**,Ctx*,int,int,int,u64,int) throw();
#define ARG(T) ({ char *p=args; args+=(sizeof(T)+3)&~3; T v=*(T*)p; v; })
extern "C" int func_00493C70(Ctx *ctx, Out out, char *dst, u8 *fmt, char *args) {
 int total=0, len, c;
 ctx->state=-1;
 while ((c=func_00493438(&fmt,&len)), total+=len, c) {
  if (!ctx->enabled) {
   if(out(c,dst,ctx)) return -1;
   if(dst) ++dst;
  } else {
   int flags; u8 *start;
   if(c!='%') goto literal;
   flags=0;
   start=fmt;
   if(*fmt==c) {
    ++fmt;
literal:
    if(out(c,dst,ctx)) return -1;
    if(dst) ++dst;
    continue;
   }
   for(;;++fmt) {
    switch(c=*fmt) {
     case '-': flags|=1; continue;
     case '+': flags|=2; continue;
     case '0': flags|=4; continue;
     case ' ': flags|=8; continue;
     case '#': flags|=16; continue;
    }
    break;
   }
   int width=-1;
   if((unsigned)(c-'0')<10) { width=func_00493B78(&fmt); c=*fmt; }
   else if(c=='*') { ++fmt; width=ARG(int); c=*fmt; }
   int prec=-1;
   if(c=='.') {
    ++fmt;
    c=*fmt;
    if((unsigned)(c-'0')<10) prec=func_00493B78(&fmt);
    else if(c=='*') { ++fmt; width=ARG(int); }
   }
   int size;
   switch(*fmt++) {
    case 'h': size=1; break;
    case 'l': size=2; break;
    case 'L': size=3; break;
    default: size=0; --fmt; break;
   }
   c=*fmt++;
   switch(c) {
    case 'X': case 'd': case 'i': case 'o': case 't': case 'u': case 'x': {
     u64 value; long signedval;
     switch(size) {
      case 1: signedval=ARG(int); value=signedval & 0xffff; break;
      case 2: case 3: value=ARG(u64); signedval=value; break;
      default: signedval=ARG(int); value=(unsigned)signedval; break;
     }
     int sign=0;
     if(signedval<0 && (c=='d'||c=='i'||c=='t')) { value=-signedval; sign='-'; }
     int n;
     switch(c) {
      case 'd': case 'u': n=func_00493918(out,&dst,ctx,flags,width,prec,value,sign,0); break;
      case 't': n=func_00493918(out,&dst,ctx,flags,width,prec,value,sign,1); break;
      case 'i': case 'o': n=func_00493A28(out,&dst,ctx,flags,width,prec,value,sign); break;
      case 'X': case 'x': n=func_00493AB0(out,&dst,ctx,flags,width,prec,value,c); break;
      default: n=0; break;
     }
     if(n<0) return -1;
     total+=n;
     break;
    }
    case 'c': if(out(ARG(int),dst,ctx)) return -1; if(dst) ++dst; break;
    case 's': if(func_00493468(out,&dst,ctx,flags,width,prec,ARG(const char*))<0) return -1; break;
    case 'E': case 'G': case 'e': case 'f': case 'g': args+=8; break;
    case 'n': case 'p': args+=4; break;
    default: fmt=start; break;
   }
  }
 }
 if(dst) *dst=0;
 return total;
}
