/* compiler: ee-gcc2.9-991111 */
// Native correction of the draft build/swarm/v1/005af850_best.c (no matched source): it declares func_0057FB50 (fptodp)
// as returning double, but the soft-float helper returns the double's bits as a 64-bit integer
// (edx:eax natively, as gt4-native implements it), so the double read from st(0) was garbage.
// Declared as returning the bits here and converted back to double through a union (bit-exact).
typedef int (*Output)();
extern char D_006D2430[], D_006D2448[];
float func_0057FA48(double);
long long func_0057FB50(float); /* the double's bits */
static __inline__ double bits_to_double(long long l) { union { long long l; double d; } u; u.l = l; return u.d; }
int func_005AF678(Output,void *,int *,double);
#define OUT(c) (count += output(context,&state,(c)))
#define PREFIX() do { for (i=0;i<prefixlen;i++) OUT(prefix[i]); } while (0)
#define ZEROS() do { for (i=0;i<precision;i++) OUT('0'); } while (0)
#define TEXT() do { while (start<end) OUT(*start++); } while (0)
#define PADDING(c) do { for (i=0;i<width;i++) OUT(c); } while (0)
#define UNSIGNED_ARG() ((flags&64)? *(unsigned long *)(args+=8,args-8): ((flags&128)? *(unsigned short *)(args+=8,args-8):*(unsigned int *)(args+=8,args-8)))
#define SIGNED_ARG() ((flags&64)? *(long *)(args+=8,args-8): ((flags&128)? *(short *)(args+=8,args-8):*(int *)(args+=8,args-8)))
int func_005AF850(Output output,void *context,int state,char *format,char *args) {
 char buffer[32],prefix[32];
 char *last=&buffer[31];
 int width,prefixlen,count=0;
 int flags,precision,i,length,padchar;
 long value;
 unsigned long uvalue;
 char *cursor,*start,*end,*digits;
 float f;
 for (;*format;format= *format?format+1:format) {
  flags=0; width=0; precision=0; prefixlen=0;
  if (*format=='%') {
   for (;;) {
    ++format;
    switch (*format) {
    case '-': flags|=1; continue;
    case '+': flags|=2; continue;
    case '0': flags|=4; continue;
    case '1':case '2':case '3':case '4':case '5':case '6':case '7':case '8':case '9':
      i=0;
      do {i=i*10+*format-'0'; ++format;} while ((unsigned int)(unsigned char)*format-'0'<10);
      width=i;
      if (*format) {--format;continue;}
      goto next;
    case '.':
      ++format;flags|=32;
      if (*format=='*') {args+=8;i=*(int *)(args-8);precision=i>=-1?i:-1;continue;}
      i=0;
      while ((unsigned int)(unsigned char)*format-'0'<10) {i=i*10+*format-'0';++format;}
      precision=i>=-1?i:-1;
      if (*format) {--format;continue;}
      goto next;
    case ' ': flags|=8; continue;
    case '#': flags|=16; continue;
    case 'l': flags|=64; continue;
    case 'h': flags|=128; continue;
    case 'o':
      uvalue=UNSIGNED_ARG();cursor=last;
      if (!uvalue) {if (!(flags&32)||precision) *cursor--='0';}
      else {while(uvalue) {*cursor--=(uvalue&7)+'0';uvalue>>=3;} if(flags&16)*cursor--='0';}
      start=cursor+1;length=last-cursor;
      break;
    case 'x':
      digits=D_006D2430;
      if(flags&16) {prefix[0]='0';prefix[1]='x';prefixlen=2;}
      goto hex;
    case 'X':
      digits=D_006D2448;
      if(flags&16) {prefix[0]='0';prefix[1]='X';prefixlen=2;}
    hex:
      uvalue=UNSIGNED_ARG();cursor=last;
      if (!uvalue) {prefixlen=0;if (!(flags&32)||precision) *cursor--='0';}
      else while(uvalue) {*cursor--=digits[uvalue&15];uvalue>>=4;}
      start=cursor+1;length=last-cursor;
      break;
    case 'd':case 'i':
      value=SIGNED_ARG();cursor=last;
      if(value>=0) {if(flags&2) {prefixlen=1;prefix[0]='+';} else if(flags&8){prefixlen=1;prefix[0]=' ';}}
      else {prefixlen=1;prefix[0]='-';}
      if(!value){if(!(flags&32)||precision)*cursor--='0';}
      else {value=value>=0?value:-value;while(value){*cursor--=value%10+'0';value/=10;}}
      start=cursor+1;length=last-cursor;
      break;
    case 'u':
      uvalue=UNSIGNED_ARG();cursor=last;
      if(!uvalue){if(!(flags&32)||precision)*cursor--='0';}
      else while(uvalue){*cursor--=uvalue%10+'0';uvalue/=10;}
      start=cursor+1;length=last-cursor;
      break;
    case 'e':case 'f':
      args+=8;f=func_0057FA48(*(double *)(args-8));
      if(f==0.0f) OUT('0');
      else count+=func_005AF678(output,context,&state,bits_to_double(func_0057FB50(f)));
      goto next;
    case 's':
      args+=8;start=*(char **)(args-8);
      if(!start){output(context,&state,'(');output(context,&state,'n');output(context,&state,'u');output(context,&state,'l');output(context,&state,'l');output(context,&state,')');goto next;}
      cursor=start;length=0;
      if(flags&32) {for(i=0;i<precision && *cursor++;i++);length=i;}
      else while(*cursor){cursor++;length++;}
      break;
    case 'c':
      args+=8;value=*(char *)(args-8);cursor=last;
      flags|=256;precision=1;*cursor--=value;
      start=cursor+1;length=last-cursor;
      break;
    case 'p':
      args+=8;uvalue=*(unsigned int *)(args-8);digits=D_006D2430;cursor=last;
      if(!uvalue){if(!(flags&32)||precision)*cursor--='0';}
      else while(uvalue){*cursor--=digits[uvalue&15];uvalue>>=4;}
      prefix[0]='0';prefix[1]='x';prefixlen=2;
      start=cursor+1;length=last-cursor;
      break;
    default:
      if(!*format)goto next;
      {cursor=last;flags|=256;precision=1;*cursor--=*format;start=cursor+1;length=last-cursor;}
      break;
    }
    break;
   }
   precision=length<precision?precision-length:0;
   width=width-length-prefixlen-precision;
   end=start+length;
   padchar=' ';
   if(flags&256) {if((flags&4)&&width>0)padchar='0';}
   else if((flags&36)==4 && width>0)padchar='0';
   if(flags&1) {PREFIX();ZEROS();TEXT();PADDING(' ');}
   else if(flags&4) {
    if(padchar=='0') {PREFIX();PADDING(padchar);}
    else {PADDING(padchar);PREFIX();}
    ZEROS();TEXT();
   } else {PADDING(padchar);PREFIX();ZEROS();TEXT();}
  } else if(*format) OUT(*format);
 next:;
 }
 output(context,&state,0);
 return count;
}
