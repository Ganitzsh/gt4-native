/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 00436640 (matched in the decomp): applies the race display mode (option block +0x16B0): 0
 * normal (fields: the display object in interlaced field mode, 640 x 448), 1 480p (whole 640 x 448 frames), 2
 * 1080i (576 x 960). Not the original's: with the host's display.race_frames = progressive (gt4_game_option(3) = 1),
 * the normal mode is drawn as 480p (docs/NATIVE_V2.md, step 4). Returns whether a mode other than normal was set. */
struct S{char pad[0x20];int mode;char data[0x168c];int state;};struct G{char pad[0x20];int val;};extern "C" G D_006184D0;
extern "C" void func_001074D0(G*,int,int,int,int),func_004AB040(int),func_004A1638(int);extern "C" int func_004390D0(void*);
extern "C" int gt4_game_option(int id); /* runtime/sdk.cpp: the host's options */
extern "C" int func_00436640(S*self){
  int state=self->state;
  if(state==0&&gt4_game_option(3)==1)state=1;
  switch(state){case 1:func_001074D0(&D_006184D0,2,6,640,448);func_004AB040(23);break;case 2:func_001074D0(&D_006184D0,2,7,576,960);D_006184D0.val=func_004390D0((char*)self+0x24);if(self->mode==1)func_004A1638(23);else func_004AB040(23);break;case 0:default:func_001074D0(&D_006184D0,1,0,640,448);func_004AB040(23);break;}return self->state!=0;}
