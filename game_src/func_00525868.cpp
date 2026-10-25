struct Buf { char pad0[0x1b]; unsigned char flag; char pad1c[0x44]; int value; };
struct Part { Buf *data; int size; };
struct Packet {
    Part parts[8]; int size,count;
    void reset() { count=0; size=0; }
    void add(Buf *data) { int c=count; parts[c].data=data; parts[c].size=sizeof(Buf); size=size+sizeof(Buf); count=count+1; }
};
struct Rec { int state; char pad[0x40]; };
struct Obj { char pad0[0x84]; Rec *records; char pad88[0x14]; unsigned short count; char pad9e[0x2a]; int value; };
extern "C" void func_00526040(Obj *,int *,unsigned short *);
extern "C" int func_00526128(Obj *,unsigned short,Buf *);
extern "C" int func_00527488(Obj *,Rec *,unsigned short,Packet *);
extern "C" int func_00527660(Obj *,unsigned short);
static inline unsigned short invalid_id() { return 0xFFFE; }
extern "C" int func_00525868(Obj *o,Rec *r,unsigned indexArg) {
    Buf buf;
    Packet packet;
    unsigned short other=invalid_id();
    Buf buf2;
    int state=0;
    unsigned short index=indexArg;
    func_00526040(o,&state,&other);
    if (r->state == 2) {
        r->state=3;
        if (state==7) {
            int result=func_00526128(o,other,&buf);
            if(result) return result;
            packet.reset();
            packet.add(&buf);
            result=func_00527488(o,r,0xf000,&packet);
            if(result) return result;
            result=func_00526128(o,index,&buf);
            if(result) return result;
            Rec *rec=o->records;
            for(unsigned short i=0;i<o->count;++i,++rec) {
                if(rec->state==3 && i!=index && i!=other) {
                    func_00526128(o,i,&buf2);
                    packet.reset();
                    packet.add(&buf2);
                    result=func_00527488(o,r,0xf000,&packet);
                    if(result) return result;
                    packet.reset();
                    packet.add(&buf);
                    result=func_00527488(o,rec,0xf000,&packet);
                    if(result) return result;
                }
            }
            buf.flag=1;
            buf.value=o->value;
            packet.reset();
            packet.add(&buf);
            result=func_00527488(o,r,0xf000,&packet);
            if(result) return result;
        }
    } else if (r->state == 1) {
        r->state=4;
    } else {
        return 1;
    }
    return func_00527660(o,index);
}
/* native correction of src/network/unit_00494578/func_00525868.cpp: returns each early-exit's nonzero result, 1 when the record state is neither 2 nor 1, else func_00527660's result */
