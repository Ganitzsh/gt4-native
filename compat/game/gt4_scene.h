/* The scene records as game code (gcc 2.9, C or C++) writes them: include/gt4/gfx.h namespace gt4::gfx::scene restated
 * in plain C (unsigned int / unsigned short / unsigned char / float only, no 8-byte members), so the layouts are the
 * same under gcc 2.9 i386 and MSVC x64. tests/scene_records.cpp checks every size and offset against gfx.h.
 * Sent through gt4_gfx_scene() (compat/game/gt4_gfx.h) only when gt4_scene_on(); docs/modern/CONTRACTS.md (a). */
#ifndef GT4_SCENE_H
#define GT4_SCENE_H

#define GT4_SCENE_VERSION 1

enum {
    GT4_SCENE_FRAME_BEGIN = 1, GT4_SCENE_FRAME_END = 2, GT4_SCENE_VIEW_BEGIN = 3, GT4_SCENE_VIEW_END = 4,
    GT4_SCENE_INSTANCE = 5, GT4_SCENE_LIGHT = 6, GT4_SCENE_EMITTER = 7, GT4_SCENE_MARKER = 8, GT4_SCENE_VEHICLE = 9,
    GT4_SCENE_PROJECTION = 10
};

struct gt4_scene_prefix {
    unsigned short kind;      /* GT4_SCENE_* */
    unsigned short version;   /* GT4_SCENE_VERSION */
};

struct gt4_scene_frame_begin {
    struct gt4_scene_prefix prefix;
    unsigned int frame;       /* filled by the runtime */
    unsigned int vblank;      /* filled by the runtime */
    unsigned int flags;       /* 1 field, 2 odd field */
};

struct gt4_scene_frame_end {
    struct gt4_scene_prefix prefix;
    unsigned int frame;
    unsigned short views, instances;
    unsigned short lights, emitters;
    unsigned short projections, reserved;
};

/* roles: 1 main, 2 mirror, 3 env face, 4 reflection, 5 showroom, 6 photo, 7 HUD, 8 menu, 15 other;
   flags: 1 cut, 2 inset, 4 camera known */
struct gt4_scene_view_begin {
    struct gt4_scene_prefix prefix;
    unsigned short view;
    unsigned char role;
    unsigned char flags;
    unsigned int pass;
    unsigned short projection;
    unsigned short reserved0;
    float world_to_view[4][4];
    unsigned short scissor[4];
};

struct gt4_scene_view_end {
    struct gt4_scene_prefix prefix;
    unsigned short view;
    unsigned short instances;
};

struct gt4_scene_projection {
    struct gt4_scene_prefix prefix;
    unsigned short id;
    unsigned short view;
    float view_to_clip[4][4];
    float viewport_scale[4];   /* the batch's qword 197, as the last 0049E548 write left it */
    float viewport_offset[4];  /* qword 198 */
    float clamp[4];            /* qword 199 */
};

/* flags: 1 lighting, 2 skinned, 4 object known, 8 car body, 16 wheel, 32 course, 64 sky */
struct gt4_scene_instance {
    struct gt4_scene_prefix prefix;
    unsigned short view;
    unsigned short flags;
    unsigned int serial;
    unsigned int set;
    unsigned short model;
    unsigned char lod, lod_count;
    unsigned int cmd;
    unsigned int entrant;
    float object_to_view[4][4];
    float axis_scale[3];
    float shape_scale;
    unsigned short items;
    unsigned short matrix_set;
    unsigned short section;
    unsigned short projection;
    unsigned int reserved;
};

struct gt4_scene_light {
    struct gt4_scene_prefix prefix;
    unsigned short view;
    unsigned char kind, source;
    unsigned int id;
    unsigned int entrant;
    float position[3];
    float width;
    float direction[3];
    float height;
    float colour[3];
    float intensity;
    float range;
    float cos_inner, cos_outer;
    unsigned int reserved;
};

struct gt4_scene_emitter {
    struct gt4_scene_prefix prefix;
    unsigned char kind, flags;
    unsigned short count;
    unsigned int id;
    unsigned int entrant;
    float position[3], size;
    float velocity[3], rate;
    float colour[4];
    float lifetime, spread;
    unsigned short view;
    unsigned short reserved0;
    unsigned int reserved1;
};

struct gt4_scene_marker {
    struct gt4_scene_prefix prefix;
    unsigned short marker;
    unsigned short view;
};

struct gt4_scene_wheel {
    float contact[3];
    float load;
    float slip_ratio, slip_angle;
    float force_lateral, force_longitudinal;
    float spin;
    float compression;
    unsigned char surface, on_ground, pad0, pad1;
    float reserved;
};

struct gt4_scene_vehicle {
    struct gt4_scene_prefix prefix;
    unsigned char entrant;
    unsigned char flags;
    unsigned short valid;
    float body_to_world[4][4];
    float velocity[3], speed;
    float angular_velocity[3], rpm;
    float throttle, brake, clutch, steer;
    unsigned char gear, lamps, pad0, pad1;
    float steering_torque;
    struct gt4_scene_wheel wheel[4];
    unsigned int reserved[2];
};

#endif
