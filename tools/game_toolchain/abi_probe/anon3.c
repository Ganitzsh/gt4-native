struct T { int z; };
struct S2 { int a; struct T; int b; };
struct S3 { int a; struct U { int q, r; }; int b; };
int sz2 = sizeof(struct S2), sz3 = sizeof(struct S3);
int q3(struct S3 *s) { return s->q; }
