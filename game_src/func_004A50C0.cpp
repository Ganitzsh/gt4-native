/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 004A50C0 (from its source): builds a 6-quadword transfer header and sends it and the data on
   DMA channel 2 (GIF); without the `sync` before the transfer (native memory is coherent). */
extern "C" void func_004A0F08(int) throw();
extern "C" void func_004A0F38(int) throw();
extern "C" int func_004A0F68(int) throw();
extern "C" void func_004A1140(int,void *,int) throw();
extern "C" int func_004A4CA8(void *,int *,int,int,int,int) throw();
extern "C" void func_005ADF20(int);
extern "C" void func_004A50C0(unsigned int a, int b, int c, int d, int e, int f, int g, void *data) {
 struct Packet { char buf[0x60]; int desc; char pad[0xc]; } p;
 p.desc = ((a >> 6) & 0xffff) | (((c >> 6) & 0xff) << 16) | (b << 24);
 int n = func_004A4CA8(&p,&p.desc,d,e,f,g);
 if (n) {
  func_005ADF20(0);
  func_004A0F08(2);
  func_004A1140(2,&p,6);
  func_004A1140(2,data,n);
  func_004A0F68(2);
  func_004A0F38(2);
 }
}
