/* compiler: ee-gcc2.96-nsa-nogcse */
/* Native code for 004C2048 (unmatched; from the decomp's draft, corrected against the original): looks up
 * the records of table 0 that match `key` (func_004C3480), keeps those of the wanted type that pass
 * func_004C9580 (and func_004C8D08 for types >= 3), builds their node list with func_004C25E0, then copies
 * every node's 16-byte entries into one buffer from func_004C8C20 (stored to *out), frees the list and
 * returns the entry count (or the last count / a negative error / 0x80000325 when out of memory).
 * The draft copied the node list in a do-while loop, dereferencing the head before testing it; the original
 * tests the head first and skips the copy loop when it is null (beqz $a1 at 0x4C22B8). */
typedef unsigned char u8;
typedef unsigned short u16;
typedef struct Key { u16 a, b, id; } Key;
typedef struct Rec { u8 b[6]; } Rec;
typedef struct Tbl { int pad; u8 *data; Rec *items; char rest[0x20]; } Tbl;
typedef struct Obj { char pad[0x24]; Tbl *table; } Obj;
typedef struct Node { int a; char *data; u16 count; u16 pad; struct Node *next; } Node;
typedef struct Name { u16 id; char rest[30]; } Name;
int func_004C8ED0(Key *);
int func_004C3480(Tbl *, Key *, Rec **, Rec **, u8 **);
int func_004C9580(int *, int);
void func_004C3160(u8 *, Rec *, Name *);
int func_004C8D08(Name *, u16 *);
int func_004C25E0(Obj *, Node **, int, int, Rec *, Rec *, Rec *, int *, u8, int);
char *func_004C8C20(Obj *, int);
void func_004C8BB0(Obj *, Node *);
void *func_004C4070(void *, const void *, unsigned);

int func_004C2048(Obj *o, Key *key, int *ctx, char **out) {
    Name name;
    Tbl *t = 0;
    Rec *cur;
    Rec *end;
    u8 *extra;
    Node *head = 0;
    int wanted;
    Rec *base;
    int count;
    Rec *last;
    int total;
    int r;
    u8 i;
    wanted = func_004C8ED0(key);
    count = 0;
    total = 0;
    for (i = 0; i < 1; i++) {
        if (o->table[i].data) {
            Rec *first;
            t = &o->table[i];
            r = func_004C3480(t, key, &cur, &end, &extra);
            if (r < 0) {
                func_004C8BB0(o, head);
                return r;
            }
            first = 0;
            if (cur != end) {
                int simple = wanted < 3;
                int seen = 0;
                base = t->items;
                count = 0;
                last = 0;
                while (cur < end) {
                    u8 b = cur->b[3];
                    int type = b & 0xF;
                    int small = type < 3;
                    if (!small) {
                        if (!seen) seen = 1;
                        else extra += b >> 4;
                    }
                    if (first) {
                        if (wanted != type) break;
                        if (!simple && name.id != key->id) break;
                    }
                    if (func_004C9580(ctx, ((cur->b[4] & 0x3F) << 3) | (cur->b[5] >> 5)) == 1) {
                        if (!small && extra) func_004C3160(extra, cur, &name);
                        if (type == wanted && (simple || func_004C8D08(&name, &key->id) == 0)) {
                            if (!first) first = cur;
                            last = cur;
                            count++;
                        }
                    }
                    cur++;
                }
                if (count > 0) {
                    r = func_004C25E0(o, &head, count, wanted, first, last, base, ctx, i, 0);
                    if (r < 0) {
                        func_004C8BB0(o, head);
                        return r;
                    }
                    total += r;
                }
            }
        }
    }
    if (total > 0) {
        Node *n;
        char *dst = func_004C8C20(o, total * 16);
        if (!dst) {
            func_004C8BB0(o, head);
            return 0x80000325;
        }
        *out = dst;
        for (n = head; n; n = n->next) {
            if (n->data && n->count) {
                func_004C4070(dst, n->data, n->count * 16);
                dst += n->count * 16;
            }
        }
        func_004C8BB0(o, head);
        count = total;
    }
    return count;
}
