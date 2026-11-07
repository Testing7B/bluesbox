#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# OpenPocket — writes the rdq routines of engine/px_nam.c (pi32v2 assembly, as C string lines): a NAM layer's rows for 4
# frames straight from the history ring, unrolled for 6, 15 and 16 taps, each multiply-add using a value loaded 2 to 4
# steps before (on the pedal a multiply-add paired with the load of the value it needs next costs 3.2 cycles, without
# that wait 2.2). Paste its output into px_nam.c's asm block (the lines after "rdq6, rdq15, rdq16").
#   python3 tools/gen_rdq.py > rdq.inc
ACC = ["r9_r8", "r11_r10", "r13_r12", "r15_r14"]
X = ["r3", "r4", "r6", "r7"]
def gen(K):
    n = "pxn_rdq%d_asm" % K
    L = []
    a = L.append
    a("\t.global %s" % n)
    a("%s:" % n)
    a("\t[--sp] = {rets, r15-r4}")
    a("\tsp += -16")
    a("\t[sp+0] = r0")
    a("\tr7 = [r0+20]\n\t[sp+4] = r7")
    a("\tr7 = [r0+16]\n\t[sp+8] = r7")
    a("\tr7 = [r0+4]\n\t[sp+12] = r7")
    a("\tr0 = [r0+0]")
    a(".L%so:" % n)
    a("\tr4 = [sp+0]\n\tr7 = [r4+12]")
    a("\tr5 = [r0++=4]\n\tr6 = [r0++=4]")
    for f in range(4):
        a("\tr3 = [r7+%d]" % (4 * f))
        a("\t%s = r3 * r5 (s)" % ACC[f])
    a("\tr3 = 1048576")
    for f in range(4):
        a("\t%s += r3 * r6 (s)" % ACC[f])
    a("\tr1 = [sp+12]\n\tr1 = [r1+0]")
    a("\tr5 = [r0++=4]")
    for f in range(4):
        a("\t%s = [r1+%d]" % (X[f], 12 * f))
    for k in range(K):
        A = "r1" if k % 2 == 0 else "r2"
        B = "r2" if k % 2 == 0 else "r1"
        last = k == K - 1
        for c in range(3):
            if c < 2:
                base, off = A, 4 * (c + 1)
            else:
                base, off = B, 0
            for f in range(4):
                if f < 3:
                    if c == 2 and last:
                        a("\t%s += %s * r5 (s)" % (ACC[f], X[f]))
                    else:
                        a("\t%s += %s * r5 (s)  #  %s = [%s+%d]" % (ACC[f], X[f], X[f], base, off + 12 * f))
                else:
                    a("\t%s += %s * r5 (s)  #  r5 = [r0++=4]" % (ACC[f], X[f]))
            if not (c == 2 and last):
                a("\tr7 = [%s+%d]" % (base, off + 36))
            if c == 0 and not last:
                a("\t%s = [sp+12]\n\t%s = [%s+%d]" % (B, B, B, 4 * (k + 1)))
    a("\tr0 += -4")
    for f in range(4):
        a("\t%s >>= 24" % ACC[f])
    a("\tr4 = [sp+8]")
    a("\t[r4+0] = r8\n\t[r4+12] = r10\n\t[r4+24] = r12\n\t[r4+36] = r14")
    a("\tr4 += 4\n\t[sp+8] = r4")
    a("\tr7 = [sp+4]\n\tr7 += -1\n\t[sp+4] = r7")
    a("\tif (r7 != 0) goto .L%so" % n)
    a("\tsp += 16")
    a("\t{pc, r15-r4} = [sp++]")
    out = []
    for ln in "\n".join(L).split("\n"):
        out.append('    "%s\\n"' % ln.replace("\t", "\\t"))
    return "\n".join(out)
print("\n".join(gen(K) for K in (6, 15, 16)))
