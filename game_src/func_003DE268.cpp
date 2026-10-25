/* compiler: ee-gcc2.96-no-strict-aliasing */
struct E { char pad[0x10]; float start, end; char rest[0x18]; };
struct S { char pad[0x30]; E *entries; };
extern "C" int func_003DE198(S *, int) throw() __attribute__((const));
extern "C" int func_003DE190(S *, int) throw();
extern "C" int func_003DE1A0(S *, int, float) throw();
static inline E *get(S *self, int i) { return (E *)(i * 0x30 + (int)self->entries); }
extern "C" int func_003DE268(S *self, E **out, int cap, int slot, float time, float range, float period) {
 int offset = func_003DE198(self, slot);
 int size = func_003DE190(self, slot);
 int index = func_003DE1A0(self, slot, time);
 int count = 0;
 float limit = period * 0.25f;
 if(limit <= range) range = limit;
 if(index >= 0) {
  int first = index + offset;
  int prev = index;
  float start = get(self,first)->start;
  float current = time;
  float delta = start - start;
  
  while(delta < range && count < cap) {
   --prev;
   if(prev < 0) { current += period; prev += size; start += period; }
   E *entry = get(self,prev + offset);
   if(entry->start <= current && current < entry->end) { out[count++] = entry; }
   delta = start - get(self,prev + offset)->start;
  }
  while(get(self,index + offset)->start <= time && count < cap) {
   E *entry = get(self,index + offset);
   if(time < entry->end) { out[count++] = entry; }
   ++index;
   if(index >= size) { time -= period; index -= size; }
  }
 }
 return count;
}
/* native correction of src/race/unit_003DDA58/func_003DE268.cpp (gt4-decomp tools/argcheck.py fix --kind classes --native): classes func_003DE1A0 */
