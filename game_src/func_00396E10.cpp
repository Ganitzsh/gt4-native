/* Native copy of 00396E10 (CourseData::render_course: the mirror's, split screen's and RacePS2Base::render_course's course)
 * from gt4-decomp's src/race/unit_00394D48/CourseData__render_course.cpp, the source the native game compiled until now,
 * with its Instances flagged kInstCourse (docs/modern/CONTRACTS.md, A11a). Nothing is sent while scene records are off. */
#include "gt4_gfx.h"
#include "spr.h"
struct Object{virtual float d0(float);virtual float d1(float);virtual float d2(float);virtual float x(float,void*,void*,int);virtual float y(float);virtual float z(float);};
extern "C" void** VisionList__getVoronoi(void*,float,float,float);extern "C" void CourseData__render_course_2(void*,void*,int);
static inline void* convert(void* source,float x,float y,float z){void* result=0;if(source)result=*VisionList__getVoronoi(source,x,y,z);return result;}
extern "C" void CourseData__render_course(void* self,void* source,Object* object,int mode){
    gt4_scene_course(spr_base(), 1);   /* the course's draws (kInstCourse, A11a) */float x=object->x(0.0f,source,object,mode);/* $a1-$a3 stay in place for the callee, so they are passed on. */float y=object->y(0.0f);float z=object->z(0.0f);CourseData__render_course_2(self,convert(source,x,y,z),mode);
    gt4_scene_course(spr_base(), 0);
}
