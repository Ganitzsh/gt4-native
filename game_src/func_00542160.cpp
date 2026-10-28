/* compiler: ee-gcc2.96 */
/* Native code for 00542160 (unmatched; from the decomp's draft drafts/00542160/00542160_7a6cebcd9495a505.cpp,
 * corrected for the native calling convention): walks the node tree from s->root (00541DD8 pops the
 * next node, 00541D78 pushes one) in post-order and releases each node whose children are all done
 * with 00542310.
 * 00542310 takes (s, node) and frees that node's members (0x542314: $a1 is the node it reads). The
 * original's jal at 0x5422A0 sets only $a0 = s and leaves $a1 = the node just popped (loaded from the
 * stack slot at 0x5421E4 and copied to $a1 at 0x5421EC; the child scan only reads through it). The draft
 * passed s alone, which matched on the EE; natively the node came from the stack slot after s
 * (garbage), so 00542310 freed a garbage pointer's members. */
struct Node { char pad[0x10c]; Node **children; unsigned count; };
struct S { char pad[0x8008]; Node *root; };
extern "C" int func_00541D78(S *, Node *) throw();
extern "C" int func_00541DD8(S *, Node **) throw();
extern "C" void func_00542310(S *, Node *);
extern "C" int func_00542160(S *s) {
    Node *last = 0;
    Node *node = 0;
    int r;
    while ((r = func_00541DD8(s, &node)) == 0) {
        if (node == 0) break;
    }
    if (r) return r;
    if (!s->root) return 0;
    r = func_00541D78(s, s->root);
    if (r) return r;
    do {
        r = func_00541DD8(s, &node);
        if (r) return r;
        if (!node) return 0;
        unsigned next = 0;
        for (unsigned i = 0; i < node->count; ++i) {
            if (node->children[i] == last) { next = i + 1; break; }
        }
        if (next < node->count) {
            r = func_00541D78(s, node);
            if (r) return r;
            r = func_00541D78(s, node->children[next]);
            if (r) return r;
        } else {
            if (next != node->count) return 0;
            func_00542310(s, node);
            last = node;
        }
    } while (node);
    return 0;
}
