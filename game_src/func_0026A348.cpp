/* compiler: ee-gcc2.96-no-strict-aliasing */
struct Node { int color; Node *parent,*left,*right; char pad[8]; };
struct Tree { int alloc; Node *root; int count; unsigned char flag; char pad[3]; };
struct Obj { int ref; void *vt; int a; void *b; int x,y,z,w; Tree tree; int end; };
extern char mWindowContext__vtable[];
extern "C" void *hObject__structor_0(void *);
extern "C" const char **func_005DF780();
extern "C" void *func_00326750(unsigned int, int, const char *);
extern "C" Node *mWindowContext__structor_0(Obj *o, int x, int z) {
    int spare[4];
    hObject__structor_0(o);
    Tree *t = &o->tree;
    o->x = x;
    o->vt = mWindowContext__vtable;
    int one = 1;
    o->z = z;
    o->y = 0;
    o->w = 0;
    t->root = 0;
    Node *n = (Node *)func_00326750(0x18,4,*func_005DF780());
    t->count = 0;
    t->root = n;
    t->flag = 0;
    n->color = 0;
    t->root->parent = 0;
    t->root->left = t->root;
    t->root->right = t->root;
    o->end = one;
    return t->root;
}
/* native correction of src/ui/mWindowContext/mWindowContext__structor_0.cpp: returns the tree's new root node (tree.root), left in $v0 at the end on the PS2 */
