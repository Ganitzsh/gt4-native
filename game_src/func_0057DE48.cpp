// Native correction of the provisional draft of 0057DE48 (the game's vprintf core, build/swarm/v2/0057de48_best.cpp):
// the EE spills variadic arguments in 8-byte register slots; the native va_list (compat/game/gt4_prelude.h)
// steps 4 bytes for int, char, pointer and %F's raw float bits, 8 for 64-bit long and double.
typedef signed char s8;
typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned long u64;
typedef long s64;
struct Stream {
    virtual ~Stream();
    virtual int format(s8 **fmt,int *count,int flags,int width,int precision,int length,int c);
    virtual int put(int c);
};
extern "C" int func_0057DDF0(s8 **) throw();
extern "C" int func_0057F260(const s8 *) throw();
extern u64 D_006CC9C0[];
extern u64 D_006CCBD8[];
extern u8 D_006CCA60[];
extern s8 D_006CCA80[];
#define EMIT(c) if((result=out->put(c))==-1) return result; ++count
extern "C" int func_0057DE48(Stream *out,s8 *fmt,u8 *args) {
    int count=0;
    s8 buffer[634];
    int c,next,result;
    while(c=fmt[0],next=fmt[1],++fmt,c!=0) {
        if(c!='%') { EMIT(c); continue; }
        if(next==c) { result=out->put('%'); if(result==-1)return result; ++fmt; ++count; continue; }
        int flags=0,width=0,precision=-1;
        s8 *start=fmt;
        int length=0;
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
        if((u32)(c-'0')<10) width=func_0057DDF0(&fmt);
        else if(c=='*') {
            width=*(int *)args; args+=4;
            if(width<0) { width=-width; flags|=2; }
            ++fmt;
        }
        if(*fmt=='.') {
            ++fmt;
            if((u32)(*fmt-'0')<10) precision=func_0057DDF0(&fmt);
            else if(*fmt=='*') { precision=*(int *)args; args+=4; ++fmt; }
            if(precision<0) precision=0;
        }
        switch(*fmt) {
            case 'h': length=1; ++fmt; break;
            case 'l': length=2; ++fmt; if(*fmt=='l') { length=3; ++fmt; } break;
            case 'L': length=3; ++fmt; break;
        }
        c=*fmt++;
        int sign=0,prefix=0,fixed=0,base;
        int exponent_char;
        int size,zeros,right,left,nprefix;
        s8 *text;
        switch(c) {
            case 'c': { int ch=(s8)*(int *)args; args+=4; EMIT(ch); continue; }
            case 's': {
                text=*(s8 **)args; args+=4;
                if(!text) text=D_006CCA80;
                size=func_0057F260(text);
                if(precision>=0 && precision<size) size=precision;
                left=0; zeros=0; right=0; nprefix=0;
                goto emit_integer;
            }
            case 'o': base=0; prefix=(flags&16)?0x3001:0; goto integer;
            case 'p': c='x'; length=0; flags|=16;
            case 'x': case 'X':
                if(flags&16)prefix=(c<<16)|0x3002;
                base=2; goto integer;
            case 't': fixed=1;
            case 'd': case 'i': sign=-1;
            case 'u': base=1;
            integer: {
                u64 value;
                s64 signed_value;
                if(length==0) { value=*(u32 *)args; signed_value=*(int *)args; args+=4; }
                else if((u32)(length-2)<2) { value=*(u64 *)args; signed_value=value; args+=8; }
                else if(length==1) { signed_value=*(int *)args; value=signed_value&65535; args+=4; }
                else { signed_value=0; value=0; }
                if(sign<0) {
                    sign=(flags&2)?'+':((flags*4)&32);
                    if(signed_value<0) { value=-signed_value; sign='-'; }
                }
                size=0;
                switch(base) {
                    case 0: case 2: {
                        int mask=base==0?7:15;
                        int shift=base==0?3:4;
                        u8 *digits=D_006CCA60;
                        if(base==2 && c=='X') digits+=16;
                        s8 *p=buffer+633;
                        s8 *end=p;
                        do { *p--=digits[(int)value&mask]; value>>=shift; } while(value);
                        size=end-p;
                        break;
                    }
                    case 1: {
                        int n=1;
                        u64 *power=D_006CC9C0+1;
                        while(value>=*power) { ++n; ++power; if(n>=19)break; }
                        size=n;
                        s8 *dot=0;
                        if(fixed) { size=n<precision?precision:n; dot=buffer+634-precision-1; precision=-1; ++size; }
                        int i=size-fixed-1;
                        s8 *p=buffer+634-size;
                        for(;i>=0;--i) {
                            u64 power=D_006CC9C0[i];
                            int digit=0;
                            while(value>=power) { value-=power; ++digit; }
                            if(p==dot) *p++='.';
                            *p++=digit+'0';
                        }
                        break;
                    }
                }
                zeros=size<precision?precision-size:0;
                right=0; left=0; nprefix=prefix&255;
                text=buffer+634-size;
                goto emit_integer;
            }
            emit_integer: {
                int total=(sign!=0)+size+(zeros+nprefix);
                if(total<width) {
                    int extra=width-total;
                    if(flags&4)zeros+=extra;
                    else if(!(flags&1))left=extra;
                    else right=extra;
                }
                while(left>0) { EMIT(' '); --left; }
                if(sign) { EMIT(sign); }
                for(int i=1;i<=nprefix;++i) { EMIT(prefix>>(i*8)); }
                while(zeros>0) { result=out->put('0'); --zeros; if(result==-1)return result; ++count; }
                while(size>0) { result=out->put(*text++); --size; if(result==-1)return result; ++count; }
                while(right>0) { EMIT(' '); --right; }
                continue;
            }
            case 'E': case 'e': exponent_char=c; goto floating_default;
            case 'F': case 'f': exponent_char=0;
            floating_default:
                if(precision<0)precision=6;
                goto floating;
            case 'G': case 'g':
                exponent_char=-1;
                if(precision<=0)precision=1;
                flags|=64;
            floating: {
                s64 bits;
                if(c!='F') { bits=*(s64 *)args; args+=8; }
                else {
                    u32 v=*(u32 *)args; args+=4;
                    bits=(v&0x7fe00000)?(((s64)v<<29)+((v>>31)?(0xc80UL<<52):(0x380UL<<52))):0;
                }
                sign=(flags&2)?'+':((flags*4)&32);
                if(bits<0) { sign='-'; bits=(u64)(bits*2)>>1; }
                int exponent=0;
                if(bits) {
                    int lo=0,hi=633,mid=316;
                    while(bits!=D_006CCBD8[mid]) {
                        if((u64)bits>=D_006CCBD8[mid])lo=mid; else hi=mid;
                        mid=(lo+hi)>>1;
                        if(lo==mid) break;
                    }
                    exponent=mid-324;
                }
                if(exponent_char<0) {
                    if(exponent<-4 || exponent>=precision)exponent_char=c-2;
                    else exponent_char=0;
                }
                int point=1,n=precision+1;
                if(!exponent_char) {
                    if(exponent<0)exponent=0;
                    point=exponent+1;
                    n+=exponent;
                }
                int binary_exp=(bits>>52)&2047;
                u64 mantissa=bits&(~0UL>>12);
                if(binary_exp)mantissa|=1UL<<52;
                int power_index=exponent+324;
                if(power_index<n)n=power_index;
                int significant=0,used=0;
                u64 *power=D_006CCBD8+power_index;
                while(n>0) {
                    u64 v=*power;
                    int digit=0;
                    u64 frac=v&(~0UL>>12);
                    int e=(v>>52)&2047;
                    int shift=binary_exp-e;
                    if(e)frac|=1UL<<52;
                    if(shift>=0) {
                        mantissa<<=shift;
                        binary_exp-=shift;
                        while(mantissa>=frac) { mantissa-=frac; ++digit; }
                        if(mantissa && mantissa<=(~0UL>>12)) {
                            do { mantissa*=2; --binary_exp; } while(mantissa<=(~0UL>>12));
                        }
                    }
                    if(used==point) { significant=used; buffer[used++]='.'; }
                    buffer[used++]=digit+'0';
                    if(digit)significant=used;
                    --n; --power;
                }
                left=0; zeros=0; int exp_size;
                if(!exponent_char) {
                    if(precision || (flags&16)) { if(point==significant)++significant; }
                    size=(exponent+precision+2)-significant;
                    exp_size=0;
                } else {
                    size=0;
                    if(!(flags&64))size=precision+2-significant;
                    else if(flags&16) { if(significant==1)++significant; }
                    exp_size=(u32)(exponent+99)>=199?5:4;
                }
                right=0;
                int total=(sign!=0)+exp_size+(size+significant);
                if(total<width) {
                    int extra=width-total;
                    if(flags&4)zeros=extra;
                    else if(!(flags&1))left=extra;
                    else right=extra;
                }
                while(left>0) { EMIT(' '); --left; }
                if(sign) { EMIT(sign); }
                while(zeros>0) { EMIT('0'); --zeros; }
                text=buffer;
                while(significant>0) { EMIT(*text++); --significant; }
                while(size>0) { EMIT('0'); --size; }
                if(exponent_char) {
                    result=out->put(exponent_char); if(result==-1)return result;
                    int e=exponent,es='+';
                    if(e<0) {e=-e;es='-';}
                    result=out->put(es); if(result==-1)return result;
                    int hundreds=e/100,rem=e%100;
                    if(hundreds) { EMIT(hundreds+'0'); }
                    result=out->put(rem/10+'0'); if(result==-1)return result;
                    result=out->put(rem%10+'0'); if(result==-1)return result;
                    count+=4;
                }
                while(right>0) { EMIT(' '); --right; }
                continue;
            }
            default:
                if(!out->format(&fmt,&count,flags,width,precision,length,c))fmt=start;
                continue;
        }
    }
    return count;
}
