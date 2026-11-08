/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native copy of 00396F00 (CourseData::render_course_2: the course's models, then its BillboardSet) from gt4-decomp's
 * src/race/unit_00394D48/CourseData__render_course_2.cpp, the source the native game compiled until now, with the
 * billboards out of kInstCourse (A11a): they face the camera, so their objectToView is not the view's camera, and the
 * course flag is paused around them (gt4_scene_course_pause). Nothing is sent while scene records are off. */
#include "gt4_gfx.h"
#include "spr.h"
typedef unsigned short u16;
struct Obj { char pad[4]; void *model; char pad2[0x78-8]; void *bill; };
extern "C" void func_00450558(void *);
extern "C" void BillboardSet__end(void *);
extern "C" void BillboardSet__render(void *,int);
extern "C" void BillboardSet__render_2(void *);
extern "C" void ModelSet2__begin(void *,void *);
extern "C" void ModelSet2__end(void *);
extern "C" void ModelSet2__render(void *, u16 *list);
extern "C" void ModelSet2___render(void *,int,void *);
extern "C" void CourseData__render_course_2(Obj *p,u16 *list,int flags) {
 if(list) {
  int n=*list++;
  if(p->model) {
   ModelSet2__begin(p->model,0);
   if(n) { u16 *it=list; int remaining=n;
   while(remaining) { ModelSet2___render(p->model,*it++,0); remaining--; } }
   ModelSet2__end(p->model);
  }
  if(p->bill && (flags&1)) {
   gt4_scene_course_pause(spr_base(), 1);
   func_00450558(p->bill);
   for(int j=0;j<n;j++) { BillboardSet__render(p->bill,list[j]); }
   BillboardSet__end(p->bill);
   gt4_scene_course_pause(spr_base(), 0);
   return;
  }
 } else {
  if(p->model) ModelSet2__render(p->model, list);
  if(p->bill) {
   gt4_scene_course_pause(spr_base(), 1);
   BillboardSet__render_2(p->bill);
   gt4_scene_course_pause(spr_base(), 0);
  }
 }
}
