"""MIPS (EE) inline asm statements of the decomp's sources as C, for the native i386 compile.

tools/game_cc.py first rewrites the one-instruction statements it knows (its ASM_C table); every other
asm statement comes here. A statement whose instructions are all known becomes a C block:

    ({ temporaries;  inputs copied in;  one C statement per instruction;  outputs copied out; })

- every register operand ("r", "d", "=r", "+r", tied "0"...) is a __gt4_reg temporary, the EE's
  128-bit GPR (compat/game/gt4_asm.h); "f" operands are float temporaries; "m" operands are used by
  address; an input tied to an output ("0") initialises that output's temporary, as the register
  allocator would;
- explicit registers ($8, $v0, $f2) are file-scope statics, so a value can go from one asm statement
  to the next as it does in the original code ($28/$gp goes through the runtime's GetGp/SetGp);
- the EE keeps 128 bits in a register even when the C variable bound to it is 64-bit or smaller:
  `qmfc2 %0, $vf1 : "=r"(lo)` then `pcpyud %0, %1, %1 : "=r"(hi) : "r"(lo)` reads the high half that
  qmfc2 left in lo's register. So a plain variable that is an asm output and an asm input of the
  same file gets a shadow static holding the full register it was last given; an input takes the
  shadow when its low bytes still equal the variable's value;
- VU0 registers are the program-wide __gt4_vu0 register file (runtime/native/vu0.cpp), which the
  VU0 macro-mode instructions operate on; vcallms calls the runtime's VU0 microprogram runner.

Inside a single-line macro, `"... $vf" #n` stringification is kept as the C expression n (the VU
register index), which the preprocessor substitutes.

translate(body, ctx) returns the C text (bytes) or None when an instruction is not known (the asm is
then left as it is and the compile fails as before)."""
import re

ABI = {"zero": 0, "at": 1, "v0": 2, "v1": 3, "a0": 4, "a1": 5, "a2": 6, "a3": 7,
       "t0": 8, "t1": 9, "t2": 10, "t3": 11, "t4": 12, "t5": 13, "t6": 14, "t7": 15,
       "s0": 16, "s1": 17, "s2": 18, "s3": 19, "s4": 20, "s5": 21, "s6": 22, "s7": 23,
       "t8": 24, "t9": 25, "k0": 26, "k1": 27, "gp": 28, "sp": 29, "fp": 30, "s8": 30, "ra": 31}
FIELDS = "xyzw"


class Unsupported(Exception):
    pass


def split_top(text, sep):
    """text split at sep where no bracket or string is open."""
    parts, depth, cur, i, quote = [], 0, "", 0, None
    while i < len(text):
        c = text[i]
        if quote:
            cur += c
            if c == "\\":
                cur += text[i + 1:i + 2]
                i += 1
            elif c == quote:
                quote = None
        elif c in "\"'":
            quote = c
            cur += c
        elif c in "([{":
            depth += 1
            cur += c
        elif c in ")]}":
            depth -= 1
            cur += c
        elif c == sep and depth == 0:
            parts.append(cur)
            cur = ""
        else:
            cur += c
        i += 1
    parts.append(cur)
    return parts


_OPERAND = re.compile(r'^\s*"([^"]*)"\s*\((.*)\)\s*$', re.S)
_TOKEN = re.compile(r'\s*(?:"((?:[^"\\]|\\.)*)"|#\s*([A-Za-z_]\w*))', re.S)


def template_of(part):
    """The asm template: its string literals joined (C escapes \\n \\t decoded); `#param`
    (stringification in a macro) becomes {{param}}. None if anything else is there."""
    out, pos = [], 0
    part = part.replace("\\\n", " ")
    while pos < len(part):
        if not part[pos:].strip():
            break
        m = _TOKEN.match(part, pos)
        if not m:
            return None
        if m.group(1) is not None:
            s = m.group(1)
            s = s.replace("\\n", "\n").replace("\\t", " ").replace('\\"', '"').replace("\\\\", "\\")
            out.append(s)
        else:
            out.append("{{%s}}" % m.group(2))
        pos = m.end()
    return "".join(out)


def operands(section):
    out = []
    for o in split_top(section, ","):
        if not o.strip():
            continue
        m = _OPERAND.match(o)
        if not m:
            return None
        out.append((m.group(1).strip(), m.group(2).strip()))
    return out


def parse(body):
    """(template, outputs, inputs, clobbers) of an asm statement's parenthesised text, or None."""
    parts = split_top(body, ":")
    tmpl = template_of(parts[0])
    if tmpl is None or len(parts) > 4:
        return None
    outs = operands(parts[1]) if len(parts) > 1 else []
    ins = operands(parts[2]) if len(parts) > 2 else []
    if outs is None or ins is None:
        return None
    clob = re.findall(r'"([^"]*)"', parts[3]) if len(parts) > 3 else []
    return tmpl, outs, ins, clob


IDENT = re.compile(r"^[A-Za-z_]\w*$")


def kind_of(constraint):
    c = constraint.lstrip("=+&%")
    if c.isdigit():
        return "tie"
    if "f" in c:
        return "f"
    if any(x in c for x in "mVo") and not any(x in c for x in "rdg"):
        return "m"
    if c and all(x in "inIJKLMNOPsEF" for x in c):
        return "i"
    return "r"


def scan_shadow_names(asm_bodies):
    """Plain variables that are a register output of one asm statement and a register input of
    another (or the same): they get a shadow of their full 128-bit register."""
    outs, ins = set(), set()
    for body in asm_bodies:
        p = parse(body)
        if not p:
            continue
        _, o, i, _ = p
        for c, e in o:
            if kind_of(c) == "r" and IDENT.match(e):
                outs.add(e)
        for c, e in i:
            if kind_of(c) in ("r", "i") and IDENT.match(e):
                ins.add(e)
    return outs & ins


class Ctx:
    """Per file: the shadowed names and the file-scope statics the translations need."""

    def __init__(self, shadow=()):
        self.shadow = set(shadow)
        self.statics = {}  # name -> declaration

    def need(self, name, decl):
        self.statics[name] = decl

    def prologue(self):
        out = []
        for name in sorted(self.statics):
            out.append("#ifndef __GT4_S_%s\n#define __GT4_S_%s\n%s\n#endif\n" % (name, name, self.statics[name]))
        return "".join(out)


INT = re.compile(r"^-?(?:0[xX][0-9a-fA-F]+|\d+)$")
ZERO_CAST = re.compile(r"^\(\s*\w+\s*\)\s*0$")


class Stmt:
    def __init__(self, ctx, outs, ins):
        self.ctx = ctx
        self.decls, self.pre, self.body, self.post = [], [], [], []
        self.ops = []  # (kind, expr, temp name or None)
        n_out = len(outs)
        allops = outs + ins
        temps = {}
        for k, (c, e) in enumerate(allops):
            kind = kind_of(c)
            if kind == "tie":
                j = int(c.lstrip("=+&%"))
                if j >= n_out:
                    raise Unsupported("tie to an input")
                jk = self.ops[j][0]
                self.ops.append((jk, e, temps[j]))
                self.init_temp(jk, temps[j], e)
                continue
            if kind == "m":
                self.ops.append(("m", e, None))
                continue
            t = ("__gt4_r%d" if kind in ("r", "i") else "__gt4_f%d") % k
            temps[k] = t
            if kind in ("r", "i"):
                self.decls.append("__gt4_reg %s = { { 0, 0 } };" % t)
            else:
                self.decls.append("float %s = 0.0f;" % t)
            self.ops.append(("f" if kind == "f" else "r", e, t))
            if k >= n_out or c.startswith("+"):
                self.init_temp(self.ops[k][0], t, e)
            if k < n_out:
                if self.ops[k][0] == "f":
                    self.post.append("__GT4_FROM_FPR ((%s), %s);" % (e, t))
                else:
                    self.post.append("__GT4_FROM_REG ((%s), %s);" % (e, t))
                    if IDENT.match(e) and e in ctx.shadow:
                        self.need_shadow(e)
                        self.post.append("__gt4_sh_%s = %s;" % (e, t))
        self.gp_read = self.gp_written = False
        self.locals = set()

    def init_temp(self, kind, t, e):
        if ZERO_CAST.match(e):
            return  # `(u128) 0`: the temporary starts at zero (no conversion from int to the struct)
        if kind == "f":
            self.pre.append("__GT4_TO_FPR (%s, (%s));" % (t, e))
        elif IDENT.match(e) and e in self.ctx.shadow:
            self.need_shadow(e)
            self.pre.append("__GT4_TO_REG_SH (%s, (%s), __gt4_sh_%s);" % (t, e, e))
        else:
            self.pre.append("__GT4_TO_REG (%s, (%s));" % (t, e))

    def need_shadow(self, name):
        self.ctx.need("sh_" + name, "static __gt4_reg __gt4_sh_%s;" % name)

    def operand(self, a):
        m = re.match(r"^%z?(\d+)$", a)
        if not m:
            return None
        k = int(m.group(1))
        if k >= len(self.ops):
            raise Unsupported("operand number")
        return self.ops[k]

    # --- accessors: C lvalues / expressions for instruction arguments ---
    def gpr(self, a, write=False):
        op = self.operand(a)
        if op:
            if op[0] != "r":
                raise Unsupported("not a register operand")
            return op[2]
        m = re.match(r"^\$(\d+|[a-z]\w*)$", a)
        if not m:
            raise Unsupported("register " + a)
        r = m.group(1)
        n = int(r) if r.isdigit() else ABI.get(r)
        if n is None or n > 31:
            raise Unsupported("register " + a)
        if n == 0:
            name = "__gt4_sink" if write else "__gt4_zero"
            if name not in self.locals:
                self.locals.add(name)
                self.decls.append("__gt4_reg %s = { { 0, 0 } };" % name)
            return name
        if n == 28:
            if "__gt4_rgp" not in self.locals:
                self.locals.add("__gt4_rgp")
                self.decls.append("__gt4_reg __gt4_rgp = { { 0, 0 } };")
            if write:
                self.gp_written = True
            else:
                self.gp_read = True
            return "__gt4_rgp"
        if n in (29, 31):
            raise Unsupported("$sp/$ra")
        return self.static_gpr(n)

    def static_gpr(self, n):
        self.ctx.need("gpr%d" % n, "static __gt4_reg __gt4_gpr%d;" % n)
        return "__gt4_gpr%d" % n

    def fpr(self, a):
        op = self.operand(a)
        if op:
            if op[0] != "f":
                raise Unsupported("not a float operand")
            return op[2]
        m = re.match(r"^\$f(\d+)$", a)
        if not m:
            raise Unsupported("fpr " + a)
        n = int(m.group(1))
        self.ctx.need("fpr%d" % n, "static float __gt4_fpr%d;" % n)
        return "__gt4_fpr%d" % n

    def facc(self):
        self.ctx.need("facc", "static float __gt4_facc;")
        return "__gt4_facc"

    def imm(self, a):
        op = self.operand(a)
        if op:
            if op[0] == "m":
                raise Unsupported("memory as immediate")
            return "(%s)" % op[1]
        if INT.match(a):
            return "(%s)" % a
        raise Unsupported("immediate " + a)

    def mem(self, a):
        """The address of a memory argument, as a char * expression."""
        op = self.operand(a)
        if op:
            if op[0] != "m":
                raise Unsupported("register as memory")
            return "((char *) &(%s))" % op[1]
        m = re.match(r"^(.*)\((.*)\)$", a)
        if not m:
            raise Unsupported("memory " + a)
        off = m.group(1).strip()
        base = self.gpr(m.group(2).strip())
        off = self.imm(off) if off else "0"
        return "((char *) %s.w[0] + %s)" % (base, off)

    def vf(self, a):
        """(C array expression, field index or None) for $vfN, $vfNx, $vfN.x, ACC."""
        if a.upper() == "ACC" or a == "$ACC":
            return "__gt4_vu0.acc", None
        m = re.match(r"^\$vf(\d+|\{\{\w+\}\})(?:\.?([xyzw]+))?$", a)
        if not m:
            raise Unsupported("vf " + a)
        idx = m.group(1)
        if idx.startswith("{{"):
            idx = "(%s)" % idx[2:-2]
        f = m.group(2)
        return "__gt4_vu0.vf[%s]" % idx, (FIELDS.index(f) if f and len(f) == 1 else None)

    def vi(self, a):
        m = re.match(r"^\(?\$vi(\d+|\{\{\w+\}\})(\+\+|--)?\)?$", a)
        if not m:
            raise Unsupported("vi " + a)
        idx = m.group(1)
        return ("(%s)" % idx[2:-2]) if idx.startswith("{{") else idx

    def finish(self):
        pre = list(self.pre)
        if self.gp_read:
            pre.append("__gt4_rgp.sd[0] = (int) __gt4_ee_get_gp ();")
        post = list(self.post)
        if self.gp_written:
            post.insert(0, "__gt4_ee_set_gp ((void *) __gt4_rgp.w[0]);")
        if not self.body and not post:
            return '__asm__ __volatile__ ("" : : : "memory")'
        return "({ " + " ".join(self.decls + pre + self.body + post) + " (void) 0; })"


def mask_of(dest):
    if not dest:
        return 15
    m = 0
    for c in dest:
        if c not in FIELDS:
            raise Unsupported("dest " + dest)
        m |= 1 << FIELDS.index(c)
    return m


VU_ARITH = re.compile(r"^v(add|sub|mul|madd|msub|max|mini)(a?)([xyzwqi]?)$")
VU_MOVE = {"vmove": 0, "vabs": 1, "vftoi0": 2, "vftoi4": 3, "vftoi12": 4, "vftoi15": 5,
           "vitof0": 6, "vitof4": 7, "vitof12": 8, "vitof15": 9, "vmr32": 10}
MMI_LANES = {"padd": 0, "psub": 1, "pceq": 2, "pcgt": 3, "pmax": 4, "pmin": 5,
             "paddu": 6, "psubu": 7, "padds": 8, "psubs": 9}
NOPS = {"sync", "sync.l", "sync.p", "nop", "ssnop", "vnop", "vwaitq", "vwaitp", "pref", "cache", "ehb"}


def insn(st, mn, args):
    """Appends the C for one instruction to st.body."""
    B = st.body.append
    a = args
    dest = None
    if mn.startswith("v") and "." in mn:
        mn, dest = mn.split(".", 1)
    elif re.match(r"^(qmtc2|qmfc2|cfc2|ctc2)\.(i|ni)$", mn):
        mn = mn.split(".")[0]

    def n(k):
        if len(a) != k:
            raise Unsupported("%s takes %d arguments" % (mn, k))

    if mn in NOPS:
        return
    # ---- VU0 macro mode ----
    m = VU_ARITH.match(mn)
    if m and mn not in VU_MOVE:
        op, acc, bc = m.groups()
        n(3)
        mask = mask_of(dest)
        d = "__gt4_vu0.acc" if acc else st.vf(a[0])[0]
        if acc and a[0].upper() != "ACC":
            raise Unsupported("acc form without ACC")
        s = st.vf(a[1])[0]
        if bc in ("q", "i"):
            t, b = "&__gt4_vu0.%s" % bc, 0
        else:
            t, tf = st.vf(a[2])
            b = FIELDS.index(bc) if bc else -1
        B("__gt4_v%s (%s, %s, %s, %d, %d);" % (op, d, s, t, b, mask))
        return
    if mn in VU_MOVE:
        n(2)
        B("__gt4_vmove (%s, %s, %d, %d);" % (st.vf(a[0])[0], st.vf(a[1])[0], mask_of(dest), VU_MOVE[mn]))
        return
    if mn in ("vopmula", "vopmsub"):
        n(3)
        d = "__gt4_vu0.acc" if mn == "vopmula" else st.vf(a[0])[0]
        B("__gt4_vopm (%s, %s, %s, %d);" % (d, st.vf(a[1])[0], st.vf(a[2])[0], mn == "vopmsub"))
        return
    if mn in ("vdiv", "vrsqrt"):
        n(3)
        (s, sf), (t, tf) = st.vf(a[1]), st.vf(a[2])
        if sf is None or tf is None:
            raise Unsupported("vdiv fields")
        den = "%s[%d]" % (t, tf) if mn == "vdiv" else "__gt4_f_sqrt (__gt4_f_abs (%s[%d]))" % (t, tf)
        B("__gt4_vu0.q = %s[%d] / %s;" % (s, sf, den))
        return
    if mn == "vsqrt":
        n(2)
        t, tf = st.vf(a[1])
        B("__gt4_vu0.q = __gt4_f_sqrt (__gt4_f_abs (%s[%d]));" % (t, tf))
        return
    if mn == "vclipw":
        n(2)
        t, tf = st.vf(a[1])
        B("__gt4_vclip (%s, %s[%d]);" % (st.vf(a[0])[0], t, 3 if tf is None else tf))
        return
    if mn == "vmfir":
        n(2)
        B("__gt4_vfill (%s, __gt4_f_bits ((unsigned int) (int) (short) __gt4_vu0.vi[%s]), %d);"
          % (st.vf(a[0])[0], st.vi(a[1]), mask_of(dest)))
        return
    if mn == "vmtir":
        n(2)
        s, sf = st.vf(a[1])
        B("__gt4_ctc2 (%s, __gt4_f_uint (%s[%d]) & 0xFFFF);" % (st.vi(a[0]), s, sf or 0))
        return
    if mn in ("viadd", "visub", "viand", "vior"):
        n(3)
        o = {"viadd": "+", "visub": "-", "viand": "&", "vior": "|"}[mn]
        B("__gt4_ctc2 (%s, (__gt4_vu0.vi[%s] %s __gt4_vu0.vi[%s]) & 0xFFFF);"
          % (st.vi(a[0]), st.vi(a[1]), o, st.vi(a[2])))
        return
    if mn in ("viaddi", "viaddiu", "visubiu"):
        n(3)
        o = "-" if mn == "visubiu" else "+"
        B("__gt4_ctc2 (%s, (__gt4_vu0.vi[%s] %s %s) & 0xFFFF);" % (st.vi(a[0]), st.vi(a[1]), o, st.imm(a[2])))
        return
    if mn in ("vlqi", "vlqd", "vsqi", "vsqd"):
        n(2)
        load = mn.startswith("vlq")
        v = st.vf(a[0] if load else a[0])[0]
        p = st.vi(a[1] if load else a[1])
        mask = mask_of(dest)
        if mn.endswith("d"):
            B("__gt4_vu0.vi[%s] = (__gt4_vu0.vi[%s] - 1) & 0xFFFF;" % (p, p))
        if load:
            B("{ float *__gt4_m = __GT4_VU0_MEM (__gt4_vu0.vi[%s]); __gt4_vmove (%s, __gt4_m, %d, 0); }"
              % (p, v, mask))
        else:
            B("{ float *__gt4_m = __GT4_VU0_MEM (__gt4_vu0.vi[%s]); __gt4_vmove (__gt4_m, %s, %d, 0); }"
              % (p, v, mask))
        if mn.endswith("i"):
            B("__gt4_vu0.vi[%s] = (__gt4_vu0.vi[%s] + 1) & 0xFFFF;" % (p, p))
        return
    if mn in ("vilwr", "viswr"):
        n(2)
        f = FIELDS.index(dest) if dest and len(dest) == 1 else 0
        t, s = st.vi(a[0]), st.vi(a[1])
        if mn == "vilwr":
            B("__gt4_ctc2 (%s, ((unsigned int *) __GT4_VU0_MEM (__gt4_vu0.vi[%s]))[%d] & 0xFFFF);" % (t, s, f))
        else:
            mask = mask_of(dest)
            for k in range(4):
                if mask & (1 << k):
                    B("((unsigned int *) __GT4_VU0_MEM (__gt4_vu0.vi[%s]))[%d] = __gt4_vu0.vi[%s] & 0xFFFF;" % (s, k, t))
        return
    if mn == "vrinit" or mn == "vrxor":
        n(2)
        s, sf = st.vf(a[1])
        x = "__gt4_f_uint (%s[%d])" % (s, sf or 0)
        if mn == "vrxor":
            x = "(__gt4_vu0.r ^ %s)" % x
        B("__gt4_vu0.r = 0x3F800000u | (%s & 0x7FFFFFu);" % x)
        return
    if mn in ("vrget", "vrnext"):
        n(2)
        if mn == "vrnext":
            B("__gt4_vrnext ();")
        B("__gt4_vfill (%s, __gt4_f_bits (__gt4_vu0.r), %d);" % (st.vf(a[0])[0], mask_of(dest)))
        return
    if mn == "vcallms":
        n(1)
        B("__gt4_vu0_callms (%s);" % st.imm(a[0]))
        return
    if mn == "vcallmsr":
        B("__gt4_vu0_callms (__gt4_vu0.vi[27] * 8);")
        return
    # ---- COP2 transfers ----
    if mn == "qmtc2":
        n(2)
        B("__gt4_qmtc2 (%s, &%s);" % (st.vf(a[1])[0], st.gpr(a[0])))
        return
    if mn == "qmfc2":
        n(2)
        B("__gt4_qmfc2 (&%s, %s);" % (st.gpr(a[0], True), st.vf(a[1])[0]))
        return
    if mn == "ctc2":
        n(2)
        B("__gt4_ctc2 (%s, %s.w[0]);" % (st.vi(a[1]), st.gpr(a[0])))
        return
    if mn == "cfc2":
        n(2)
        B("%s.sd[0] = (int) __gt4_cfc2 (%s);" % (st.gpr(a[0], True), st.vi(a[1])))
        return
    if mn in ("lqc2", "sqc2"):
        n(2)
        v = st.vf(a[0])[0]
        p = "((__gt4_reg *) ((unsigned int) %s & ~15u))" % st.mem(a[1])
        B(("__gt4_qmtc2 (%s, %s);" % (v, p)) if mn == "lqc2" else ("__gt4_qmfc2 (%s, %s);" % (p, v)))
        return
    # ---- FPU ----
    fp3 = {"add.s": "+", "sub.s": "-", "mul.s": "*", "div.s": "/"}
    if mn in fp3:
        n(3)
        B("%s = %s %s %s;" % (st.fpr(a[0]), st.fpr(a[1]), fp3[mn], st.fpr(a[2])))
        return
    fp2 = {"mov.s": "%s", "neg.s": "-%s", "abs.s": "__gt4_f_abs (%s)", "sqrt.s": "__gt4_f_sqrt (__gt4_f_abs (%s))",
           "cvt.w.s": "__gt4_f_bits ((unsigned int) __gt4_f_toint (%s))",
           "cvt.s.w": "(float) (int) __gt4_f_uint (%s)"}
    if mn in fp2:
        n(2)
        B("%s = %s;" % (st.fpr(a[0]), fp2[mn] % st.fpr(a[1])))
        return
    if mn in ("min.s", "max.s"):
        n(3)
        B("%s = __gt4_f_%s (%s, %s);" % (st.fpr(a[0]), mn[:3], st.fpr(a[1]), st.fpr(a[2])))
        return
    if mn == "rsqrt.s":
        n(3)
        B("%s = %s / __gt4_f_sqrt (__gt4_f_abs (%s));" % (st.fpr(a[0]), st.fpr(a[1]), st.fpr(a[2])))
        return
    if mn in ("adda.s", "suba.s", "mula.s"):
        n(2)
        B("%s = %s %s %s;" % (st.facc(), st.fpr(a[0]), {"adda.s": "+", "suba.s": "-", "mula.s": "*"}[mn], st.fpr(a[1])))
        return
    if mn in ("madda.s", "msuba.s"):
        n(2)
        B("%s %s= %s * %s;" % (st.facc(), "+" if mn == "madda.s" else "-", st.fpr(a[0]), st.fpr(a[1])))
        return
    if mn in ("madd.s", "msub.s"):
        n(3)
        B("%s = %s %s %s * %s;" % (st.fpr(a[0]), st.facc(), "+" if mn == "madd.s" else "-", st.fpr(a[1]), st.fpr(a[2])))
        return
    if mn == "mfc1":
        n(2)
        B("%s.sd[0] = (int) __gt4_f_uint (%s);" % (st.gpr(a[0], True), st.fpr(a[1])))
        return
    if mn == "mtc1":
        n(2)
        B("%s = __gt4_f_bits (%s.w[0]);" % (st.fpr(a[1]), st.gpr(a[0])))
        return
    # ---- MMI ----
    if mn in ("pcpyld", "pcpyud", "pand", "por", "pxor", "pnor", "ppacb", "ppach", "ppacw"):
        n(3)
        B("__gt4_%s (&%s, &%s, &%s);" % (mn, st.gpr(a[0], True), st.gpr(a[1]), st.gpr(a[2])))
        return
    m = re.match(r"^pext([lu])([bhw])$", mn)
    if m:
        n(3)
        B("__gt4_pext%s (&%s, &%s, &%s, %d);" % (m.group(2), st.gpr(a[0], True), st.gpr(a[1]), st.gpr(a[2]),
                                                 m.group(1) == "u"))
        return
    m = re.match(r"^(paddu|psubu|padds|psubs|padd|psub|pceq|pcgt|pmax|pmin)([bhw])$", mn)
    if m:
        n(3)
        B("__gt4_pl%s (&%s, &%s, &%s, %d);" % (m.group(2), st.gpr(a[0], True), st.gpr(a[1]), st.gpr(a[2]),
                                               MMI_LANES[m.group(1)]))
        return
    m = re.match(r"^ps(ll|rl|ra)([hw])$", mn)
    if m:
        n(3)
        B("__gt4_ps%s (&%s, &%s, %s, %d);" % (m.group(2), st.gpr(a[0], True), st.gpr(a[1]), st.imm(a[2]),
                                              ["ll", "rl", "ra"].index(m.group(1))))
        return
    if mn in ("pcpyh", "pexew", "prot3w", "pexcw"):
        n(2)
        B("__gt4_%s (&%s, &%s);" % (mn, st.gpr(a[0], True), st.gpr(a[1])))
        return
    if mn in ("pmulth", "pmaddh"):
        n(3)
        st.ctx.need("mhilo", "static __gt4_reg __gt4_mhi, __gt4_mlo;")
        B("__gt4_pmulth (&%s, &%s, &%s, &__gt4_mhi, &__gt4_mlo, %d);"
          % (st.gpr(a[0], True), st.gpr(a[1]), st.gpr(a[2]), mn == "pmaddh"))
        return
    if mn == "pmfhl.lh":
        n(1)
        st.ctx.need("mhilo", "static __gt4_reg __gt4_mhi, __gt4_mlo;")
        B("__gt4_pmfhl_lh (&%s, &__gt4_mhi, &__gt4_mlo);" % st.gpr(a[0], True))
        return
    if mn == "plzcw":
        n(2)
        B("__gt4_mmi_plzcw (&%s, &%s);" % (st.gpr(a[0], True), st.gpr(a[1])))
        return
    # ---- integer ----
    if mn == "move":
        n(2)
        B("%s.d[0] = %s.d[0];" % (st.gpr(a[0], True), st.gpr(a[1])))
        return
    r3 = {"addu": ("sd", "(int) (%s.w[0] + %s.w[0])"), "add": ("sd", "(int) (%s.w[0] + %s.w[0])"),
          "subu": ("sd", "(int) (%s.w[0] - %s.w[0])"), "sub": ("sd", "(int) (%s.w[0] - %s.w[0])"),
          "daddu": ("d", "%s.d[0] + %s.d[0]"), "dadd": ("d", "%s.d[0] + %s.d[0]"),
          "dsubu": ("d", "%s.d[0] - %s.d[0]"), "and": ("d", "%s.d[0] & %s.d[0]"),
          "or": ("d", "%s.d[0] | %s.d[0]"), "xor": ("d", "%s.d[0] ^ %s.d[0]"),
          "nor": ("d", "~(%s.d[0] | %s.d[0])"), "slt": ("d", "%s.sd[0] < %s.sd[0]"),
          "sltu": ("d", "%s.d[0] < %s.d[0]")}
    if mn in r3:
        n(3)
        f, e = r3[mn]
        B("%s.%s[0] = %s;" % (st.gpr(a[0], True), f, e % (st.gpr(a[1]), st.gpr(a[2]))))
        return
    ri = {"addiu": ("sd", "(int) (%s.w[0] + (unsigned int) %s)"), "addi": ("sd", "(int) (%s.w[0] + (unsigned int) %s)"),
          "daddiu": ("d", "%s.d[0] + (long long) %s"), "daddi": ("d", "%s.d[0] + (long long) %s"),
          "andi": ("d", "%s.d[0] & (%s & 0xFFFF)"), "ori": ("d", "%s.d[0] | (%s & 0xFFFF)"),
          "xori": ("d", "%s.d[0] ^ (%s & 0xFFFF)"), "slti": ("d", "%s.sd[0] < (long long) %s"),
          "sltiu": ("d", "%s.d[0] < (unsigned long long) (long long) %s")}
    if mn in ri:
        n(3)
        f, e = ri[mn]
        B("%s.%s[0] = %s;" % (st.gpr(a[0], True), f, e % (st.gpr(a[1]), st.imm(a[2]))))
        return
    sh = {"sll": ("sd", "(int) (%s.w[0] << (%s))"), "srl": ("sd", "(int) (%s.w[0] >> (%s))"),
          "sra": ("sd", "%s.sw[0] >> (%s)"), "dsll": ("d", "%s.d[0] << (%s)"), "dsrl": ("d", "%s.d[0] >> (%s)"),
          "dsra": ("sd", "%s.sd[0] >> (%s)"), "dsll32": ("d", "%s.d[0] << (32 + %s)"),
          "dsrl32": ("d", "%s.d[0] >> (32 + %s)"), "dsra32": ("sd", "%s.sd[0] >> (32 + %s)")}
    if mn in sh:
        n(3)
        f, e = sh[mn]
        B("%s.%s[0] = %s;" % (st.gpr(a[0], True), f, e % (st.gpr(a[1]), st.imm(a[2]))))
        return
    shv = {"sllv": ("sd", "(int) (%s.w[0] << (%s.w[0] & 31))"), "srlv": ("sd", "(int) (%s.w[0] >> (%s.w[0] & 31))"),
           "srav": ("sd", "%s.sw[0] >> (%s.w[0] & 31)"), "dsllv": ("d", "%s.d[0] << (%s.w[0] & 63)"),
           "dsrlv": ("d", "%s.d[0] >> (%s.w[0] & 63)"), "dsrav": ("sd", "%s.sd[0] >> (%s.w[0] & 63)")}
    if mn in shv:
        n(3)
        f, e = shv[mn]
        B("%s.%s[0] = %s;" % (st.gpr(a[0], True), f, e % (st.gpr(a[1]), st.gpr(a[2]))))
        return
    if mn in ("movz", "movn"):
        n(3)
        B("if (%s.d[0] %s 0) %s.d[0] = %s.d[0];" % (st.gpr(a[2]), "==" if mn == "movz" else "!=",
                                                   st.gpr(a[0], True), st.gpr(a[1])))
        return
    if mn == "lui":
        n(2)
        B("%s.sd[0] = (int) ((unsigned int) %s << 16);" % (st.gpr(a[0], True), st.imm(a[1])))
        return
    if mn in ("li", "dli"):
        n(2)
        B("%s.sd[0] = (long long) %s;" % (st.gpr(a[0], True), st.imm(a[1])))
        return
    loads = {"lq": None, "ld": ("d", "unsigned long long", 7), "lw": ("sd", "int", 3), "lwu": ("d", "unsigned int", 3),
             "lh": ("sd", "short", 1), "lhu": ("d", "unsigned short", 1), "lb": ("sd", "signed char", 0),
             "lbu": ("d", "unsigned char", 0)}
    if mn in loads:
        n(2)
        d, p = st.gpr(a[0], True), st.mem(a[1])
        if mn == "lq":
            B("__gt4_lq (&%s, %s);" % (d, p))
        else:
            f, t, al = loads[mn]
            B("%s.%s[0] = *(const %s *) %s;" % (d, f, t, p))
        return
    stores = {"sq": None, "sd": "unsigned long long", "sw": "unsigned int", "sh": "unsigned short", "sb": "unsigned char"}
    if mn in stores:
        n(2)
        s, p = st.gpr(a[0]), st.mem(a[1])
        if mn == "sq":
            B("__gt4_sq (&%s, %s);" % (s, p))
        else:
            B("*(%s *) %s = (%s) %s.d[0];" % (stores[mn], p, stores[mn], s))
        return
    # ---- system ----
    if mn == "mtc0":
        n(2)
        r = re.match(r"^\$(\d+)$", a[1])
        if not r:
            raise Unsupported("mtc0 register")
        B("__gt4_ee_mtc0 (%s, %s.w[0]);" % (r.group(1), st.gpr(a[0])))
        return
    if mn == "mfc0":
        n(2)
        r = re.match(r"^\$(\d+)$", a[1])
        if not r:
            raise Unsupported("mfc0 register")
        B("%s.sd[0] = (int) __gt4_ee_mfc0 (%s);" % (st.gpr(a[0], True), r.group(1)))
        return
    if mn == "ei" or mn == "di":
        B("__gt4_ee_%s ();" % mn)
        return
    if mn == "syscall":
        B("%s.sd[0] = __gt4_ee_syscall ((int) %s.w[0], %s.w[0], %s.w[0], %s.w[0], %s.w[0]);"
          % (st.static_gpr(2), st.static_gpr(3), st.static_gpr(4), st.static_gpr(5), st.static_gpr(6),
             st.static_gpr(7)))
        return
    if mn == "break":
        B("__gt4_ee_break (%s);" % (st.imm(a[0]) if a else "0"))
        return
    raise Unsupported("instruction " + mn)


def instructions(tmpl):
    out = []
    for line in re.split(r"[\n;]", tmpl):
        line = line.strip()
        if not line or line.startswith(".") or line.startswith("#"):
            continue
        if re.match(r"^[\w.$]+:", line):
            raise Unsupported("label")
        parts = line.split(None, 1)
        mn = parts[0].lower()
        args = [x.strip() for x in split_top(parts[1], ",")] if len(parts) > 1 else []
        out.append((mn, args))
    return out


def translate(body, ctx):
    """C for an asm statement (the text between its parentheses, str), or None."""
    p = parse(body)
    if p is None:
        return None
    tmpl, outs, ins, clob = p
    if not tmpl.strip():
        return None  # empty asm (a barrier): already valid i386
    try:
        insns = instructions(tmpl)
        st = Stmt(ctx, outs, ins)
        for mn, args in insns:
            insn(st, mn, args)
        return st.finish()
    except (Unsupported, ValueError, IndexError):
        return None
