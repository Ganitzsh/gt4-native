/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native correction of the draft of 00396F00 (CourseData::render_course_2; drafts/00396f00): with no model list
 * it renders the whole model set, ModelSet2::render(model, arg) (00455528), and the original leaves its own $a1,
 * the list, NULL on that path (00397048 calls it so when the object has no list), in $a1 for `arg`, which
 * ModelSet2::begin and ModelSet2::_render take. The draft passed the model only: natively they got stack garbage
 * (tools/notpassed_triage.py). Passed explicitly.
 * A11a: the BillboardSet's draws pause the course flag (gt4_scene_course_pause): its billboards face the camera, so their
 * objectToView is not the view's camera. Nothing is sent while scene records are off. */
#include "gt4_gfx.h"
#include "spr.h"
typedef unsigned short u16;
struct Obj { char pad[4]; void *model; char pad2[0x78-8]; void *bill; };
extern "C" void func_00450558(void *);
extern "C" void func_004506B8(void *);
extern "C" void func_004506F0(void *,int);
extern "C" void func_00450918(void *);
extern "C" void func_00455370(void *,void *);
extern "C" void func_004554D0(void *);
extern "C" void func_00455528(void *, void *);
extern "C" void func_00455708(void *,int,void *);
extern "C" void func_00396F00(Obj *p,u16 *list,int flags) {
 if(list) {
  int n=*list++;
  if(p->model) {
   func_00455370(p->model,0);
   if(n) { u16 *it=list; int remaining=n;
   while(remaining) { func_00455708(p->model,*it++,0); remaining--; } }
   func_004554D0(p->model);
  }
  if(p->bill && (flags&1)) {
   gt4_scene_course_pause(spr_base(), 1);
   func_00450558(p->bill);
   if(n) { u16 *it=list;
   for(int j=0;j<n;j++) { func_004506F0(p->bill,*it++); } }
   func_004506B8(p->bill);
   gt4_scene_course_pause(spr_base(), 0);
   return;
  }
 } else {
  if(p->model) func_00455528(p->model, list);   /* list is NULL here, as the original's $a1 */
  if(p->bill) {
   gt4_scene_course_pause(spr_base(), 1);
   func_00450918(p->bill);
   gt4_scene_course_pause(spr_base(), 0);
  }
 }
}
