/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native copy of 0044E660 (the car's environment map: three passes, first in render_world) from gt4-decomp's
 * src/engine/unit_0044D510/func_0044E660.cpp, the source the native game compiled until now, with each pass's scene
 * records (docs/modern/CONTRACTS.md a.4, a.5.7, spike R1): a kViewEnvFace view per pass (`pass` = 0, 1, 2), opened once
 * the pass's camera is loaded and closed after the face's scene; the face's copy into the shared env map (an
 * orthographic Cmd sampling the face's target) is a kViewEnvCopy view of its own (A6: inside the face's view it split
 * every face by rule 4). The camera is loaded in 0044E528, whose only caller this is: it is copied here as
 * envFaceScene, unchanged but for the ViewBegin after the camera's +0x18. Nothing is sent while scene records are
 * off. */
#include "gt4_gfx.h"
#include "spr.h"
struct Buf { int w0, w4; char pad[56]; };
struct E { short delta, index; void (*fn)(void *, float); };
struct Cam { char pad[0x58]; float x,y,z; };
struct Self { char pad[0x18]; void *p18; char pad2[0x80-0x1c]; void *p80; };
extern char D_006A7D00[];
extern "C" {
void func_00105250(void *);
void func_00105280(void *,int);
void func_001053F8(void *,int);
void func_00105460(void *,int,int);
int func_00105528(void *);
void func_001056A0(void *);
void func_00105708(void *);
void func_00105728(void *);
int func_00105848(void *);
void func_0049CC38(int);
void func_0049CE40(int);
void func_0049CE68(int);
int func_0049D308(void *);
void func_004A1638(int);
void func_004A2980(int);
void func_004A29A8(int);
void func_004A3078(int);
void func_004A3230(int);
void func_004A4910();
void RaceEntryCar__update(int);
void func_004A53F8();
void func_004A5400();
void pglPushAttrib(int);
void func_004A5688();
void func_004A59B8(float,float,float,float,float,float,float);
void func_004A5A90(float,float,float,float,float,float);
void func_004A60E0(float,float,float,float);
void func_004A6100(float,float,float,float);
void func_004A7454();
void func_004A74A0(void);
void func_004A7988(float);
void func_004A79B0(float);
void func_004A79D8(float);
void func_004AB040(int);
void ModelSet2__begin(void *, void *);
void ModelSet2__end(void *);
void ModelSet2__render(void *, void *);
int ModelSet2___render(void *, int, void *);
}
extern "C" void func_00397ED8(void *);
extern "C" void **VisionList__getVoronoi(void *,float,float,float);

/* 0044E528: one pass's scene (sky, then the course near the camera), from gt4-decomp's
 * src/engine/unit_0044D510/func_0044E528.cpp. */
struct FaceCam { char pad[0x28]; float f28; char pad2[0x48 - 0x2C]; float f48; };
struct FaceSelf { char pad[0x14]; void *f14; char pad2[0x5C - 0x18]; void *f5C; char pad3[0x80 - 0x60]; FaceCam *f80; };
struct FaceObj { E *vtbl; };
static void envFaceScene(FaceSelf *self, FaceObj *obj, unsigned short *list, int face) {
    func_004A1638(5);
    func_004AB040(4);
    func_004A3230(3);
    func_004A53F8();
    {
        E *e = (E *)((char *)obj->vtbl + 0x18);
        e->fn((char *)obj + e->delta, 0.0f);
    }
    gt4_scene_view_begin(spr_base(), 3, 4, (unsigned)face);   /* kViewEnvFace, kViewCameraKnown: the pass's camera is set 0 now */
    func_004A53F8();
    func_004A74A0();
    func_004A7988(self->f80->f48);
    func_004A79B0(self->f80->f28);
    ModelSet2__render(self->f5C, 0);
    func_004A5400();
    func_004A3230(2);
    gt4_scene_course(spr_base(), 1);   /* the face's course list (f14; the sky above is f5C): kInstCourse, A11a */
    if (list != 0) {
        int i = *list;
        unsigned short *p = list + 1;
        ModelSet2__begin(self->f14, 0);
        i = (i + 0xFFFF) & 0xFFFF;
        while (i != 0xFFFF) {
            unsigned short a = *p;
            p++;
            ModelSet2___render(self->f14, a, 0);
            i = (i + 0xFFFF) & 0xFFFF;
        }
        ModelSet2__end(self->f14);
    } else {
        ModelSet2__render(self->f14, 0);
    }
    gt4_scene_course(spr_base(), 0);
    func_004A5400();
}

extern "C" void func_0044E660(void *target, Self *scene, Cam *camera, int slot, int width, int height) {
  pglPushAttrib(0x887);
  func_00397ED8(scene->p80);
  func_001056A0(target);
  func_004A3078(0x80000000); func_004A3230(1);
  Buf bufferA,bufferB;
  func_00105250(&bufferA); func_00105250(&bufferB);
  bufferA.w4=0; func_00105460(&bufferA,width,height);
  bufferB.w4=0x31; func_00105460(&bufferB,width,height);
  if (slot<0) { func_00105528(&bufferA); func_00105528(&bufferB); }
  else { func_001053F8(&bufferA,slot); func_001053F8(&bufferB,slot+func_00105848(&bufferA)); }
  void *visibleList=0;
  if (scene->p18) visibleList=*VisionList__getVoronoi(scene->p18,camera->x,camera->y,camera->z);
  int i=0;
  do {
   float pass=(float)i;
   const int face=i;
   RaceEntryCar__update(1); i++; func_004A7454();
   func_004A59B8(-0.5f,0.5f,-0x1.81ef30p-1f,0x1.cf1f14p-1f,0x1.47ae14p-8f,200.0f,0x1.4779a6p-3f);
   RaceEntryCar__update(0); func_004A7454();
   func_004A7988(0x1.3fe668p+6f); func_004A79D8(pass*-120.0f);
   func_001056A0(&bufferA); func_00105708(&bufferB);
   envFaceScene((FaceSelf *)scene,(FaceObj *)camera,(unsigned short *)visibleList,face);
   gt4_scene_view_end(spr_base());   /* the face's scene is drawn; its copy into the env map is a view of its own */
   func_00397ED8(scene->p80); func_004A4910();
   func_001056A0(target); func_00105728(&bufferA);
   RaceEntryCar__update(1); func_004A7454();
   func_004A5A90(-0x1.ccccccp-1f,0x1.ccccccp-1f,-0x1.ccccccp-1f,0x1.ccccccp-1f,-1.0f,1.0f);
   RaceEntryCar__update(0); func_004A7454();
   func_004A79D8(240.0f-pass*120.0f);
   gt4_scene_view_begin(spr_base(), 9, 4, (unsigned)face);   /* kViewEnvCopy, kViewCameraKnown: the copy's ortho set 0 */
   func_004AB040(5); func_004AB040(4); func_004AB040(3); func_004AB040(15);
   func_004A29A8(0); func_004A2980(0xffffff);
   func_0049CE68(0); func_0049CE40(0); func_0049CC38(0); func_0049D308(D_006A7D00);
   gt4_scene_view_end(spr_base());   /* the face is copied */
   func_004A1638(17); func_004A29A8(1); func_004A2980(-1);
  } while(i<3);
  func_004A4910(); func_004A5688();
  func_00105280(&bufferB,2); func_00105280(&bufferA,2);
}
