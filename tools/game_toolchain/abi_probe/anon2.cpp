union V2 { struct { float x, y; }; float v[2]; void normalize(); };
void V2::normalize() { float l = x * x + y * y; x /= l; y /= l; }
struct C { int k; struct { short a, b; }; int m; };
int sz_V2 = sizeof(V2), sz_C = sizeof(C);
int g(C *c, V2 *v) { return c->a + c->b + c->m + (int)v->y; }
