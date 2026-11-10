# Contracts every lane codes against

Status: design for review. These three contracts are agreed before code lands that depends on them. A change to
any struct here after it lands is a version bump (section (a) 6) and a note in this file.

- (a) The scene records, game to host: `include/gt4/gfx.h`, namespace `gt4::gfx::scene`.
- (b) The render-graph node interface: `platform/render/graph/render_graph.h`.
- (c) The light list shared by night lighting and the clustered lights: `include/gt4/render/light_list.h`.

References are at `main` a80cfca.

---

## (a) Scene records

### a.1 Transport

One new native record type in `gfx::RecordType` (`gfx.h:289-302`):

```cpp
kScene = 11,   // scene::Prefix, then the record of that kind (docs/modern/CONTRACTS.md)
```

- Records go through pgl's packet like the batch records: `gt4_gfx_scene(context, record, bytes)` calls the same
  `append(context, ...)` as `gt4_gfx_batch_model` (`runtime/native/pgl.cpp:365-368`). So they arrive at the host
  **in issue order with the `kBatch*`, `kState`, `kTexSet` and `kDraw` records of the same frame**.
- The frame walker's block test (`runtime/frame.cpp`, `first.type() <= gfx::kEffect`) became `<= gfx::kScene`,
  and since R6 (f90214e) `< gfx::kRecordTypeEnd`, so a new record type needs no change there. The walker sends
  `kScene` records as they are: they hold values, never guest pointers.
- The host: `GraphicsStream::records` (`platform/gs/stream.cpp:67`) gets `case gfx::kScene`, which hands the
  payload to `GsTarget::scene(const uint8_t* payload, size_t bytes)` (new virtual in `platform/gs/stream.h`, no-op
  default). `QueuedTarget` (`gs_host.cpp:306`) forwards it in order like `effect`.
- `GT4_LINK_VERSION` (`include/gt4/link/protocol.h:22`) goes from 2 to 3.
- **`kOriginParts = 12`** (R6, lane A, f90214e; link version 5): not a scene record, sent whatever
  `kOptionSceneRecords` is. **Informational since R6's re-baseline (lane B's 7f58836): the GS path draws the drawing
  origin (XYOFFSET) as the GS has it, in every mode and every preset, the scene path's `model()` jobs too, so the
  scene stage agrees with the GS path; the stream counts the records (`counters.originParts`, in the replay summary)
  and the GS path ignores them** (`GT4_GSN_JITTER=mask` restores d3a50ff's half-pixel steps, for comparisons). GT4
  sets the origin in 004A2BC8 as base + (int)((ox + sx * px) * 16) per axis, at most 15 sixteenths: ox / oy the field
  part (004A2B58, from the display mode 004A2A10; oy: 0 or half a pixel by field in types 1, 2 and 9, fixed (0 in 3,
  half a pixel in 4) in the other field modes, 0 in the frame modes; ox always 0), sx * px / sy * py the supersampling
  part (by default sx = sy = 0.5, the pixel centre, which a field mode's py = 0.5 makes a constant quarter pixel;
  004A2C58's jitter passes set it: the garage and showroom car, photo, the race's HUD gauges). The register holds only
  the sum, so our copy of 004A2BC8 sends `gfx::OriginParts` (8 bytes, 12 with its `Header`) before the origin it sets
  reaches the stream: `contexts` (bit 0 context 1, bit 1 context 2), `fieldX`/`fieldY`, `superX`/`superY` (004A2BC8's
  own value with ox = oy = 0, clamped as it clamps) and `sumX`/`sumY` (what it added to the base), all in sixteenths,
  0..15; one byte reserved, 0. An `OriginParts` in a display list called from the frame list goes out once per call,
  with that call's own origin write. Rate (lane A): about 120 a frame on the garage and 118 on a Route 5 race, mostly
  from 004A2BC8's other callers (each base or field change).
  - **Kept for B9:** the modern path's showroom and photo views need to know which draws are supersampling passes and
    by how much (one view and TAA in place of the passes, R6), and the record is the only thing that says so for the
    2D and the passes outside scene projections. If B9 takes the jitter from the scene projections instead, the record
    is retired (the `game_src` copy, the pgl hook, the stream case; `kRecordTypeEnd` and the walker stay).
  - **Why the register as is** (lane B, lane A, 2026-11-08): on the PS2 it is what the GS draws. On PCSX2's software
    renderer it is closer than the mask where they differ, on the garage car (frame-exact, 65.8 % against 30.8 %), PE
    race 2's countdown with the gauges (66.8 % against 33.2 %) and a field-mode menu page (lane C's pre-race entry
    list, `race_grid43_r6` 3,250-3,400; over the page's own pixels, its background being a moving camera: 86.4 %
    against 12.7 %); and it shows no frame-to-frame shake beyond the mask's (the ratio of the mean one-frame to
    two-frame difference over 61 frames, keep against mask, 4:3 and 16:9; a per-field shake pushes it towards 1):
    in-car (`race_grid43_r6` 4150-4210, 1x to 4x at 4:3, 2x and 3x at 16:9) 0.587-0.596, the two within 0.01; driving
    at night (`a6r_r5` 600-660, B-spec, 2x and 4x) 0.603-0.610 against 0.614-0.617; the turntable (`garage_r6`
    2600-2660, 2x and 4x) 0.579-0.590 against 0.580-0.592; `race_pe2_r` 200-260 (2x to 4x) 0.564-0.567 against
    0.566-0.570, though the player's car is idle there. Keep's ratio is at or below the mask's in every case.
    d3a50ff's shake 'above 1x' isn't reproduced on today's tree. The mask kept half-pixel steps: the field's 0/8
    survived, the constant quarter pixel went, and the passes' offsets were rounded down to half pixels. Two rules
    tried first and dropped: (origin & ~15) + super everywhere (it removed the field's line placement: nearly every
    field-mode frame moved by up to 150 levels); exact only in frame modes (the garage's passes run in a field mode,
    so it changed nothing there).
- Records are sent only when the host sets game option `kOptionSceneRecords = 5` (`runtime/runtime.h:26`):
  - **0, off**: nothing (the `original` preset).
  - **1, full** (as before): every record of this section. The host sends 1 when `sceneRecordsWanted()`
    (`render.scene != original`, `lighting.night = enhanced`, and when recording a capture with
    `GT4_HOST_RECORD_SCENE=1`, so the parity captures carry them).
  - **2, vehicles only**: else, when `input.haptics = physics` (D14). Haptics need only the `Vehicle` records, and
    full records would change every preset's timing through the tick deferral (a.5.1). In this mode
    `gt4_scene_on()` stays false: pgl's hooks send nothing, game code sends no `FrameBegin`, and the kick never
    defers the clock. While a race runs (`vehicle_live()`), the kick sends one `GT4_REC_GFX` block of its own:
    `FrameBegin` (flags `kFrameVehiclesOnly`), the `Vehicle`s (each padded to whole qwords), then `FrameEnd` (views,
    instances, lights, emitters, projections 0; `begins` 1). It is one block, so nothing comes between it and its
    `FrameEnd`, and a.5.1 holds without the deferral.

### a.2 Game-side API (`compat/game/gt4_gfx.h`) and the game's copy of the structs

```c
/* Scene records (include/gt4/gfx.h gt4::gfx::scene; the layouts restated in compat/game/gt4_scene.h): `record`
 * starts with its Prefix; `bytes` is the size of that kind's struct. Nothing is sent unless gt4_scene_on(). */
void gt4_gfx_scene(void *context, const void *record, int bytes);
unsigned int gt4_scene_on(void);   /* gt4_game_option(kOptionSceneRecords) */
/* The hooks' helpers (runtime/native/pgl.cpp; A2, A3): each builds its record from pgl's state and sends it. */
void gt4_scene_frame_begin(void *context, unsigned int flags);   /* FrameEnd: the runtime, at the kick */
unsigned int gt4_scene_view_begin(void *context, int role, int flags, unsigned int pass);
void gt4_scene_view_end(void *context);
void gt4_scene_cmd(void *context, const void *cmd);
void gt4_scene_marker(void *context, int kind);   /* MarkerKind, in the open frame (A5a) */
```

- gcc 2.9 cannot include `gfx.h` (C++17), so the structs are restated in **`compat/game/gt4_scene.h`** (plain C,
  `unsigned int` / `unsigned short` / `unsigned char` / `float` only). A host unit test (`tests/scene_records.cpp`)
  includes both and checks `sizeof` and every `offsetof`.
- **No 8-byte members**, so the layout is the same under gcc 2.9 i386 (8-byte types 4-aligned) and MSVC x64.
- Arguments are pointers and `int`; no `long`, no float arguments or results. `tools/hook_lint.py` must pass.
- The runtime fills in `FrameBegin::frame` and `FrameBegin::vblank`, drops a `FrameBegin` while a frame is open, and
  sends `FrameEnd` itself at the frame's kick (a.5.1). The game never sends `FrameEnd`.
- The helpers build whole records from pgl's state, so each hook is one call: views numbered in order with the camera
  (set 0 when the view starts) and its `Projection` (set 1, viewport SPR+0xFA0); per Cmd, set 0 and set 1 as 0049E480
  picks them by `s->current`, the scales, `items`, and a `Projection` with the viewport the batch holds (the runtime's
  copy of qwords 197..199 from the last 0049E548 batch write), sent only when it differs from the last one this frame.
  `flags` passes `kViewCameraKnown` when the hook's set 0 is the view's camera (else the view sends identity).

### a.3 Conventions for every record

- **Matrices** are `float m[4][4]`, GT4's row-vector convention: `p' = p.x * m[0] + p.y * m[1] + p.z * m[2] +
  p.w * m[3]`. Composition `A · B` applies A first. This is how `0049E480` and the VU code use them.
- **Spaces** (spike R1, `spikes/R1.md`). pgl is GL-like: 004A5348 is MatrixMode, set 0 the modelview, set 1 the
  projection. *Object*: the model's packet coordinates. *View*: set 0's output (right-handed, +y up, looking down
  -z). *Clip*: set 1's output, a GL perspective (`w' = -z`). *Window*: GS window pixels before XYOFFSET and GS Z,
  from clip by the viewport (pglViewport 0x004A2D60, the batch's qwords 197 / 198): `window = offset + scale * clip /
  w` per component (z gives GS Z, w the fog). *World*: object space of the course; course instances are drawn with
  `objectToView` equal to the view's `worldToView`.
- **Units.** GT4's units are metres: `kGameUnitsPerMetre = 1.0` (R1: GS Z is about 1.678e6 / d for the race view;
  the chase camera sits 6.0 behind and 1.7 above the car; within about 5 %, A2 may tighten it).
- **Views** are numbered from 1 per frame in `ViewBegin` order; 0 means none.
- **IDs** that must be stable across frames (`Instance::cmd` + `set`/`model`, `Light::id`, `Emitter::id`) are game
  addresses or small indices. The host never dereferences them.
- Unknown values are 0 unless the field says otherwise; flags say what is known.

### a.4 The structs (`include/gt4/gfx.h`)

```cpp
namespace gt4::gfx::scene {

inline constexpr uint16_t kVersion = 1;   // every kind's layout version in this release

enum Kind : uint16_t {
    kFrameBegin = 1, kFrameEnd = 2, kViewBegin = 3, kViewEnd = 4, kInstance = 5,
    kLight = 6, kEmitter = 7, kMarker = 8, kVehicle = 9, kProjection = 10,
};

struct Prefix {
    uint16_t kind;      // Kind
    uint16_t version;   // that kind's layout version (kVersion)
};
static_assert(sizeof(Prefix) == 4);

// --- Frame ----------------------------------------------------------------------------------------------
enum FrameFlags : uint32_t {
    kFrameField = 1, kFrameOddField = 2,   // the race's 224-line fields
    kFrameVehiclesOnly = 4,                // kOptionSceneRecords 2 (a.1): only Vehicles, no view; not a scene to draw
    kFrameLampsOn = 8,     // GT4's car-lamp switch for the course scene: the course flag word (course + 0x80, + 0x10)
                           //   bit 0x8000, as 0033EAB8 reads it to light the cars' lamps (lane A, 2026-11-09). Set on
                           //   the four Route 5 layouts and Hong Kong at dusk, and on the Arcade menus' 3D background
                           //   (drawn outside scene frames: no views, so no reader sees it); clear by day. Sent with
                           //   scene records on (a.1). The one switch for 'lamps on': A7 step 2's on state, F6's
                           //   headlight shadows, F7's beams, F9's emissive (and N3's skip, if it takes it) all read
                           //   it, so they can't disagree
};
struct FrameBegin {
    Prefix prefix;
    uint32_t frame;     // runtime: +1 per FrameBegin since the game started
    uint32_t vblank;    // runtime: the VBlank count when it was sent (logs, captures)
    uint32_t flags;     // FrameFlags
};
static_assert(sizeof(FrameBegin) == 16);

struct FrameEnd {                 // sent by the runtime at the frame's kick, never by the game (a.5.1)
    Prefix prefix;
    uint32_t frame;               // as its FrameBegin
    uint16_t views, instances;    // what gt4_gfx_scene appended this frame (the host checks its counts)
    uint16_t lights, emitters;
    uint16_t projections;
    uint16_t begins;              // FrameBegins asked for this frame, the opening one included (>= 1); 0: not counted
};
static_assert(sizeof(FrameEnd) == 20);

// --- Views ----------------------------------------------------------------------------------------------
enum ViewRole : uint8_t {
    kViewMain = 1,         // render_world's main view (0033DEA8); in split screen each half's (0033FD88, kViewSplit)
    kViewMirror = 2,       // render_back_mirror (00341E50)
    kViewEnvFace = 3,      // the car's environment map passes (0044E660 / 0044EB10); `pass` = face
    kViewReflection = 4,   // reserved: the road reflection, if B9 ever draws it as a view (today kInstReflection)
    kViewShowroom = 5,     // reserved, never sent: every jittered car view (garage, showroom, dealers) goes through
                           //   00217058 and is kViewMenu; its passes are kMarkJitterPass pairs (R6, lane A)
    kViewPhoto = 6,        // Photo Travel's preview (mPhotoViewFace virtual 70, game_src/func_001BD438.cpp), pass 0;
                           //   the shot's tile passes are B9's to decide (views inside their frames, never a
                           //   FrameBegin per pass)
    kViewHud = 7,          // 3D drawn by the HUD outside render_world (RaceRoundMeterBase 003BE3C8); never substituted
    kViewMenu = 8,         // menus' 3D (mRenderContextPS2::virtual_64 00217058: the Home garage car, small car views)
    kViewEnvCopy = 9,      // an env face's copy into the shared env map (0044E660, after the face's ViewEnd);
                           //   `pass` = face; never substituted
    kViewOther = 15,
};
enum ViewFlags : uint8_t {
    kViewCut = 1,          // camera moved discontinuously: temporal history resets. Set by the runtime's ViewBegin
                           // (A3b: the game's camera signals, and a net on the camera's own motion), on main, split,
                           // mirror and photo views, once on the new camera's first frame
    kViewInset = 2,        // drawn inside another view's target (the race mirror)
    kViewCameraKnown = 4,  // worldToView is the camera the view loaded; else identity and the host estimates
    kViewSplit = 8,        // a half of a split screen (2 Player Battle, A12): two kViewMain views a frame, drawing
                           //   into one target with disjoint scissors; `pass` is the half (0 the first drawn, top or
                           //   left; GT4 lays them out side by side at 16:9, top and bottom at 4:3) and `scissor` its
                           //   rectangle. Each half's mirror (kViewMirror) carries the half's pass and the flag too,
                           //   and each half's env views (kViewEnvFace, kViewEnvCopy: its car's map) the flag with
                           //   pass = face | half << 8, so a consumer keyed by (role, pass) keeps the cars apart.
                           //   A consumer that keeps state per view keeps it per half (temporal history, the seam)
};
struct ViewBegin {
    Prefix prefix;
    uint16_t view;               // 1.. in this frame
    uint8_t role;                // ViewRole
    uint8_t flags;               // ViewFlags
    uint32_t pass;               // role-specific, else 0 (kViewSplit: the half)
    uint16_t projection;         // the camera's Projection (set 1 as the view loaded it), sent before this record
    uint16_t reserved0;
    float worldToView[4][4];     // set 0 at the stack depth the view starts with (0 in the race, 4 on the
                                 // pre-race screen: not absolute depth 0), after the camera load
    uint16_t scissor[4];         // x0, y0, x1, y1 GS pixels, inclusive (all 0: unknown)
};
static_assert(sizeof(ViewBegin) == 88);

// --- Projections: set 1 and the viewport, sent whenever either changes (about 10 a race frame) -------------
// R1: the course is drawn in two depth ranges (near 0.1 / far 1200 and near 10 / far 120,019, same x and y),
// depth-biased objects use near 0.105 / 0.108 (003E4A28, 004524B8), the sky its own (near 1, far 1000), the
// mirror and env-map passes theirs. fovY, aspect, near and far follow from viewToClip and are not sent.
struct Projection {
    Prefix prefix;
    uint16_t id;                 // 1.. per frame, in sending order
    uint16_t view;               // the open view, 0 none (env-map passes may come before a view opens)
    float viewToClip[4][4];      // set 1 as 0049E480 used it
    // The values the batch HOLDS (the last 0049E548 write of qwords 193..200, kept by the runtime from
    // gt4_gfx_batch_write), not SPR+0xFA0 read at the Cmd: 0049E548 runs for only 27.5 % of the Cmds (R1).
    float viewportScale[4];      // qword 197: x, y in GS pixels, z to GS Z, w fog
    float viewportOffset[4];     // qword 198
    float clamp[4];              // qword 199: x, y, z always FLT_MAX; w the fog clamp (100, 30, 0 or FLT_MAX by object)
};
static_assert(sizeof(Projection) == 120);

struct ViewEnd {
    Prefix prefix;
    uint16_t view;
    uint16_t instances;          // sent inside it
};
static_assert(sizeof(ViewEnd) == 8);

// --- Instances: one per Cmd drawn by 0049D328 --------------------------------------------------------------
enum InstanceFlags : uint16_t {
    kInstLighting = 1,      // cmd->flag: a lighting chain (lit model)
    kInstSkinned = 2,       // cmd->bits & 0x80: vertices computed per frame (0049DA60)
    kInstObjectKnown = 4,   // set / model / lod come from 00455708's context
    kInstCarBody = 8,       // sphere-map bodies 0x10A8 / 0x12C8 seen in the Cmd (set by the host if not known)
    kInstWheel = 16,
    kInstCourse = 32,       // the course's world-space geometry: a Cmd drawn inside the course bracket (00396D40,
                            //   00396E10, 0044E528) at the bracket's own matrix-stack depth (A11a); objectToView =
                            //   the view's worldToView
    kInstSky = 64,
    kInstCourseObject = 128,   // a placed course object: inside the course bracket at any other matrix-stack depth
                               //   (its display program pushed a transform of its own), so its own objectToWorld
    kInstBillboard = 256,      // one batch run of BillboardSet__render 004506F0 (items 1): camera-facing quads
                               //   built every frame, objectToView = the camera (set 0); never course geometry
    kInstLightMap = 512,       // one batch run of a car's light map (0038F5D0, from render_cars): the fans of the
                               //   course lamps near the car, drawn into the car's reflection texture, not the
                               //   view's target; objectToView = set 0 as the pass set it (the car's camera,
                               //   orthographic); entrant = the car. Out of the binding (a.5.7)
    kInstReflection = 1024,    // drawn by the road reflection pass (render_reflection 003411D0), inside the main
                               //   view: into its 64 x 32 target with its own Z, and in part straight into the
                               //   main view's own target; set by the pass's context, never from the target. Out
                               //   of the binding (a.5.7)
};
struct Instance {
    Prefix prefix;
    uint16_t view;               // the open view; 0: none (counted, never substituted)
    uint16_t flags;              // InstanceFlags
    uint32_t serial;             // 1.. per frame in emission order
    uint32_t set;                // the model set (00455708's Set address), 0 unknown
    uint16_t model;              // model index in the set
    uint8_t lod, lodCount;       // detail level drawn, levels the model has
    uint32_t cmd;                // the Cmd's address: stable for static content
    uint32_t entrant;            // car 0..5 drawing it, 0xFFFFFFFF none or unknown
    float objectToView[4][4];    // set 0 as 0049E480 used it: shared vectors 28..31 when set 0 is current, else +0xED0
    float axisScale[3];          // s->scale x, y, z (+0xBD0) when byte +0xE is set, else 1, 1, 1
    float shapeScale;            // cmd->value (0049E548's scale; 0: rows not scaled)
    uint16_t items;              // the Cmd's items: exactly this many BatchRun records follow for it
    uint16_t matrixSet;          // s->current (+1) when it ran: 0, 1 or 2
    uint16_t section;            // course section (CourseData::render_model 0x00397188's model index), 0xFFFF none
                                 //   (A11b, parked with N2)
    uint16_t projection;         // the Projection current for this Cmd (set 1 and viewport), 0 none
    uint32_t reserved;           // 0 (the members above sum to 116 bytes; A1's compile found it: the record stays 120)
};
static_assert(sizeof(Instance) == 120);

// --- Lights ---------------------------------------------------------------------------------------------
enum LightKind : uint8_t { kLightPoint = 1, kLightSpot = 2, kLightDirectional = 3 };
enum LightSource : uint8_t {
    kSrcCourseGlow = 1,      // the course's lamps: the street lamps the host finds in the course meshes (F2b, not
                             //   a record), and the rare entries2[] sources whose blink is 0 (A7 step 1a)
    kSrcHeadlight = 2, kSrcTailLight = 3, kSrcBrakeLight = 4,   // every car's lamps, every frame, day and night
                             //   (A7 step 2, after each kViewMain's ViewEnd): spots, a headlight along its beam, the
                             //   tail and brake lamps backwards; range and cone 0. Intensity: headlights and tail
                             //   lamps 1 with kFrameLampsOn, else 0 (sent, so the host keeps their ids); brake lamps
                             //   GT4's level with kFrameLampsOn, else a ramp of 0.25 a frame from the car's brake
                             //   flag
    kSrcCarLightReflection = 5,   // 0x00341A30: not sent as lights (reflections; N3 handles their sprites)
    kSrcSun = 6,
    kSrcWarning = 7,         // 003B88C8's entries2[], a source with blink > 0 (warning lamps; A7 step 1a)
    kSrcCarLamp = 8,         // a car lamp whose group is not known (entries[], A7 step 1a): point, no direction
    kSrcBillboard = 9,       // a course billboard's centre and size (A7 step 1b), id = the serial of its flush's
                             //   first kInstBillboard instance, sent after the flush. A candidate, not a light: the
                             //   host keeps it as kSrcCourseGlow when that instance's material has the glow class
                             //   (F2b), else drops it
    kSrcOther = 15,
};
struct Light {
    Prefix prefix;
    uint16_t view;               // the view whose space position / direction are in; a consumer that wants world
                                 //   space goes back through that view's ViewBegin::worldToView
    uint8_t kind, source;        // LightKind, LightSource
    uint32_t id;                 // the game's light object address; a car lamp (kSrcHeadlight, kSrcTailLight,
                                 //   kSrcBrakeLight) entrant << 8 | group << 5 | index (group 0 headlights, 1 brake,
                                 //   2 tail lamps), stable while the car is in the race
    uint32_t entrant;            // 0xFFFFFFFF none
    float position[3];           // view space (point, spot)
    float width;                 // glow quad half width as the game drew it, 0 none
    float direction[3];          // view space, unit (spot, directional)
    float height;                // glow quad half height, 0 none
    float colour[3];             // the game's colour, 0..1, before its clamp
    float intensity;             // the game's intensity this frame, without the view-dependent term; 1 when none;
                                 //   warning lamps: their smooth blink factor 0..1, not thresholded
    float range;                 // view units, 0 unknown (the host picks per source)
    float cosInner, cosOuter;    // spot cone, 0 unknown
    uint32_t reserved;
};
static_assert(sizeof(Light) == 80);

// --- Emitters: once per emitter per simulation tick that spawns ---------------------------------------------
enum EmitterKind : uint8_t { kEmitSmoke = 1, kEmitDust = 2, kEmitSpark = 3, kEmitSpray = 4, kEmitExhaust = 5, kEmitOther = 15 };
enum EmitterFlags : uint8_t { kEmitWorld = 1, kEmitBurst = 2 };
struct Emitter {
    Prefix prefix;
    uint8_t kind, flags;         // EmitterKind, EmitterFlags
    uint16_t count;              // particles the game spawned this tick (0: use rate)
    uint32_t id;                 // emitter object address
    uint32_t entrant;            // 0xFFFFFFFF none
    float position[3], size;     // world space when kEmitWorld, else the view's; size in units
    float velocity[3], rate;     // units per second; particles per second
    float colour[4];             // 0..1 RGBA as the game sets it
    float lifetime, spread;      // seconds; cone half-angle in radians
    uint16_t view;               // when not kEmitWorld
    uint16_t reserved0;
    uint32_t reserved1;
};
static_assert(sizeof(Emitter) == 80);

// --- Markers ---------------------------------------------------------------------------------------------
enum MarkerKind : uint16_t {
    kMarkPostBegin = 1, kMarkPostEnd = 2,   // around render_post_effect (0x0033F890)
    kMarkHudBegin = 3, kMarkHudEnd = 4,     // around render_panel (0x0033FA40)
    kMark2DBegin = 5, kMark2DEnd = 6,       // menus: 2D drawn over a 3D view
    // GT4 effects the modern path suppresses (RISKS R11, R12). The draws between a pair can be skipped as a group.
    kMarkFlareBegin = 7, kMarkFlareEnd = 8,           // the flare pass: 003B8848 / 003B88C8 samples and what it draws from them
    kMarkGlareBegin = 9, kMarkGlareEnd = 10,          // glare 003BADF8 (inside the post-effect pair)
    kMarkLampBandsBegin = 11, kMarkLampBandsEnd = 12, // reserved, never sent: no call site (A5b); N3 uses a B6 class
    kMarkDofBegin = 13, kMarkDofEnd = 14,             // depth of field 0033E6E0 (its game_src copy): Photo Travel's
                                                      //   preview and the race's Dive view
    kMarkJitterPassBegin = 15, kMarkJitterPassEnd = 16, // one supersampling pass of 00216E78 (garage, showroom),
                                                      //   from its game_src copy; the pass index is its order in
                                                      //   the view, the count the number of pairs (R6)
};
struct Marker {
    Prefix prefix;
    uint16_t marker;             // MarkerKind
    uint16_t view;               // the view it belongs to, 0 none
};
static_assert(sizeof(Marker) == 8);

// --- Vehicle state: one per running car per frame, at the kick (fields: D6, RISKS R14; a.5 rule 4) ----------
enum VehicleFields : uint16_t {
    kVehTransform = 1, kVehVelocity = 2, kVehEngine = 4, kVehInputs = 8, kVehWheelContact = 16,
    kVehWheelForces = 32, kVehWheelSlip = 64, kVehSteeringTorque = 128, kVehWheelStroke = 256,
};
struct Wheel {
    float contact[3];            // world space
    float load;                  // N
    float slipRatio, slipAngle;  // -, radians
    float forceLateral, forceLongitudinal;   // N
    float spin;                  // rad/s
    float compression;           // 0..1; 0 and unflagged until a car's suspension travel is known
    uint8_t surface, onGround, pad0, pad1;
    float strokeSpeed;           // m/s, + compressing (R14: W + 0x98); kVehWheelStroke (was reserved, 0)
};
static_assert(sizeof(Wheel) == 48);
struct Vehicle {
    Prefix prefix;
    uint8_t entrant;             // 0..5
    uint8_t flags;               // 1 player, 2 AI, 4 replay. Not filled until R14 finds the player bit; until
                                 //   then consumers take entrant 0 as pad 1's car and 1 as pad 2's (split
                                 //   screen), as seen in Arcade and GT mode
    uint16_t valid;              // VehicleFields that are filled
    float bodyToWorld[4][4];     // rows as stored are the body's axes in world space, row 3 its position:
                                 //   row 0 forward, along the car's motion (speed is the velocity along it),
                                 //   row 1 right, row 2 up (from the model's +y; its sign not yet measured
                                 //   against world up); world = p0 * row 0 + p1 * row 1 + p2 * row 2 + row 3.
                                 //   GT4's model space: a model point (x, y, z) is the body point (-z, x, y) plus
                                 //   a per-car offset along row 0 (the model's origin against the body's: 0 to
                                 //   -0.39 m on the six cars measured), so the model's front is at negative z
                                 //   (lanes D and A, 2026-11-09: the motion, and the lamps' groups)
    float velocity[3], speed;    // world units / s; m/s
    float angularVelocity[3], rpm;
    float throttle, brake, clutch, steer;   // 0..1; steer -1..1
    uint8_t gear, lamps, pad0, pad1;        // lamps: 1 headlights, 2 brake, 4 reverse
    float steeringTorque;        // N m at the rack
    Wheel wheel[4];              // FL, FR, RL, RR
    uint32_t reserved[2];        // 0 (the members above sum to 320 bytes; A1's compile found it: the record stays 328)
};
static_assert(sizeof(Vehicle) == 328);

}  // namespace gt4::gfx::scene
```

Where each record is sent (tickets in PLAN.md):

| Record | Sent by | Ticket |
|---|---|---|
| `FrameBegin` | start of `render_world` (`game_src/func_0033DEA8.cpp`; the 3 env-map passes come first inside it); menus: mRenderContextPS2::virtual_64 (00217058). The first since the last kick opens the frame; the runtime drops the others (a.5.1) | A3 |
| `FrameEnd` | the runtime, at the frame's kick (004A11F8, `runtime/frame.cpp`), after `walk(list)`, when a frame is open | A3 |
| `ViewBegin` / `ViewEnd` | main: after 0033EAB8's camera load (a `game_src` copy of 0033EAB8, or a hook at the camera's +0x18 call), `ViewEnd` before `render_back_mirror`; mirror: `game_src/func_00341E50.cpp` (after every main-view instance, so views never nest); env-map passes 0044E660 (before the main view; each face's `ViewEnd` right after its scene, before 001056A0 binds the env target, and the face's copy into the env map in a `kViewEnvCopy` view of its own); menus 00217058 (`kViewMenu`); the HUD's 3D widget 003BE3C8 (`kViewHud`); showroom and Photo Travel per R6 | A3, A6 |
| `Projection` | `game_src/func_0049D328.cpp`, before the `Instance`, when set 1 (as 0049E480 picks it) or the viewport / clamp the batch holds (the runtime's copy of the last qwords 197..199 written by 0049E548) differ from the last `Projection` sent this frame (a 64 + 48-byte compare); also for each view's camera load, before its `ViewBegin` | A2, A3 |
| `Instance` | `game_src/func_0049D328.cpp`, after 0049E480 / 0049E548, before the item loop; A and B read as 0049E480 picks them (by `s->current`) even when it does not run (R1: it recomposed for 16 % of the Cmds); and one per batch run inside `BillboardSet__render` 004506F0's billboard context, flagged `kInstBillboard`, just before the run (A7 step 1b); one per batch run inside the car light map pass 0038F5D0, flagged `kInstLightMap`, the context opened around each 0038F5D0 call by the `render_cars` copy (`game_src/func_00340950.cpp`, entrant = car `+0x544`) (A6) | A2, A6, A7 |
| object identity | 00455708 (draft) sets a runtime-global "model context" that A2 reads | A4 |
| `Light` | step 1a: 003B88C8's `entries2[]` (course lamps and warning lamps) and `entries[]` (car lamps), read right before 003B88C8 draws them (in 0033F2E8, inside the flare bracket; the course sources are added after 0033EAB8's method 0x3F0), tagged with the main view's id (a.5 rule 4); step 1b: each billboard of `BillboardSet__render` as a `kSrcBillboard` candidate (its entry's world position and item size through set 0); step 2: headlight spots from the car body (A9). A lamp named both by `entries2[]` and as a billboard gets one light (PLAN F2b) | A7 |
| `Emitter` | 0x003E7A58, 0x003C1430, 0x003C2780 | A8 |
| `Marker` | `kMarkPost*` at the start and end of render_post_effect (`game_src/func_0033F890.cpp`, virtual 105); `kMarkHud*` at the start and end of RaceBasic::render_panel (`game_src/func_0038B5A0.cpp`, which RaceSolitaire's calls and RaceSplitBattleBase's reaches per player). Both are reached through the race's vtable after `render_world`, so they are inside the kick's frame (a.5.1). `kMarkDof*` around 0033E6E0's draws (`game_src/func_0033E6E0.cpp`); `kMarkJitterPass*` around each pass of 00216E78 (a new `game_src` copy) (R6). Every marker is sent between instances, outside any instance's runs (a Marker ends a.5.3's batch runs) | A5, A6 (R6) |
| `Vehicle` | the kick (`send_frame_end`, `runtime/frame.cpp`), in `FrameEnd`'s own `GT4_REC_GFX` block, before `FrameEnd`: one per running car, in entrant order, each padded to whole qwords; fields from the car state D6 found (R14) | A9 (D6 emits, lane A reviews) |

### a.5 Ordering within a frame, and against the existing records

1. **A scene frame is one kicked list.** `FrameBegin` is its first scene record and `FrameEnd` its last. (A
   vehicles-only frame, a.1 mode 2, is a single block the runtime sends at the kick: `FrameBegin`, the `Vehicle`s,
   `FrameEnd`.)
   - The first `FrameBegin` since the last kick opens the frame. The runtime drops any later one until the kick
     and counts it in `FrameEnd::begins`, so the opening one's `flags` stand.
   - The kick (004A11F8) sends `FrameEnd` after `walk(list)` when a frame is open. It goes as a `GT4_REC_GFX` block
     of its own from the kick's thread. The frame's `Vehicle` records go in that same block, before `FrameEnd`
     (rule 4), so they are its last records. Between the list's first block and `FrameEnd` no record that ends a host
     `FrameList` is sent: no `GT4_REC_VSYNC` and no `GT4_REC_DISPLAY` (which goes with it). Other records keep
     their stream order, as on the PS2: `audio_frame`'s records at the tick, uploads from the game's interrupt
     handlers delivered on the kick thread mid-list (`gt4_gfx_upload_now`), read replies. So the whole list and
     its `FrameEnd` land in one host `FrameList`.
   - **Capture edges.** A game-side capture's window (`--capture`, `GT4_CAPTURE_FROM` / `FRAMES`) is decided per
     record by VBlank count (`link_send`, `link_capture_window`, `link.cpp:215`), so a capture's first and last
     lists can be partial. `GT4_REC_STATE` never goes on the link: the host's recorder writes it when a host
     capture starts (`gs_host.cpp:650`).
     - At a capture's start, records before the first `FrameBegin` count as `outsideFrame` (not gated).
     - A frame still open when the stream ends counts as `framesTruncated` (reported), not `framesUnclosed`.
     - So the edges cost nothing at the gates, and captures stay as `link.cpp` writes them.
   - **Ticks during a walk are deferred.** The frame clock's tick (`on_frame`, `runtime/gs.cpp`, on the clock's
     thread under `g_lock`) could send `GT4_REC_VSYNC` between any two blocks of a walk (A3 found it), splitting a
     list across two `FrameList`s.
     - `frame.cpp` keeps a mutex, a `walking` flag and a queue of ticks. A tick during a walk queues its records
       (DISPLAY as the registers stand at the tick, then `{field, count}`), including the DISPLAY resend
       `on_frame` makes when the capture window changes. It sends nothing.
     - `audio_frame` and `link_poll` stay at the tick.
     - After `FrameEnd`, the kick drains the queue in order with the flag still set: lock, pop one, unlock, send,
       until the queue is empty; then it clears the flag under the lock. So a new tick can't overtake a queued one.
     - Neither side holds the mutex across a `link_send` from a guest thread. A guest thread takes `g_lock` after
       each send, and the clock holds `g_lock`.
     - The tick's queue is a fixed ring under an SRWLOCK: nothing is allocated on the clock thread.
     - **Only while scene records are on** (`gt4_scene_on()`, latched at the kick's start and held through the
       drain, so a change of the option mid-walk can't strand queued ticks). With them off, `on_frame` and the
       timing are byte-identical to today at every preset: `original` and the GS path don't change timing for a
       feature they don't use, and the records-off path stays a clean baseline. With records on, the game's VBlank
       count and interrupts are unchanged; only where `GT4_REC_VSYNC` falls in the stream moves, by at most one walk.
     - A synchronous read inside a walk (`link_read_image`) does not wait for a VSync. `GsHost::readTransfer`
       (`gs_host.cpp:251`) serves it at once: it draws the `FrameList` so far as a partial list and reads back
       (A3 checked). So deferred ticks need no flush before a read. The cut this makes is covered in a.8.
     - The kick counts the ticks that land mid-walk (logged per session).
   - The frame therefore holds everything drawn from its `FrameBegin` to the kick: the env-map passes, the main
     view, the mirror, the post effects, the HUD's 3D widget (003BE3C8, `kViewHud`, drawn after `render_world`
     returns) and the menus' car views (00217058, `kViewMenu`). The HUD view is never substituted. It is recorded
     so the output-resolution 2D layer (ARCHITECTURE.md section 4, step 6) can lift the HUD later.
   - The runtime counts each scene record per kind as `gt4_gfx_scene` appends it to the open frame. That is what
     `FrameEnd`'s counts carry, and `countsMismatch` compares them with what arrived.
   - A display list recorded for later calls (`call` / `ret` in `walk`) would send its scene records once per
     call. pgl has no recording state to test at append time: recorded blocks go through the same cursor (+0x1F8).
     So **the walker drops a `kScene` block found at call depth > 0** and logs it once per session.
     - The frame's counts then disagree on purpose. They were counted at append time, possibly in an earlier
       frame, so `FrameEnd` / `ViewEnd` count records the host never gets, and a dropped `Projection` leaves an
       id gap.
     - The host sees `countsMismatch` (and maybe `projectionUnknown`), so that frame is never substituted. A
       record lost this way shows as a fault; the counts are not patched.
     - A3 measured none in the SCENE race (2750 frames). The menus have not been measured yet.
   - At the host, a `FrameBegin` before a `FrameEnd` is a new frame (the old one is counted as `framesUnclosed`
     and dropped). With the runtime closing every open frame at its kick, that means a `FrameEnd` was lost.

   A3 checks:
   - frame-open state, `gt4_gfx_scene` and the kick all run on the game thread, and list N+1 is not built
     before kick N returns;
   - with the deferral, no `GT4_REC_VSYNC` or `GT4_REC_DISPLAY` falls inside a list or between it and
     `FrameEnd`, and no `FrameList` holds two frames (SCENE race: 0 and 0, with 51 ticks deferred);
   - no `kScene` block at call depth > 0 on the race and menu routes (the Home garage, showroom, event screens):
     the race is measured (none); the menus are still to do;
   - no kick in a race has a menu `FrameBegin` before `render_world`'s (its flags would stand).
2. `ViewBegin` / `ViewEnd` pairs do not nest. Instances appear only inside a view; lights as rule 4 says.
3. An `Instance` is followed by its `items` `kBatchRun` records (with the `kBatchWrite`, `kBatchModel`,
   `kState`, `kTexSet` records its items and callbacks send), before the next `Instance`, `ViewEnd` or `Marker`.
   - The decoder tags those `items` runs' `ModelJob`s with the instance (`ModelJob::pad[0]` = its position in
     `SceneFrame::instances` + 1, which is its `serial` in a well-formed stream; 0 for none).
   - It closes the instance once its `items` runs have arrived. `kBatchRun` records after that belong to no
     instance and stay untagged. They come from other draw paths that run after a 0049D328 Cmd: 00497838's
     pre-built shapes, and 0049FAC8's Cmd-like models with their own 0049E480 / 0049E548 composition. A6 gives
     those paths `Instance`s of their own.
   - Batches outside any instance (immediate-mode glows and billboards: 0049BBF0, 0049C2A8...) stay untagged.
   - Blind spot: an extra run from inside 0049D328's item loop (an item callback that draws), on any item,
     takes the next tag slot. That pushes the Cmd's own last run past `items`, untagged. The per-job matrix
     check flags the wrong tag unless the callback draws with the same matrix. `runsAfterSameMatrix` (a.7)
     catches it either way: the displaced run always carries the closed instance's matrix.
4. A `Light` names the view whose space it uses (`Light::view`). It comes inside that view, or after the view's
   `ViewEnd` within the same frame. The course glows and warning lamps (A7 step 1) are filled after the main view
   and the mirror: 0033EAB8 fills their list through its method 0x3F0 (003B7B70), with this frame's camera,
   after the main `ViewEnd`.
   - The decoder adds every `Light` to its view's lights in `SceneFrame`, by id, at `FrameEnd`.
   - The view's `SceneView` package (a.8) is made at its `ViewEnd` and is const once shared, so it holds only the
     lights that came before then. A later light reaches consumers through `FrameList::scene`.
   - A consumer that runs in a partial list cut before `FrameEnd` doesn't have the later lights. It counts that,
     and F2 adds no light in that frame.
   - A `Light` naming a view this frame hasn't begun is `lightViewUnknown`, and is dropped.
   `Emitter` comes anywhere between `FrameBegin` and `FrameEnd`. `Vehicle` comes at the kick, in `FrameEnd`'s own
   block before `FrameEnd` (a.5.1): one per running car, in entrant order, after every view. `FrameEnd` has no
   vehicle count (the layout is unchanged); the decoder counts them as they come. They are sent only with scene
   records on and a frame open, so with records off the stream is unchanged.
4a. A `Projection` comes before the first record that names its id (`ViewBegin::projection`,
   `Instance::projection`); ids are per frame and never reused within it. A `Projection` may come outside a view
   (`view` 0): the env-map passes' come before the main view opens.
5. `kMarkPostBegin` follows the main view's `ViewEnd`; `kMarkHudBegin` follows `kMarkPostEnd`.
6. Scene records sent while no frame is open (e.g. a HUD view in a kick without `render_world`) are dropped by
   the runtime and never reach the host. At the host, `outsideFrame` counts any that do (a stream fault) and drops
   them.
7. **Relation to the Light command.** The front end still decides where the Light command goes (`sceneRead`,
   `gs_frontend.cpp:821`). A bound view's items are substituted in place, at their own runs in GT4's order (B7, B8);
   the Light command is where the scene stage lights them. Several views can bind the same target: in a race the
   rear-view mirror (view 5) is drawn into the scene target with a smaller scissor (R5). In-place drawing keeps
   their order by construction (the mirror over the main view's corner); the scene stage lights each view within its
   own rectangle (the main view's rectangle is the whole target). The rectangle is the scissor its tagged draws
   used: `ViewBegin::scissor` stays 0 (pgl sets the scissor with the view's first draws, after `ViewBegin`), so the
   front end keeps the union of the SCISSORs of each bound view's tagged draws and publishes it as
   `ViewOnList::scissor`. Not the projection's viewport: the mirror's scissor is tighter than its viewport.
   - **Destination alpha is part of the picture.** Later draws read the target's alpha: Z set from it (R5 (c)), a
     low-resolution render added by it (R5 (d)), the car paint's reflection pass with the destination alpha test,
     and at night the light-image glow weighted by it (R11). So a substituted view writes alpha as GT4's draws
     would have left it at the Light, not only colour and Z: that is the alpha after the mid-frame draws (R5 (d)
     resets it at 0.47 of the order and later draws mark it). In-place drawing (B8) writes it by construction, each
     item with its own GT4 draw state.
   - **Principal target.** The host binds a view to its principal target: the colour target (FBP, FBW, PSM) and
     depth buffer (ZBP) of the view's first tagged model draw with a depth test (ZTE on, ZTST not ALWAYS). A view
     with no such draw binds to its first tagged draw's colour target, as before (the HUD view never binds to a
     model draw, a.7 `viewsUnbound`). The sky's pass comes first in the main view (R1), so the principal is
     provisional: set from the first tagged draw, and replaced when the first depth-tested tagged draw arrives on
     another target. The provisional target and its draws then become auxiliary. The split reasons and auxiliary
     reads below are decided at `ViewEnd` against the final principal, whatever order the draws came in.
   - **Auxiliary draws.** A tagged draw into any other colour target is auxiliary: a small pass inside the view,
     such as the sky's 32 x 32 pass (00396B80, R1) in the main view.
     Auxiliary draws stay on the GS path in stream order, before the view's Light command, and a substitution
     never touches them. They are counted per view (`viewAuxiliaryDraws`), with the auxiliary targets they write.
   - **Auxiliary reads.** A draw into the principal target, inside the view, whose texture overlaps an auxiliary
     target written earlier in the view (the main view sampling the sky's pass) reads it. That is counted
     (`viewAuxiliaryReads`) and named in the binding (a.8 `ViewOnList::auxReads`), so a consumer (B7) treats that
     auxiliary target as an input to the substituted view and never drops its producer.
   - **Light-map and reflection draws.** The draws of `kInstLightMap` and `kInstReflection` instances take no part
     in the binding: they never set or replace the principal, are not auxiliary, count towards no split reason and
     no auxiliary target, and a read of their target is not an auxiliary read. The binding treats them as untagged
     draws, and counts them (`viewLightMapDraws`, `viewReflectionDraws`). They are render-to-texture built every
     frame on the GS path, which the substitution never replaces (B8 draws in place, so nothing drops their
     producer), and their readers sample the result as an ordinary texture. The road reflection writes two targets
     (lane A, 2026-11-08): its 64 x 32 target, and the main view's own target, where part of the pass draws
     straight into the picture; those draws too stay on the GS path, drawn in place. The tag is there for accounting
     (a.5.3) and for B9.
     - The road reflection is a second scene drawn in the middle of the main view (lane A, 2026-11-08: its first
       draw at about draw 364 of the frame, about 330 principal draws after it), so as tagged draws it split the
       main view by rule 1. A view of its own would need views to nest or the main view split around it; neither
       is worth it while its output is hardly read: on Route 5 night 11 reads in 1,200 frames, all by untagged
       draws in the same frame, none by the main view. B9 decides whether it needs a `kViewReflection` view, from
       a wet course's capture.
   - **Split.** A view is split (`viewSplitTargets`, by reason) and never substituted when any tagged draw of it:
     (1) builds a second scene: the view's tagged draws into a second target both test and write its depth
     buffer (one of its own; some draw tests it and some writes it, not necessarily the same one). A pass that
     only tests against a depth buffer it never writes, such as the sky's 64 x 64 pass in the main view (A10:
     depth at bp 12288, tested, never written), is auxiliary;
     (2) tests or writes the principal depth buffer while its colour target is another one;
     (3) writes a target whose memory (colour or depth) overlaps the principal colour or depth buffer;
     (4) samples the principal colour or depth buffer before the view's Light command (with the substitution the
     principal holds none of the view's draws yet, so the read would see a different image); or
     (5) writes a fifth auxiliary target (the binding keeps four, a.8).
     Rules (2) to (4) mean no pass the substitution leaves on the GS path can overwrite or read the substituted
     image unnoticed. Untagged draws into the principal target are unchanged: `jobsUntaggedInView`, drawn on the
     GS path.
   - **An env face's copy is a view of its own.** After a face's scene, 0044E660 binds the shared env target
     (256 x 256) and draws the face into it with a Cmd (0049D308, orthographic, sampling the face's target).
     Inside the face's view that copy sampled the principal before the Light (rule 4) and split every face (lane
     A, 2026-11-08: 1.73 splits a frame on Route 5 night). It isn't part of the face's scene, so the face's
     `ViewEnd` comes before it, and the copy is a `kViewEnvCopy` view (`pass` = face), bound to the env target and
     never substituted. Its instances stay inside a view, so `instancesWithoutView` keeps its gate. The sky's pass
     needs no view of its own.
8. **Field frames.** With `display.race_frames = fields`, each field is its own frame (`kFrameField`, odd field
   flagged); views and instances are per field. With a.5.1 this needs one kick per field. A3 confirms the race
   kicks once per field in that mode; if it kicks once per frame, this rule comes back to the architect.

### a.6 Versioning

- Each record carries its kind's layout version. This release: all kinds at 1 (`kVersion`). The R1 changes
  (`ViewBegin`, `Projection`, `Instance::projection`, `FrameEnd`) came before any code landed, so they stay at
  version 1. So does `FrameEnd::begins`, which replaced a reserved field before any `FrameEnd` was sent.
  `Wheel::strokeSpeed` replaced `Wheel::reserved` after `Vehicle` records were sent, but that field was
  defined 0 and the new one counts only with its valid bit (`kVehWheelStroke`), so old records read as "not
  filled" and `Vehicle` stays at version 1 too.
  Its 0 reads as "not counted".
- A layout change adds fields at the end, bumps that kind's version, and keeps the old version decodable for one
  release (old captures). Never reorder or resize fields in place.
- The host accepts a record when `version <= the version it knows` and `bytes >= sizeof(that version's
  struct)`; otherwise it counts `versionUnknown` or `bad` and skips it (records carry their length, so the
  stream continues).
- `GT4_LINK_VERSION` 3 means "the stream may carry `kScene`". Captures without scene records replay as today;
  their modern views fall back.
- Captures carry no scene state: every frame describes itself. Mesh bytes come from the model packets, which a
  capture started mid-run may hold as content only (`BatchModel::sent == 2`): RISKS.md R7.

### a.7 Validation counters

`platform/scene/scene_decoder.h` keeps these per frame and as running totals, printed by `GT4_GSN=stats`
(`[scene] vblank N: ...`) and written to `--profile-json`:

| Counter | Meaning | Gate (reference captures, scene records on) |
|---|---|---|
| `records`, `bad`, `versionUnknown`, `unknownKind`, `outsideFrame` | decoding | `bad == versionUnknown == unknownKind == 0` |
| `framesUnclosed`, `countsMismatch` | `FrameEnd`'s counts differ from what arrived | 0 |
| `framesVehiclesOnly` | frames flagged `kFrameVehiclesOnly`: complete with 0 views when their counts match; no consumer draws them as a scene | reported |
| `framesTruncated` | a frame still open when the stream ends (a capture's last list, a.5.1) | reported |
| `viewsNested`, `viewEndWithoutBegin`, `viewsUnclosed` | pairing (`viewsUnclosed`: a view still open at `FrameEnd`) | 0 |
| `lights`, `lightsAfterViewEnd`, `lightViewUnknown` | lights by view; those that came after their view's `ViewEnd` (a.5 rule 4); those naming a view not begun this frame | `lightViewUnknown == 0` |
| `instances`, `instancesWithoutView` | | `instancesWithoutView == 0` in race frames (the HUD widget is inside the frame, in its `kViewHud` view) |
| `begins` | `FrameEnd::begins`: the `FrameBegin`s the runtime merged into one frame (render_world plus each 00217058 call) | reported |
| `itemsMismatch` | an instance closed (by the next `Instance`, `ViewEnd` or `Marker`) with fewer `kBatchRun` than `items` | 0 |
| `runsAfterInstance`, `runsAfterSameMatrix` | `kBatchRun`s after an instance's `items` runs, before the next `Instance`, `ViewEnd` or `Marker` (untagged, a.5.3); of them, runs whose jobs' matrix equals the closed instance's (`diag(...) · objectToView · viewToClip`, same tolerance as `matrixMismatch`) | reported; `runsAfterSameMatrix` expected 0 |
| `projections`, `projectionUnknown` | an `Instance` or `ViewBegin` naming an id not sent this frame, or an `Instance` with items and projection 0 (its jobs could not be checked) | `projectionUnknown == 0` |
| `matrixChecked`, `matrixMismatch`, `matrixMaxError` | per tagged job: `diag(axisScale·k, 1) · objectToView · projection.viewToClip` (k = shapeScale, or 1 when 0) against `ModelJob::matrix`; mismatch when the largest difference exceeds `1e-5 · max |entry|` (R1: 0 of 270,515, largest 2.3e-7) | `matrixMismatch == 0` |
| `viewportMismatch` | per tagged job: the instance's `projection.viewportScale / viewportOffset / clamp` against `ModelJob::scale / offset / clamp` | 0 |
| `guardUnexpected` | tagged jobs whose guard (qword 200, the clipping renderers' clip flags) is not (0.5, 0.5, 1, 1), the only value R1 saw | reported |
| `cameraChecked`, `cameraMismatch` | course geometry instances (`kInstCourse`, not `kInstCourseObject`) whose `objectToView` differs from their view's `worldToView` (R1: they are equal) | 0 |
| `jobsUntaggedInView` | untagged model jobs into a bound view's target between its `ViewBegin` and the Light command | reported; the substitution keeps drawing them on the GS path |
| `viewSplitTargets` (by reason, a.5.7), `viewsUnbound` | binding | 0 for main views |
| `viewAuxiliaryDraws`, `viewAuxiliaryReads` | tagged draws into a view's auxiliary targets; principal draws sampling one (a.5.7), by view role | reported |
| `viewLightMapDraws` | draws of `kInstLightMap` instances, out of the binding (a.5.7), by view, with the targets they write | reported |
| `viewReflectionDraws` | draws of `kInstReflection` instances, out of the binding (a.5.7), by view, with the targets they write; and the draws that sample those targets, by view (none, untagged) | reported |
| `cuts`, `viewsCut` | partial `FrameList`s drawn at a read (synchronous or asynchronous) inside the frame's list, by kind; views still open at a cut, not substituted in that frame (a.8) | reported (main views: 0 in race frames, unless R9 names a reader inside the main view) |
| `viewsSubstitutedFrameInvalid` | views substituted in a partial list (validated at their `ViewEnd`) whose frame then failed a frame-level check (a.8) | 0 |
| `viewFallbacks` (by reason: not bound, mesh pending, pipeline pending, validation, read pending) | modern views drawn by the GS path | reported; 0 after warm-up for `mesh pending` and `pipeline pending` |
| `meshesNew`, `meshesPending`, `materialsNew` | scene cache | reported |

`tests/scene_records.cpp` (ctest) runs the decoder on synthetic streams: every counter has a test that makes it
move, and a well-formed stream leaves all error counters at 0.

### a.8 Host-side derived types (`platform/scene/scene_frame.h`)

Not on the link; listed so lanes B, E and F agree on them.

```cpp
namespace gt4::scene {
struct MeshKey {             // a Cmd's geometry: welded once
    uint64_t packets;        // hash of the items' batch contents (qwords 0..192 and the job's geometry fields), in order
    uint32_t renderer, chain;   // the first item's ModelJob::renderer and ::chain (the decode depends on them)
    uint32_t chainUnpack;    // lighting chain's decompression routine (normals' format)
};                           // (platform/scene/scene_frame.h; B5 named renderer / chain `mask` / `entry`)
struct MaterialKey {         // GT4's material: what the batch's pixels depend on besides geometry
    uint64_t texture;        // the texture's identity (the front end's textureIdentity(id): content hash,
                             //   fields, palette, TEXA), so a re-decode after eviction keeps the id; 0 untextured
    uint32_t palette;        // CLUT source id/version
    uint32_t body;           // ModelJob::chain >> 16 (lighting body)
    uint32_t dot;            // ModelJob::dot
    uint32_t state;          // hash of the DrawState fields that matter (blend, tests, fog, filter)
    uint64_t colours;        // 0. The batch's colour qwords (204..216, from 0049E228: the material's rows scaled
                             //   by a global table) are per-frame lighting constants: one object alternates
                             //   between two colour sets within a frame and others drift, so in the key they made
                             //   28 new materials a frame (B6, race_boot). B7 reads them per frame from the batch,
                             //   as gsn_models.comp does. Reserved for an unscaled per-object colour, if a record
                             //   ever carries 0049E228's source rows
};
using MeshId = uint32_t;      // 0 = none
using MaterialId = uint32_t;  // 0 = none

struct ProjectionData {
    uint16_t id, view;
    Mat4 viewToClip;                              // GT4's GL projection as sent (its own near / far)
    float viewportScale[4], viewportOffset[4], clamp[4];
};
struct ViewData {
    uint16_t id; uint8_t role, flags; uint32_t pass;
    uint16_t projection;                          // the camera's (ViewBegin::projection)
    Mat4 worldToView;
    Mat4 cameraToClip;                            // the camera projection's x and y rows, with the modern path's own
                                                  // reverse-Z infinite depth (not GT4's per-instance near / far)
    Mat4 previousWorldToView;                     // matched by role + pass from the last frame
    uint32_t target = 0, depth = 0;               // GS target ids, bound by the front end
    uint32_t firstInstance = 0, instanceCount = 0, firstLight = 0, lightCount = 0;
    bool valid = false;                           // the view's own checks passed at its ViewEnd (cuts, below)
};
struct InstanceData {
    uint16_t view; uint16_t flags; uint32_t serial;
    uint16_t projection;                          // GT4's projection for this instance: parity and GS Z (B7, B8)
    uint64_t object;                              // set << 32 ^ model << 8 ^ lod, or the Cmd address
    Mat4 objectToView, objectToWorld, previousObjectToWorld;
    float axisScale[3], shapeScale;
    uint32_t jobCount = 0;                        // in FrameList::models, found by tag (ModelJob::pad[0] = this
                                                  //   instance's position + 1), not as a range: an item
                                                  //   callback's runs can land between them (a.5.3)
    MeshId mesh = 0;                              // copied at sceneFrame() from the views' ViewOnList::ids (B6)
    uint32_t firstMaterial = 0, materialCount = 0;   // in SceneFrame::materials: one per item, in the mesh's
                                                     //   item order (a car has more than four). In a SceneView
                                                     //   package these three stay 0: it is made before B6 runs
                                                     //   and const after; its ids are on ViewOnList
    uint32_t entrant = 0xFFFFFFFF;
};
struct SceneFrame {                               // one per kicked list (a.5.1)
    uint32_t frame = 0, vblank = 0, flags = 0;
    uint16_t begins = 0;                          // FrameEnd::begins
    std::vector<ProjectionData> projections;      // by id - 1
    std::vector<ViewData> views;
    std::vector<InstanceData> instances;
    std::vector<gfx::scene::Light> lights;
    std::vector<gfx::scene::Emitter> emitters;
    std::vector<gfx::scene::Marker> markers;      // with the FrameList command index each arrived at
    std::vector<uint32_t> markerCommands;
    std::vector<gfx::scene::Vehicle> vehicles;
    std::vector<MaterialId> materials;            // the instances' materials (InstanceData::firstMaterial), B6
    SceneCounters counters;
    bool complete = false;                        // FrameEnd arrived and counts match
};
}
```

`FrameList` (`gs_frontend.h:145`) gains `std::unique_ptr<scene::SceneFrame> scene` (null without records: the frame
whose `FrameEnd` arrived while that `FrameList` was being built) and
`Draw` gains `uint16_t view = 0`. `recycleFrameList` resets them.

**Cuts at a read.** A read of drawn pixels inside a list draws the `FrameList` so far as a partial list. That
holds for both kinds: synchronous (`GsHost::readTransfer`, `gs_host.cpp:251`) and asynchronous
(`NativeTarget::readLater`, `gs_host.cpp:80-90`, which puts the readback on the partial list for the render
thread). Only the waiting differs. So one kicked list can be drawn as several `FrameList`s: partial ones at each
read, then the one that receives `FrameEnd`. (Race route: 38 reads in about 4900 frames.)
- The scene decoder works in stream order, independent of `FrameList`s. Its open frame, view and instance, and
  the jobs' tags, carry across a cut.
- The front end (A10) keeps each `Draw::view` across the cut. The `SceneFrame` goes on the last list only.
- **Closed views travel on their own.** At each `ViewEnd`, the stream hands the target a `SceneView` package, in
  stream order. The front end puts it on the `FrameList` being built, so a partial list carries the views it can
  substitute. `FrameList::scene` still arrives at `FrameEnd`, on the last list, for the frame-level checks.
  ```cpp
  struct SceneView {                              // platform/scene/scene_frame.h
      ViewData view;                              // view.valid: the verdict at ViewEnd (below)
      std::vector<InstanceData> instances;        // copies, in arrival order
      std::vector<ProjectionData> projections;    // the ones the view and its instances name
      std::vector<gfx::scene::Light> lights;      // the view's, as far as its ViewEnd (later ones: a.5 rule 4)
      std::vector<gfx::scene::Emitter> emitters;  // frame-wide records that arrived before this ViewEnd
      std::vector<gfx::scene::Vehicle> vehicles;  //   (later ones are not available to a partial substitution)
  };
  struct FrameList::ViewOnList {                  // nested, platform/gsnative/gs_frontend.h
      std::shared_ptr<const SceneView> package;   // made by the decoder at ViewEnd; const once shared
      uint32_t target = 0, depth = 0;             // the front end's binding (the principal target, a.5.7),
      bool splitTargets = false;                  //   filled when the package arrives (after its ViewEnd,
                                                  //   so every tagged draw is bound); split: the reason in stats
      uint32_t auxTargets[4] = {};                // auxiliary colour targets, in first-write order
      uint8_t auxCount = 0;                       // a fifth is split (a.5.7 rule 5), so no allocation
      uint8_t auxReads = 0;                       // bit i: a principal draw samples auxTargets[i]
      uint16_t auxDraws = 0;                      // tagged draws into the auxiliary targets
      uint16_t scissor[4] = {};                   // x0, y0, x1, y1: the union of its tagged draws' SCISSORs (a.5.7)
      struct InstanceIds { MeshId mesh = 0; uint32_t firstMaterial = 0, materialCount = 0; };
      std::vector<InstanceIds> ids;               // B6: by position in package->instances, filled in
                                                  //   sceneView() before the view goes on the list; mesh 0:
                                                  //   skinned, key-frame blend, pending, or the view cut
      std::vector<MaterialId> materials;          // one per item, in the mesh's item order (both vectors keep
                                                  //   their capacity with the list)
      struct ItemRef {                            // B7: the item's ModelJob and its Draw, indices in this list
          uint32_t job, draw;                     //   (valid only on the list that holds the package: a cut
          uint32_t firstModelVertex, modelVertexCount;   // view has no ids and no items); the job's own vertex
      };                                          //   range, since one Draw can merge several instances' jobs
      std::vector<ItemRef> items;                 //   parallel to materials, filled in sceneView() beside the ids
  };
  // FrameList gains: std::vector<ViewOnList> views;   (SceneViews pooled, below)
  //   and std::shared_ptr<const SceneCache> sceneCache;   (B6's caches, read-only on the render thread: F2b, B7)
  ```
  - The package can't hold the binding: it is made before the front end binds any of the view's draws, and it
    is const once shared. So the binding goes beside it on the list.
  - Packages are made only while the front end's scene split is on (no consumer otherwise). A list
    the renderer skips drops its scene and packages; that is counted (`listsSkippedWithScene`, reported in the
    host summary; until that lands, the running count in carryOver's log line).
  - `ViewData` gains `bool valid`. The decoder keeps per-view error counts and sets it at `ViewEnd`: the
    instances match `ViewEnd::instances`, and the view has no item, matrix, viewport or `projectionUnknown`
    errors. Every job of the view has been checked by then, because `job()` runs before the target sees the
    job. `viewSplitTargets` is the front end's own check (`ViewOnList::splitTargets`, a.5.7).
  - The copies cost about 80 KB per main view per frame (about 260 instances), tens of microseconds on the GS
    thread. A lazy copy is not possible, since the decoder runs ahead of the front end. `SceneView` objects are
    pooled: `recycleFrameList` returns the ones no one else still holds (`use_count() == 1`, so the render thread
    is done with them). The stream reuses them at `ViewEnd` and keeps their vectors' capacity, so a steady frame
    allocates nothing once the pool has warmed.
  - `SceneFrame` stays as it is (instances indexed by `ModelJob::pad[0]`). The package is a copy, not a second
    owner.
  - Emitters may come anywhere in a frame (a.5 rule 4). A view substituted in a partial list sees only those that
    arrived before its `ViewEnd`; A8 sends them before the main view's `ViewEnd` where it can. Vehicles come at
    the kick, after every view, so a package never holds them (`SceneView::vehicles` stays empty): consumers read
    them from `FrameList::scene`, and a substitution in a list cut before `FrameEnd` has none.
  - The same holds for lights in practice. The lamp lists are filled after the main view (A7, a.5 rule 4): on a
    Route 5 night run all 3,413 `Light`s came after their view's `ViewEnd`, so packages carry almost none.
    Consumers that need lamps (F2, B7) read `SceneFrame` / `FrameList::scene`, and a substitution in a list cut
    before `FrameEnd` doesn't see them. F2b's lamps are the host's own and don't depend on the records.
- A partial list is drawn before its frame's `FrameEnd` has arrived. So a view in a partial list is validated on
  its own, at its `ViewEnd`, not at `FrameEnd`.
  - **A view whose `ViewEnd` arrived before the cut** can be substituted in the partial list, at its Light
    command as usual, if its own checks pass: `ViewEnd::instances` against the instances that arrived,
    `itemsMismatch`, `matrixMismatch`, `viewportMismatch`, `projectionUnknown` for its instances,
    `viewSplitTargets`. Most reads come after the main view (post effects, flare), so this keeps such frames
    substitutable.
  - **A view still open at the cut** (tagged draws on both sides of it) is not substituted in that frame. It is
    counted as `viewsCut` and drawn as original.
  - If the frame then fails a frame-level check (`countsMismatch`, `framesUnclosed`), views already substituted
    in a partial list stay drawn. That is counted as `viewsSubstitutedFrameInvalid`. It means a stream fault,
    and the gate is 0.
- Which reads matter, and what a reader gets from a target whose view was substituted, are R9's questions.
- The A10 SCENE run counts cuts per frame, by kind (synchronous or asynchronous), with the reader's destination,
  for R9.

---

## (b) The render graph

### b.1 Scope

One graph per presented frame, built on the render thread, compiled, then recorded into the frame's command
buffer(s). It owns: transient resources and their aliasing, barriers (synchronization2), the queue a pass runs on,
GPU timestamps per pass, and pipeline creation through the pipeline cache. It does not own: the swapchain, the
GS targets' lifetime (the GS back end), the scene cache's meshes.

### b.2 Interface (`platform/render/graph/render_graph.h`)

```cpp
namespace gt4::rg {

enum class Queue : uint8_t { Graphics, Compute };   // Compute: async when the device has a compute-only family

enum class Size : uint8_t { Absolute, Render, Output };   // Render / Output: times scale, from FrameInfo
struct TextureDesc {
    VkFormat format = VK_FORMAT_UNDEFINED;
    Size size = Size::Render;
    float scaleX = 1.0f, scaleY = 1.0f;     // Render / Output
    VkExtent2D extent{};                    // Absolute
    uint32_t mips = 1, layers = 1;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    bool cube = false;
    const char* name = "";
};
struct BufferDesc {
    VkDeviceSize bytes = 0;
    bool hostVisible = false;               // uploaded each frame (per-slot ring)
    const char* name = "";
};

struct Texture { uint32_t id = 0; uint32_t version = 0; explicit operator bool() const { return id != 0; } };
struct Buffer  { uint32_t id = 0; uint32_t version = 0; explicit operator bool() const { return id != 0; } };

enum class Use : uint8_t {
    Sampled,                                   // SHADER_READ_ONLY_OPTIMAL
    StorageRead, StorageWrite, StorageReadWrite,   // GENERAL
    ColorWrite,                                // attachment, previous contents not needed
    ColorReadWrite,                            // attachment, loaded (blending, partial writes)
    DepthWrite, DepthRead, DepthReadWrite,     // attachment
    TransferSrc, TransferDst,
    Present,
    Opaque,                                    // the pass synchronises the image itself (GS segments); see b.6
};
enum class BufferUse : uint8_t { Uniform, StorageRead, StorageWrite, StorageReadWrite, Vertex, Index, Indirect, TransferSrc, TransferDst };

struct Range { uint32_t mip = 0, mips = VK_REMAINING_MIP_LEVELS, layer = 0, layers = VK_REMAINING_ARRAY_LAYERS; };

class PassBuilder {
public:
    Texture create(const TextureDesc& desc);                         // transient
    Buffer create(const BufferDesc& desc);
    // Declares a use; a writing use returns the next version, which later passes must use.
    Texture use(Texture t, Use use, VkPipelineStageFlags2 stages, Range range = {});
    Buffer use(Buffer b, BufferUse use, VkPipelineStageFlags2 stages);
    void queue(Queue q);                     // default Graphics
    void keep();                             // side effects outside the graph: never culled
};

struct FrameInfo {
    uint32_t slot = 0, frameIndex = 0;
    VkExtent2D render{}, output{};
    const Settings* settings = nullptr;
    const scene::SceneFrame* scene = nullptr;   // null on frames without scene records
};

class PipelineCache;

class PassContext {
public:
    VkCommandBuffer command = VK_NULL_HANDLE;
    const FrameInfo& frame;
    PipelineCache& pipelines;
    vk::Image& image(Texture t);            // in the layout the pass declared
    VkBuffer buffer(Buffer b);
    VkDeviceSize offset(Buffer b);           // host-visible rings
    void* mapped(Buffer b);
    // Dynamic rendering over declared attachments; load = clear for ColorWrite / DepthWrite when clear is given.
    void beginRendering(std::initializer_list<Texture> colours, Texture depth, const VkClearValue* clears = nullptr);
    void endRendering();
};

class Graph {
public:
    explicit Graph(const vk::Device& device);
    Texture import(vk::Image& image, const char* name);   // state taken from image.layout / stage / access, written back after execute
    Buffer import(vk::Buffer& buffer, const char* name);
    Texture history(const char* name, const TextureDesc& desc);   // persists across frames (two copies, swapped per frame); never aliased
    template <class Setup, class Execute> void pass(const char* name, Setup&& setup, Execute&& execute);
    void compile();
    void execute(VkCommandBuffer graphics, VkQueue computeQueue /* null: none */);
    std::string dump() const;                              // GT4_RG_DUMP=1: the compiled graph as Graphviz
};

// Feature modules connect through typed slots, not through each other's headers.
class Blackboard {
public:
    template <class T> T& add();             // one T per frame
    template <class T> const T* get() const; // null when the producer did not run
};

struct FeatureConfig { const Settings* settings; SceneFormats formats; };

class Feature {
public:
    virtual ~Feature() = default;
    virtual const char* name() const = 0;
    virtual void prepare(PipelineCache& pipelines, const FeatureConfig& config) {}   // on settings or format change
    virtual void build(Graph& graph, Blackboard& board, const FrameInfo& frame) = 0; // adds passes
};

}  // namespace gt4::rg
```

**Additions in the code (ticket B1, agreed in its review).** `Graph::begin(const FrameInfo&)` starts a frame (it
sizes Render / Output textures). `Graph(Backend&)` compiles without a GPU, for the CPU tests (`Backend` gives memory
requirements and whether a compute-only queue is used). `Graph::plan()` returns the compiled plan: live passes and their
barriers, culled passes, placements, async edges, errors. `Graph::historyValid(Texture)` tells a feature whether a
history's version 0 holds the last frame's contents (false on its first frame and after it is made again).
`PassContext::pipelines` is a pointer until the `PipelineCache` class lands (B2b). A history's version 0 is the
previous frame's copy; its first write goes to the other copy and must not read; a pass writing from version 0
gets the new copy for that handle. Async compute: the plan has its edges and lifetimes, but execution stays in order
on the graphics queue until B21. `Plan::exports` gives what each import and history copy ends the frame in (the layout,
and the stages and accesses of its last write and every read since), written to its `vk::Image` after `execute()`.

### b.3 Semantics

- **Order.** Passes run in the order added. Compile removes passes that are culled and adds barriers; it never
  reorders graphics passes. Compute passes marked `Queue::Compute` may overlap graphics work between their
  declared dependencies.
- **Culling.** A pass whose outputs nobody reads and that did not call `keep()` is culled. Imports written by a
  pass count as read (they leave the graph).
- **Versions.** Each writing use bumps the version. Reading a stale version, writing a texture another pass
  writes at the same version, or reading a transient before any write is a compile error (debug: abort with the
  pass names; release: the view falls back).
- **Barriers.** Per resource and range, from the previous use to the next: layout from `Use`, stage from the
  declared stages, access from `Use`. Planned by the graph (synchronization2) and flushed once at the start of
  each pass. Read after read in the same layout: none when the stages that saw the last write cover the new read,
  else a barrier from that write (a fragment read, then a compute read). Write after read: execution dependency
  only. Imports start from and end in `vk::Image`'s tracked state, so code outside the graph keeps working during
  the migration.
- **Ranges.** v1 tracks whole images when `Range` is default and per mip when a pass gives one (bloom chains,
  HiZ). Layers per range from v2.

### b.4 Transient aliasing

- Transients get a lifetime `[first use, last use]` in compiled order. Lifetimes of resources used by a compute
  pass extend to the graphics pass that waits for it.
- Placement: first fit by memory type bits, size and alignment into per-slot blocks (`vk::ImageHeap`,
  `vk.h:81`), one heap per frame slot (2). Two transients share memory only if their lifetimes do not overlap.
- An aliased image's first use is a discard (`UNDEFINED` old layout); `ColorReadWrite`, `DepthRead*`, `Sampled`
  and `Storage*Read*` as a first use are compile errors.
- `VkImage` objects are cached per (description, placement) and kept across frames; they are recreated only when
  a description or the render extent changes.
- `history()` and imports are never aliased.
- The graph reports the transient memory, with and without aliasing, in `--profile-json`.

### b.5 Async compute

- `Device` (`vk.h:17`) gains an optional compute-only queue family. When present, `Queue::Compute` passes are
  recorded into a compute command buffer and the graph adds a timeline semaphore signal after the last compute pass
  of each run and a wait before the first graphics pass that uses its outputs.
- Images used on both queues are created `VK_SHARING_MODE_CONCURRENT` (no ownership transfers in v1).
- Without a compute family, or with `GT4_RG_ASYNC=0`, the same passes run in order on the graphics queue.
  **Acceptance for every async pass: the frame is identical with `GT4_RG_ASYNC=0` and `=1`.**

### b.6 GS segments and `Use::Opaque`

The GS back end executes long command lists that bind, sample and render GS targets in an order only it knows.
In the graph, each **GS segment** (the frame list's commands between two split points: Light commands, and later
markers) is one pass that declares the GS targets it touches with `Use::Opaque`. For an opaque use the graph
makes all earlier graph work on that image visible (a full barrier from its tracked state), lets the pass do its
own barriers as today, and reads the image's tracked state back from `vk::Image` afterwards. The front end
already knows which targets a range of commands touches (`Draw::target`, `Draw::depth`, `TextureSource`).

**As B4 builds it: GS targets not imported yet.** A frame may hold several `FrameList`s. When a later list needs a
target bigger, or of another generation, `ensureTarget` retires the image and makes a new, cleared one in place,
after the earlier list drew into the old one (lane B, B4). So the CPU decisions can't all move ahead of the build:
the earlier list would draw into the new image. One target can't be two images in one build either, since imports
are keyed by `vk::Image*`.
- In B4, each list's passes are kept passes (`keep()`) that record their own barriers from `vk::Image` state, as
  today: `gs: uploads` (`ensureTarget`, the new-target clears and the uploads, run when the pass executes, so
  the order is today's), `gs: 3D batches`, and one `gs: draws` per range between Light commands, with the
  effects inside.
- The scene stage's work images (`ao`, `lit`, `velocity`, `ssr`) are graph transients with declared uses. The
  passes touch the GS colour and Z targets themselves.
- No GS pass runs on `Queue::Compute` while the GS targets are not declared.
- `Use::Opaque` imports of GS targets come with the first consumer outside the GS back end that uses them
  inside the graph: B8's side channels beside the scene target (read by the scene stage), and the display
  target that `scene: main` samples. Then a target that changes image between lists needs one import per
  image, with the retired image kept alive until the frame's work has finished. That is B8's design, reviewed
  by the architect before it is built.

**The synchronous read** (`GsnScene::read`) builds a second `rg::Graph` instance, with its own plan cache and
transient heap, recorded on the uploader's command buffer. It never touches the frame's graph while that is being
built. Since the read waits for its fence before it returns, one heap slot is enough. Its passes are reported
with a `read:` prefix. The frame-again path (the last list executed again) uses the frame's build.
- The two graphs share images only through imports. Each graph reads the tracked state (`vk::Image`) when it
  imports an image and writes it back after `execute()`. So the two must not interleave between import and
  execute, and the GPU must run their work in the order it was recorded. A read graph is built, executed,
  submitted and waited for entirely before or entirely after the frame graph's import-to-submit span, or the
  frame's recorded work that touches the same images is submitted before the read's. Today's code gives the
  first order, and B4 keeps it. `GsnScene::read` runs only from the main loop's read branch
  (`takeReadRequest` -> `Renderer::readGsTarget`), on the render thread, never inside `Renderer::draw` (which
  begins, builds, executes and submits the frame's graph in one call). It starts with `vkDeviceWaitIdle` and
  waits for its own submit.

### b.7 Timestamps and CPU scopes

- Every pass is wrapped in two timestamps (per-slot query pool, `kPerSlot` sized to the pass count), read when the
  slot's fence signals, as `gsn_scene.cpp:270-316` does today; that code moves into the graph.
- Each pass also opens a `GT4_CPU_SCOPE` with its name for recording time.
- Reported in the profile overlay, the hitch log, and `--profile-json PATH` (one JSON line per frame: pass names,
  GPU microseconds, CPU microseconds, transient bytes, pipelines created).
- On a compute queue whose `timestampValidBits` is 0, its passes report CPU time only.

### b.8 Pipeline cache (`platform/render/pipeline_cache.h`)

```cpp
class PipelineCache {
public:
    PipelineCache(const vk::Device& device, std::filesystem::path folder);   // loads the cache file and key list
    ~PipelineCache();                                                       // saves (tmp + rename)
    const vk::Pipeline& graphics(const vk::GraphicsDesc& desc);   // synchronous: created now if missing (GS path)
    const vk::Pipeline& compute(const vk::ComputeDesc& desc);
    struct Ref { const vk::Pipeline* pipeline = nullptr; bool ready() const { return pipeline && pipeline->valid(); } };
    Ref request(const vk::GraphicsDesc& desc);   // created on a job worker; not ready this frame -> the caller falls back
    Ref request(const vk::ComputeDesc& desc);
    void prewarm();                              // replays the key list on workers before the first frame
    uint32_t created() const, fromDisk() const;
};
```

- **Step 1 already exists as uncommitted work** in `C:/Users/julie/gt4-native-game` (on a80cfca): one
  `VkPipelineCache` in `vk::Device` (`Device::pipelineCache`), `vk::openPipelineCache / savePipelineCache /
  closePipelineCache`, used by every `createGraphics` / `createCompute`; the file is `pipeline_cache.bin` beside
  `settings.ini` (`GT4_PIPELINE_CACHE=FILE` another file, `=0` none); the Vulkan header is checked against vendor,
  device and `pipelineCacheUUID` before use; written to `.new`, then renamed over the old. Ticket B2 lands it as
  is after review (PLAN.md), with one fix: replace the `remove` + `rename` pair with `MoveFileExW(...,
  MOVEFILE_REPLACE_EXISTING)` on Windows (`rename` on POSIX), so a crash between the two cannot lose the file.
- Step 2 (ticket B2b) wraps that cache in the `PipelineCache` class above: the description map, `request()` on
  workers, the key list `pipeline_cache.keys` beside the `.bin` (the descriptions seen, including the GS back
  end's draw-state pipelines) and `prewarm()`, and a save every 64 new pipelines as well as at exit.
- The GS path always uses the synchronous calls: a missing pipeline there would change pixels.

### b.9 How today's code ports

| Today | In the graph |
|---|---|
| `FramePipeline::record` (`pipeline.cpp:972`): exposure, MSAA resolve, FSR 2 / TAA, DOF, motion blur, bloom, tone map, RCAS, FXAA / SMAA, EASU / resample, output pass, 2D overlay | `DisplayFeature` (`platform/render/display_feature.cpp`): one pass per stage, same shaders, same order. `sceneColor/Velocity/Depth` imported from the scene source (later: the modern view's outputs). TAA history, SMAA T2x's frames: `history()`. (As built in B3: the MSAA resolve stays with the scene pass in `FramePipeline`; `exposure` is an import, rewritten every frame from its state buffer, so it carries no image contents between frames.) `hdrA/B`, `ldrA/B`, `outA/B`, `dof*`, `tiles`, `neighbourMax`, `bloomDown/Up`, `smaa*`: transients. `areaTexture`, `searchTexture`: imports. FSR 2's own resources stay inside its dispatch (one pass, imports). Swapchain: import, `Present`. |
| `GsnScene::Impl::execute` (`gsn_scene.cpp:794`) | `GsFeature`: per list, the uploads (with `ensureTarget`), the 3D batch dispatch (`gsn_models.comp`) and GS segments split at each Light command, as kept passes that do their own barriers (b.6). `Use::Opaque` on GS targets from B8. |
| The scene stage at a Light command (`gsn_scene.cpp:1494-1518`: motion vectors, relight, reflect, haze, temporal resolve, motion blur, focus) | `SceneStageFeature`: one pass each, between two GS segments; their work images become transients (`ao`, `lit`, `velocity`, `ssr`: each pass samples the target's colour and Z and writes its transient; where the result goes back into the colour target, a pass right after it, `<name> (copy back)`, copies it, since a pass uses each image in one layout). The per-scene TAA history is **not** a `history()`. It stays as `GsnScene`'s own two images per target, imported, with today's rules: valid only if used the frame before, and a new copy at each resolve. A target can resolve twice in a frame (two lists, or a Light resolving the same scene again), and targets are made and retired between lists. `history()` gives one new copy a frame, swapped once, so it fits neither. |
| (new) the substituted items of a bound view | Drawn in place inside the GS segment, at their runs in GT4's order: B7's vertex stage (GT4's Z, `Instance::projection`), the mode's fragment stage, and side channels (normal, material id, view-space depth, motion) beside the scene target. The scene stage at the Light reads the side channels (B8, B10). No compositor. |
| GPU profile in `gsn_scene.cpp:270-316` and `renderer.cpp:216, 774` | graph timestamps (b.7) |
| no pipeline cache | `PipelineCache` (b.8) |

Gate for the two port tickets (B3, B4): every preset's output on the reference captures is bit-identical before
and after, and Vulkan synchronization validation reports nothing.

### b.10 Shared shader arithmetic is precise

Shader code that more than one stage, renderer or device must compute identically is written with explicit
arithmetic: `precise` (SPIR-V `NoContraction`), so no compiler fuses a multiply and an add on its own. Every stage
and every device then computes the same bits. This covers GT4's transform, GS Z, the window point (the origin
subtraction, `gt4WindowPoint`) and the lighting chains (`gt4_transform.glsl`, `gt4_lighting.glsl`), shared by
`gsn_models.comp` and B7's `modern_parity.vert`, and the parity stage's fog word (its ADC bit hides a triangle).
- Code shared only between vertex stages on one device (`gsnPosition` in `gsn_common.glsl`, used by `gsn.vert`,
  `gsn_velocity.vert` and `modern_parity.vert`) relies on `invariant gl_Position` instead, which is enough
  there. Making it precise would move `ORIG` again for nothing.
- Why (lane B, B7, 2026-11-08): without it the GS path's 3D was one driver's fusion choice. Moving identical
  expressions into includes changed 15 of 30 `race7` frames (1 level); `precise` on the transform alone moved 9 %
  of pixels, because positions and Z moved by one rounding and GT4's near-equal depth tests (RISKS R3) flipped.
  Explicit `fma()` orders didn't reproduce the driver either. So the PC and the Odin already drew those pixels
  differently, and B7's vertex stage couldn't give the compute stage's Z by sharing source alone.
- It matches the VU's structure (no fused multiply-add) and the CPU renderers' (`renderers3d.cpp`), though not the
  VU's truncating rounding.
- Adopting it moves `ORIG`: a re-baseline under PLAN section 2's rule, listed in CAPTURES.md.

### b.11 `DisplayInputs`: the scene side's frame for the display side

The seam between the scene side (lane B: the scene source, the GS path, the modern renderer) and the display side
(lane C: the display stages and the upscalers, `platform/render/upscale/` from B16). The scene side delivers one frame
at a render extent R. The display side takes it and the output extent, and nothing else. `DisplayInputs`
(`platform/render/display_feature.h`) carries it, filled by `FramePipeline` (`pipeline.cpp`).

| Input | Format and meaning |
|---|---|
| colour | `kHdr` (R16G16B16A16_SFLOAT) at R, linear, before exposure |
| velocity | R16G16_SFLOAT at R: where the pixel's surface was in the previous frame less where it is now, **as a fraction of R** (x and y in 0..1 units), the jitter removed. A backend scales by R (FSR 2: `motionVectorScale` = R). 0 where nothing is known (2D, a surface new this frame) |
| depth | D32_SFLOAT at R (the resolved one under MSAA), in the source's stated convention (below) |
| reactive, transparency | R8_UNORM at R, optional (`SceneSource::reactiveMask`, `transparencyMask`; null: none). Reactive: particles and other blended effects. Transparency: surfaces whose shading the velocity doesn't describe (reflections, animated textures) |
| camera | near plane, vertical field of view and the depth convention, as stated values (below) |
| reset | `DisplaySceneState::reset`: the history is invalid this frame (a camera cut, a settings change) |
| jitter | the sub-pixel offset applied this frame, in render pixels. Its sequence is the selected upscaler's (B16 (3)) |

The units are R's, not pixels, so the same velocity stays right when R is not an integer scale of the output
(B16b). The GS scene stage keeps its own units inside the GS path: `gsn_velocity.frag` writes GS pixels, previous
less now, unjittered. Delivering velocity at this seam means dividing by the GS target's extent, the conversion
B16b makes.

Depth conventions, one per source, stated with the frame (the upscalers' flags are chosen from these, not assumed):
- The test scene: reverse-Z with an infinite far plane (`SceneCamera`: near 0.1, 1 at the near plane, 0 at
  infinity). FSR 2's `DEPTH_INVERTED | DEPTH_INFINITE` describes it exactly.
- The GS path: GT4's integer Z times 2^-32, larger is nearer (GT4 tests GEQUAL). A 24-bit buffer tops out near
  2^-8. On race views GT4's 3D Z is 1.6e6 / view distance (docs/LIGHTING.md; `kZScale` in `gsn_scene`'s light
  and AO passes), so depth = 1.6e6 x 2^-32 / distance: a reversed infinite projection, its equivalent near
  1.6e6 x 2^-32 = 3.73e-4 in the view's distance units. FSR 2's `DEPTH_INVERTED | DEPTH_INFINITE` fits it, with
  that equivalent near as `cameraNear`, not the projection's own near. Measured on race views (lane B). Other
  roles (the showroom's, the menus' 3D) may scale Z differently and are checked per role before they're used.
  2D draws (a constant Z per sprite) and the sky put arbitrary values there. The reactive or transparency masks,
  or B8's per-item side channels, must cover them before an upscaler trusts the GS path's depth.

The camera, by role (with scene records on): the displayed frame's `kViewMain` view that drew into the shown target
(B8's binding: its target is the shown target), else its `kViewPhoto`, else none. From that view's last `Projection`
record (`viewToClip`, a GL perspective): the vertical fov = 2 atan(1 / viewToClip[1][1]). For the GS path the near
given to the upscaler is the depth's equivalent near above. With none (records off, as at `original`, or a frame
with no 3D view: menus, HUD-only screens) the backend uses stated defaults (near 0.1, vertical fov 0.87 rad), and
the frame is flagged and counted in the display stages' summary.

**What the GS path delivers today:** colour only. No velocity (`gsn_display.frag` writes 0; `hasMotionVectors()` is
false, so FSR 2 falls back to FSR 1) and no depth. Its TAA resolves per scene target inside the GS scene stage
(`antiAliasesItself()`, `resolveConfig`'s `selfResolved`). Delivering velocity and depth here is B16b's, lane B's.

---

## (c) The light list

Shared by night lighting on the GS path (ticket F2, relighting GT4's scene from its Z), the clustered deferred
lighting and the clustered forward pass (F3, B lane). One producer, one cluster build, many readers.

### c.1 Layout (`include/gt4/render/light_list.h`, mirrored by `platform/render/shaders/light_list.glsl`, std430)

```cpp
namespace gt4::render {

inline constexpr uint32_t kMaxLights = 1024;          // per view per frame
inline constexpr uint32_t kClusterX = 16, kClusterY = 9, kClusterZ = 24;   // froxels over the view's render extent
inline constexpr uint32_t kMaxLightsPerCluster = 128;

enum LightType : uint32_t { kPoint = 0, kSpot = 1, kDirectional = 2 };
enum LightFlags : uint32_t {
    kCastsShadow = 1,      // a shadow-casting light (the sun; later headlights)
    kVolumetric = 2,       // contributes to froxel fog
    kNoSpecular = 4,
    kGameGlow = 8,         // GT4 draws its own glow sprite for it: do not add one
    kFromGame = 16,        // from a scene::Light record (else synthesised by the host)
    // bits 8-15: the light's profile index (F7's cookie array, 0 none); bits 16-27: its shadow-atlas slot (F6 and
    // B11's atlas, 0 none). So GpuLight stays 64 bytes.
    kProfileShift = 8, kProfileMask = 0xFF00, kShadowSlotShift = 16, kShadowSlotMask = 0x0FFF0000,
};

struct GpuLight {                       // 64 bytes
    float position[3]; float range;     // view space, metres; range: where the light reaches 0
    float direction[3]; float spotScale;   // view space, unit, pointing away from the light; 1 / max(cosInner - cosOuter, 1e-4)
    float colour[3]; float spotOffset;  // linear Rec.709, scene-referred, intensity folded in; -cosOuter * spotScale
    uint32_t type, flags;               // LightType, LightFlags
    float sourceRadius;                 // metres (soft falloff near the source, specular size)
    uint32_t id;                        // scene::Light::id (temporal stability, debugging)
};
static_assert(sizeof(GpuLight) == 64);

struct LightListHeader {                // 32 bytes, then GpuLight lights[count]
    uint32_t count;                     // lights in the list
    uint32_t directionalCount;          // the first directionalCount lights are directional (not clustered)
    float zNear, zFar;                  // metres: the cluster slices' range
    float sliceScale, sliceBias;        // slice = floor(log(z) * sliceScale + sliceBias), z = view distance in metres
    uint32_t clustersX, clustersY;      // = kClusterX, kClusterY (shaders read these)
};
static_assert(sizeof(LightListHeader) == 32);

struct ClusterCell { uint32_t offset, count; };   // into the index list: uint32 light indices

}  // namespace gt4::render
```

- `sliceScale = kClusterZ / log(zFar / zNear)`, `sliceBias = -kClusterZ * log(zNear) / log(zFar / zNear)`.
- Attenuation (every reader uses `light_list.glsl`'s functions, not its own):
  `falloff = saturate(1 - (d / range)^4)^2 / (d^2 + sourceRadius^2)`;
  spot `cone = saturate(dot(-L, direction) * spotScale + spotOffset)^2`.
- Metres: `kGameUnitsPerMetre = 1.0` (spike R1). Intensity: the game's colour times a per-source table on the host
  (`platform/render/modern/light_sources.cpp`), tuned against references, a setting under `lighting.night`.

### c.2 Who writes what

| Buffer | Writer | Readers |
|---|---|---|
| `LightListHeader` + `GpuLight[]`, per view per slot | `LightGather` pass (F lane): `SceneFrame::lights` of the view, converted to metres and the view's space; the course lamps the host finds in the course meshes (F2b, without `kFromGame`); plus the sun from the batches' light matrices (B lane, B10) | everyone below |
| `ClusterCell[kClusterX * kClusterY * kClusterZ]` + index list | `LightClusters` compute pass (F1), the only writer | deferred lighting, clustered forward, froxel fog, the GS-path night relight |

Published on the blackboard as `struct LightList { rg::Buffer header, lights, cells, indices; }`.

### c.3 The GS-path night relight (before the deferred renderer exists)

Night lighting can ship first on today's renderer: at a Light command, rebuild view-space positions from GT4's Z
(as `gsn_view.glsl` does), normals from depth, and add the clustered lights. It uses the same `LightList`, with
the cluster grid built over the GS scene rectangle at the render scale. When the view moves to the modern
renderer, the same shader functions run in the deferred lighting pass with G-buffer normals. Nothing in c.1
changes between the two.

**The albedo estimate (F2 step 2, lane F's design, agreed 2026-11-08).** The GS path has no albedo, only GT4's lit
colour. It assumes GT4's lighting varies slowly over the screen and the textures quickly. So:
`albedo = colour_linear / L_local * 0.18`, clamped to [0, 0.9]. `L_local` is a 1/8-resolution image of the
log-average luminance, sampled bilinearly. That keeps texture and hue without GT4's dark night exposure. A fixed
grey with GT4's chroma would lose the road markings, and colour times a gain would saturate lit areas.
- `L_local` counts only pixels with geometry (GT4's Z inside the view's depth range): the sky and the clear colour
  are left out, so they don't pull it, and they get no added light. The log average keeps small bright spots (lamp
  heads, headlights) from lifting it. A floor of 1e-3 keeps black areas finite; the clamp does the rest.
- Each 1/8 block stores the sum of its pixels' log luminance and their count. Both are sampled bilinearly and
  divided after, so a block with no geometry weighs 0 near the skyline. A pixel whose four neighbouring blocks are
  all empty gets no added light.
- `L_local` is not smoothed over time in step 2: the N1 acceptance shows whether it has to be, and a history buffer
  waits for that evidence.
- The light is added in `gsn_light.comp`, in its linear light (its 2.2 decode): after the occlusion multiply
  (lamp light is not ambient), before the host's enhanced grade, and before GT4's own grade and post effects.
  The per-pixel cluster loop (Lambert, `light_list.glsl`'s falloff and cone) runs in that same pass. So the cost is
  one 1/8 luminance pass plus the existing full pass.
- Positions and distances come from the view's `ProjectionData` only (its projection and viewport give GT4's
  exact Z for a distance, R1), not `gsn_ao.comp`'s measured `1.6e6 / d`, which is about 8 % off at 100 m and would
  shift the falloff. There are no lights without scene records, so no fallback to `lighting.tanX/tanY` is needed.
  Normals come from depth, from the nearer neighbour on each axis, as `gsn_ao.comp` does.
- The mirror inset (`Lighting::hasInset`) is another view, with its own projection, and stays unlit in step 2.
- It is an interim estimate: the modern views use their G-buffer albedo (B10).
