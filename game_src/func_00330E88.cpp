/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 00330E88 (from its draft, checked against the original): the MMI blend loop in plain C. */
typedef unsigned int u32;
typedef unsigned char u8;
struct Image { int f0; char *data; int f8, fC, depth, height, width, f1C, channels; };

/* Blends src's pixels into dst's: each byte becomes bits 8..15 of src*alpha + dst*(256 - alpha). The original works
   a quad at a time (LQ/SQ, which ignore an address's low four bits), with the factors as eight halfwords: PEXTLB/
   PEXTUB widen the bytes, PMULTH/PMADDH multiply and add, PMFHL.LH takes the low halfwords, PSRLH 8 and PPACB pack
   them back. Its pipeline reads the next quads before it stores the current one; it stores quad k while the source
   quad after it is below the end. */
extern "C" void func_00330E88(Image *dst,int x,int y,Image *src,int alpha,int row,int sx,int sy) {
    if(src->depth != dst->depth) return;
    if(dst->height < row+src->height) return;
    int beta=256-alpha;
    int ss=src->depth*src->channels;
    int ds=src->depth*dst->channels;
    int sr=ss*src->height;
    int dr=ds*dst->height;
    u32 s=(u32)(src->data+sr*(sy*src->width+sx));
    char *dest=dst->data+dr*(x+y*dst->width);
    u32 d=(u32)(dest+ds*row);
    u32 end=s+(dr&~15)+16;
    u32 a=(u32)alpha&0xffff, b=(u32)beta&0xffff;
    for(; s+16<end; s+=16, d+=16) {
        const u8 *sp=(const u8 *)(s&~15u);
        u8 *dp=(u8 *)(d&~15u);
        u8 out[16];
        for(int i=0;i<16;i++) out[i]=(u8)((sp[i]*a+dp[i]*b)>>8);
        for(int i=0;i<16;i++) dp[i]=out[i];
    }
}
