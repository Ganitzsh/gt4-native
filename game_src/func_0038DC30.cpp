/* Fills the active variant of a NormalCarGeometry and selects it. The sub-object at +0x10 is built from sourceA with 0038CFD8 or, when that is
 * null, from sourceB with 0038D088. The kind byte reported by 0038DC18 then decides which block the `implementation` pointer at +0x1C refers to:
 * kind 10 uses the block at +0x0C, kind 15 initialises the block at +0x04 (0038C108) and uses it, anything else uses +0x10. The three block
 * layouts are separate geometry implementations (inferred from the field names); what kinds 10 and 15 mean is purpose unknown. */
struct Info { char pad[2]; unsigned char geometryKind; };
struct Obj {
    char pad0[4];
    char special[8];
    char implementation0C;
    char padD[3];
    char implementation10;
    char pad11[0xB];
    void *implementation;
};

extern "C" void func_0038CFD8(void *, void *, int);
extern "C" void func_0038D088(void *, void *, int);
extern "C" Info *func_0038DC18(Obj *);
extern "C" void func_0038C108(void *, int);

extern "C" void *func_0038DC30(Obj *geometry, void *sourceA, void *sourceB, int param) {
    if (sourceA != 0) {
        func_0038CFD8(&geometry->implementation10, sourceA, param);
    } else if (sourceB != 0) {
        func_0038D088(&geometry->implementation10, sourceB, param);
    }
    unsigned char geometryKind = func_0038DC18(geometry)->geometryKind;
    switch(geometryKind) {
    case 10:
        geometry->implementation = &geometry->implementation0C;
        return &geometry->implementation0C;
    case 15: {
        void *special = geometry->special;
        func_0038C108(special, param);
        geometry->implementation = special;
        break;
    }
    default:
        geometry->implementation = &geometry->implementation10;
        break;
    }
    return &geometry->implementation10;
}
/* native correction of src/race/CarGeometry_NormalCarGeometry/func_0038DC30.cpp: returns what $v0 holds on the PS2: &implementation0C for kind 10, else &implementation10 (also for kind 15: set in a branch delay slot; func_0038C108 never writes $v0). */
