struct S { int a; union { int b; float f; }; struct { short c, d; }; struct T { int z; }; int e; };
int sz = sizeof(struct S);
int get(struct S *s) { return s->a + s->b + s->c + s->d + s->e; }
void put(struct S *s, float x) { s->f = x; s->d = 3; }
int off_e = (int)&((struct S *)0)->e;
