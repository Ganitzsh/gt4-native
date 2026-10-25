/* compiler: ee-gcc2.96-nogcse */
struct Detail { char pad[0x5b6]; unsigned char flag; };
struct Item { char pad[0x18]; Detail *detail; char pad1[0x3444-0x1c]; int kind; };
struct List { int count; int unused; Item **items; };
struct World { char pad[0x60]; List *list; char pad1[0xbc-0x64]; int flag; };
struct Owner { int unused; void *course; char pad[0x14-8]; World *world; };
extern "C" void func_00397568(void *, float);
extern "C" void func_003974D8(void *, int, float);
extern "C" void func_00394D80(Owner *self, float dt) {
    unsigned int i=0;
    void *course=self->course;
    func_00397568(course, dt);
    func_003974D8(course, 0, dt);
    func_003974D8(course, 1, dt);
    List *list=self->world->list;
    if (list->count != 0) {
        do {
            Item *item=list->items[i];
            int kind=item->kind;
            i++;
            if (kind<2 && item->detail->flag==0 && self->world->flag==0)
                func_003974D8(course, kind+2, dt);
        } while (i<(unsigned int)list->count);
    }
}
/* native correction of src/race/unit_00394D48/func_00394D80.cpp (gt4-decomp tools/argcheck.py fix --kind classes --native): classes func_003974D8 */
