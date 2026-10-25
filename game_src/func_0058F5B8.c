/* compiler: ee-gcc2.9-991111 */
/* Native correction of src/libstream/unit_0057A128/func_0058F5B8.c: func_00590828 takes an int as its second
 * argument; the matched source passes an 8-byte bitfield struct by value (one register on the
 * EE, the callee reads its low word). Natively the struct took 8 stack bytes and shifted the
 * later arguments; here its low word is passed as the int. */
struct Req {
    unsigned long long a : 14;
    unsigned long long b : 2;
    unsigned long long c : 8;
    unsigned long long d : 8;
};
union ReqWord { struct Req req; int word; };
extern int func_00590828(int, int, int *, int);
int func_0058F5B8(int dev, int arg) {
    union ReqWord u;
    int out;
    int r;
    u.req.a = 2;
    u.req.b = 2;
    u.req.c = 3;
    u.req.d = 1;
    r = func_00590828(dev, u.word, &out, arg);
    if (r < 0) return r;
    return out;
}
