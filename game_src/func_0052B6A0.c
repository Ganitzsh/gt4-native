/* compiler: ee-gcc2.96-nogcse-as2004 */
/* Native copy of the provisional draft for 0052B6A0: the check() callback also gets id, port and size, which the EE forwards to it in $a1-$a3 (passthrough_lint) */
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
struct Key { int lo, hi; };
struct Raw { u8 b[4]; };
struct Conn { char pad[0x44]; char timer[0x40]; int handle; };
struct Packet { u16 flags; u8 data[2]; };
struct Self { char pad[0x4c]; struct Packet *packet; };
struct Ops { char pad[0x70]; int (*check)(struct Key); };
struct Net { char pad[0x40]; int (*send)(int, int, int, u8 *, u16); };
extern struct Ops *D_0064B4B0;
extern struct Net *D_0064B710;
struct Conn *func_0052CAE0(struct Self *, struct Packet *, struct Key, int);
int func_0053A878(void *);
int func_0052C4A8(struct Conn *, u8 *, u32, u16 *);
int func_0052C3D8(struct Conn *);
int func_0052C300(struct Self *, u8 *, struct Conn *);
int func_0052BFC0(struct Self *, struct Conn **, struct Packet *, struct Key, int);
int func_0052C9B0(struct Self *, u8 *, struct Key, int);
int func_0052C428(struct Self *, struct Packet *, struct Conn *);
int func_0052C640(struct Self *, u8 *, struct Conn *);
int func_0052BAD0(struct Self *, struct Conn *, struct Packet *, u8 *);

static inline void get_header(u8 *p, u16 *flags, u16 *size) {
    ((u8 *)flags)[0] = p[0]; ((u8 *)flags)[1] = p[1]; *flags &= 0xf800;
    if (*flags & 0x6000) *size = 4; else *size = 2;
}

int func_0052B6A0(struct Self *self, struct Key id, int port, u32 size) {
    u16 ack, len, flags;
    u16 f1, h1, f2, h2;
    struct Conn *conn;
    int token;
    int create;
    int result = 0;
    int process = 0;
    struct Packet *base;
    u8 *p, *first;
    u32 off;
    conn = 0;
    create = 0;
    if (((int (*)(struct Key, struct Key, int, u32))D_0064B4B0->check)(id, id, port, size)) return 2;
    if (port == 0) return 2;
    base = self->packet;
    off = 2;
    conn = func_0052CAE0(self, base, id, port);
    if (conn) {
        func_0053A878(conn->timer);
        if (base->flags & 0x4000) {
            int r;
            ack = 0;
            r = func_0052C4A8(conn, base->data, size - 2, &ack);
            if (r) return r;
            off = ack + 2;
        }
    }
    if (size == off) return 0;
    if (size - off < 2) return 0xd2f5;
    p = (u8 *)base + off;
    first = p;
    ((u8 *)&len)[0] = p[0]; ((u8 *)&len)[1] = p[1]; len &= 0x7ff;
    if (conn) {
        if (base->flags & 0x1000) {
            int r = func_0052C3D8(conn);
            if (r) return r;
        }
        if (base->flags & 0x2000) {
            int r = func_0052C300(self, p, conn);
            if (r) return r;
        }
    }
    if (base->flags & 0x8000) {
        if (!conn) {
            if (base->flags & 0x800) {
                int r = func_0052BFC0(self, &conn, base, id, port);
                if (r) return r;
                if (conn) { create = 1; process = 1; }
            } else if ((base->flags & 0x3ff) == 0x1c3) {
                int r = func_0052C9B0(self, first, id, port);
                if (r) return r;
            }
        } else {
            ((u8 *)&flags)[0] = first[0]; ((u8 *)&flags)[1] = first[1]; flags &= 0xf800;
            if (base->flags & 0x800) { create = 1; process = 1; }
            else if (!len) {
                int r = func_0052C428(self, base, conn);
                if (r) return r;
            } else process = 1;
        }
    }
    if (process) {
        while (off < size) {
            u8 *data, *send;
            int has;
            get_header(p, &f1, &h1);
            data = p + h1;
            send = data;
            ((u8 *)&len)[0] = p[0]; ((u8 *)&len)[1] = p[1]; len &= 0x7ff;
            ((u8 *)&flags)[0] = p[0]; ((u8 *)&flags)[1] = p[1]; flags &= 0xf800;
            get_header(p, &f2, &h2);
            off += h2 + len;
            if (flags & 0x8000) {
                *(struct Raw *)&token = *(struct Raw *)(data + len);
                off += 4;
                has = 1;
            } else {
                token = 0;
                has = 0;
            }
            if (off > size) { result = 0xd2f5; break; }
            if (has) {
                if (create) { send = data + 12; len -= 12; }
                result = 0xd2f5;
                if (D_0064B710->send(-1, conn->handle, token, send, len)) break;
            }
            if (!create) result = func_0052C640(self, p, conn);
            else result = func_0052BAD0(self, conn, base, p);
            if (result) { fail: return result; }
            p = (u8 *)base + off;
        }
        goto fail;
    }
    return 0;
}
