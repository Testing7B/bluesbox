#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# OpenPocket v34 — one-shot: replaces the amp / drive / cab tables of engine/px_dsp.c with the fitted set
# (docs/FITTED-MODELS.md), renumbers the types, rewrites the factory presets on the new types, and adds
# px_type_v34() (old type -> nearest new one, for presets saved before v34: firmware/px_store.h).
#   apply_v34.py FITDIR      (FITDIR: the fit_amp / fit_drive / fit_cab JSON files)
import json, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIT = sys.argv[1]
DSP = os.path.join(ROOT, "engine", "px_dsp.c")
HDR = os.path.join(ROOT, "engine", "px_dsp.h")
s = open(DSP).read()
h = open(HDR).read()

# ---- the new sets: (enum, name on the pedal, fit file) ----
AMPS = [("TWIN", "TWIN", "twin"), ("AC15", "AC15 TB", "ac15"), ("JCM900", "JCM900", "jcm900"),
        ("JCM2000", "JCM2000", "jcm2000"), ("MARK4", "MARK IV", "mark4"), ("SHIRLEY", "SHIRLEY", "shirley"),
        ("PROMOD", "PRO MOD", "promod"), ("5150", "5150", "5150")]
DRVS = [("TS9", "TS9", "ts9"), ("KLON", "KLON", "klon"), ("M77", "M77 OD", "m77"), ("HM2", "SWEDE HM-2", "hm2")]
CABS = [("112JN", "1x12 US OPEN", None), ("212BLUE", "2x12 BLUE", None), ("412GB", "4x12 GREEN", None),
        ("412T75", "4x12 T75", None), ("412V30", "4x12 V30", "v30"), ("112CREAM", "1x12 CREAM", None),
        ("DI", "DI (NO CAB)", None)]
OPEN_BACK = ("112JN", "212BLUE")

# ---- old -> new (enum suffixes) ----
OLD_AMP = ["TWIN", "DELUXE", "PRINCETON", "BASSMAN", "CHAMP", "AC30", "AC15", "JTM45", "PLEXI", "JCM800", "JVM",
           "HIWATT", "ORANGE", "MATCHLESS", "DUMBLE", "SLO", "MARK", "RECTO", "5150", "5150III", "ECSTASY", "FRIEDMAN",
           "VH4", "POWERBALL", "REVV", "JC120", "SUPRO", "TWOROCK", "SVT"]
AMP_TO = {"TWIN": "TWIN", "DELUXE": "TWIN", "PRINCETON": "TWIN", "BASSMAN": "SHIRLEY", "CHAMP": "AC15", "AC30": "AC15",
          "AC15": "AC15", "JTM45": "SHIRLEY", "PLEXI": "SHIRLEY", "JCM800": "JCM900", "JVM": "JCM2000",
          "HIWATT": "JCM900", "ORANGE": "JCM2000", "MATCHLESS": "AC15", "DUMBLE": "MARK4", "SLO": "PROMOD",
          "MARK": "MARK4", "RECTO": "5150", "5150": "5150", "5150III": "5150", "ECSTASY": "PROMOD",
          "FRIEDMAN": "PROMOD", "VH4": "5150", "POWERBALL": "5150", "REVV": "5150", "JC120": "TWIN", "SUPRO": "AC15",
          "TWOROCK": "TWIN", "SVT": "TWIN"}
OLD_DRV = ["TS808", "TS9", "SD1", "KLON", "BD2", "OCD", "RAT", "DS1", "DPLUS", "BLUESBR", "KOT", "TIMMY", "ZEN", "MUFF",
           "RAMS", "RUSSIAN", "FUZZGE", "FUZZSI", "TONEB", "OCTAVIA", "FFACTORY", "RANGEM", "EPBOOST", "HM2", "MT2",
           "PRECISION", "BASSDRV"]
DRV_TO = {"TS808": "TS9", "TS9": "TS9", "SD1": "TS9", "KLON": "KLON", "BD2": "KLON", "OCD": "M77", "RAT": "HM2",
          "DS1": "HM2", "DPLUS": "M77", "BLUESBR": "KLON", "KOT": "KLON", "TIMMY": "KLON", "ZEN": "TS9",
          "MUFF": "M77", "RAMS": "M77", "RUSSIAN": "M77", "FUZZGE": "M77", "FUZZSI": "M77", "TONEB": "M77",
          "OCTAVIA": "M77", "FFACTORY": "M77", "RANGEM": "M77", "EPBOOST": "KLON", "HM2": "HM2", "MT2": "HM2",
          "PRECISION": "TS9", "BASSDRV": "KLON"}
OLD_CAB = ["110", "112JN", "210", "212BLUE", "212GB", "410BASSMAN", "412GB", "412T75", "412V30", "412H30", "112CREAM",
           "115", "810SVT", "410BASS", "DI"]
CAB_TO = {"110": "112JN", "112JN": "112JN", "210": "112JN", "212BLUE": "212BLUE", "212GB": "412GB",
          "410BASSMAN": "112JN", "412GB": "412GB", "412T75": "412T75", "412V30": "412V30", "412H30": "412V30",
          "112CREAM": "112CREAM", "115": "112JN", "810SVT": "412V30", "410BASS": "112JN", "DI": "DI"}

load = lambda n: json.load(open(os.path.join(FIT, n + ".json")))
fmt = lambda x: ("%.4g" % x) + ("f" if "." in ("%.4g" % x) or "e" in ("%.4g" % x) else ".0f")


def rep(src, a, b, n=1):
    assert src.count(a) == n, (a[:80], src.count(a))
    return src.replace(a, b)


def block(src, start, end="};"):
    i = src.index(start)
    j = src.index(end, i) + len(end)
    return i, j


# ---- enums (px_dsp.h) ----
def enum_line(prefix, items):
    names = ["PX_%s_%s" % (prefix, e) for e in items] + ["PX_%s_COUNT" % prefix]
    out, line = [], "enum {"
    for k, n in enumerate(names):
        piece = " " + n + ("," if k < len(names) - 1 else " };")
        if len(line) + len(piece) > 118:
            out.append(line); line = "      "
        line += piece
    out.append(line)
    return "\n".join(out)


for prefix, items in (("DRV", [d[0] for d in DRVS]), ("AMP", [a[0] for a in AMPS]), ("CAB", [c[0] for c in CABS])):
    m = re.search(r"enum \{ PX_%s_\w+,.*?PX_%s_COUNT \};" % (prefix, prefix), h, re.S)
    h = h[:m.start()] + enum_line(prefix, items) + h[m.end():]
h = rep(h, "int         px_param_text(", "int         px_type_v34(int blk, int old);   /* presets saved before v34: old type -> new */\nint         px_param_text(")

# ---- amp table ----
rows = []
for en, name, f in AMPS:
    d = load(f)
    v = d["values"]
    n = d["stages"]
    arr = lambda k: "{" + ",".join(fmt(v["%s%d" % (k, i)]) if i < n else "0" for i in range(4)) + "}"
    rows.append('  {"%s", %d,%d, %s, %s, %s, %s, %s,%s,%s, %s,%s,%s,%s, %s,%s,%s,%s, %s,%s,%s, %s,%s, %s,%s},  /* %s */'
                % (name, n, d["hard"], arr("g"), arr("bias"), arr("lp"), arr("hp"), fmt(v["tight"]), fmt(v["bright"]),
                   fmt(v["bright_db"]), fmt(v["bass"]), fmt(v["mid"]), fmt(v["midq10"]), fmt(v["treb"]), fmt(v["rb"]),
                   fmt(v["rm"]), fmt(v["rt"]), fmt(v["scoop"]), fmt(v["pres"]), fmt(v["res"]), fmt(v["res_db"]),
                   fmt(v["vf"]), fmt(v["vdb"]), fmt(v["sag100"]), fmt(v["pw10"]), d["capture"][:-4]))
i, j = block(s, "static const amp_v AV[PX_AMP_COUNT] = {")
s = s[:i] + "/* v34: each row fitted to a NAM capture of the real amp (tools/fit_amp.py, docs/FITTED-MODELS.md); knobs at noon\n * = the captured setting. Captures: the NAM community collection (GPL v3), credited in the comments. */\n" \
    + "static const amp_v AV[PX_AMP_COUNT] = {\n" + "\n".join(rows) + "\n};" + s[j:]

# ---- drive table ----
rows = []
for en, name, f in DRVS:
    d = load(f)
    v = d["values"]
    roles = d["roles"]
    L = "L%d(%s)" % (len(roles), ", ".join(roles))
    rows.append('    {"%s", %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s},  /* %s */'
                % (name, L, fmt(v["hp"]), fmt(v["shelf"]), fmt(v["lp"]), fmt(v["mid"]), fmt(v["mid_db"]),
                   fmt(v["mid_q10"]), fmt(v["g0"]), fmt(v["g1"]), d["clip"], d["tm"], fmt(v["t0"]), fmt(v["t1"]),
                   fmt(v["p2"]), fmt(v["p2db"]), fmt(v["blend"]), fmt(v["bias"]), fmt(v["gate"]), d["capture"][:-4][:60]))
i, j = block(s, "static const drv_v DV[PX_DRV_COUNT] = {")
s = s[:i] + "/* v34: fitted to NAM captures of the pedals (tools/fit_drive.py); knobs at noon = the captured setting */\n" \
    + "static const drv_v DV[PX_DRV_COUNT] = {\n" + "\n".join(rows) + "\n};" + s[j:]

# ---- cab table: the kept rows, V30 fitted ----
i, j = block(s, "static const cab_v CV[PX_CAB_COUNT] = {")
old_rows = dict(re.findall(r'\{"([^"]+)",\s*([^}]*)\}', s[i:j]))
rows = []
for en, name, f in CABS:
    if f:
        v = load(f)["values"]
        body = ", ".join(fmt(v[k]) for k in ("hp", "hpq10", "bump", "bump_db", "d1", "d1db", "d2", "d2db", "p1", "p1db",
                                              "p2", "p2db", "lp", "lpq"))
        rows.append('    {"%s", %s},  /* fitted: 8 amp / amp+cab capture pairs (Phillipe P, GPL) */' % (name, body))
    else:
        rows.append('    {"%s", %s},' % (name, old_rows[name].strip()))
s = s[:i] + "static const cab_v CV[PX_CAB_COUNT] = {\n    /* name hp q bump dB dip1 dB dip2 dB pres1 dB pres2 dB lp [lpq] */\n" \
    + "\n".join(rows) + "\n};" + s[j:]

# ---- per-type special cases of removed types ----
s = rep(s, "    if (t == PX_DRV_TIMMY) hp = 700.0f * powf(30.0f / 700.0f, role_val(d, q, R_BASS, 0.5f));\n", "")
s = rep(s, "    if (t == PX_DRV_PRECISION) hp = 100.0f + role_val(d, q, R_TIGHT, 0.5f) * 600.0f;\n", "")
s = rep(s, "        if (t == PX_DRV_ZEN) f = 900.0f * powf(3.3f, role_val(d, q, R_VOICE, 0.5f));\n", "")
m = re.search(r"    if \(t == PX_DRV_PRECISION\) \{.*?\n    \}\n", s, re.S); s = s[:m.start()] + s[m.end():]
m = re.search(r"    if \(t == PX_DRV_FFACTORY\) \{.*?\n    \}\n", s, re.S); s = s[:m.start()] + s[m.end():]
s = rep(s, "    if (t == PX_DRV_BASSDRV) bl = role_val(d, q, R_BLEND, 0.0f);\n    else bl *= (1.0f - 0.8f * dr);",
        "    bl *= (1.0f - 0.8f * dr);")
m = re.search(r"if \(t == PX_CAB_110 \|\| t == PX_CAB_112JN.*?\{", s, re.S)
s = s[:m.start()] + "if (%s) {" % " || ".join("t == PX_CAB_%s" % c for c in OPEN_BACK) + s[m.end():]

# ---- px_type_v34 ----
def table(name, old, to, prefix):
    return "    static const uint8_t %s[%d] = {%s};" % (name, len(old), ", ".join("PX_%s_%s" % (prefix, to[o]) for o in old))
fn = ("\n/* presets saved before v34 (store format 1 / 2): the old amp / drive / cab numbers -> the nearest of the v34 set */\n"
      "int px_type_v34(int blk, int old)\n{\n"
      + table("A", OLD_AMP, AMP_TO, "AMP") + "\n" + table("D", OLD_DRV, DRV_TO, "DRV") + "\n" + table("C", OLD_CAB, CAB_TO, "CAB") + "\n"
      "    if (old < 0) return 0;\n"
      "    if (blk == PX_B_AMP) return old < %d ? A[old] : 0;\n" % len(OLD_AMP) +
      "    if (blk == PX_B_DRIVE) return old < %d ? D[old] : 0;\n" % len(OLD_DRV) +
      "    if (blk == PX_B_CAB) return old < %d ? C[old] : 0;\n" % len(OLD_CAB) +
      "    return old;\n}\n")
s = rep(s, "\nint px_param_text(int b, int t, int k, int v, char *buf)", fn + "\nint px_param_text(int b, int t, int k, int v, char *buf)")

# ---- factory presets: the same sounds on the new types ----
i = s.index("void px_preset_factory"); j = s.index("\n}\n", i)
body = s[i:j]
for o, n in AMP_TO.items():
    body = re.sub(r"\bPX_AMP_%s\b" % o, "PX_AMP_%s" % n, body)
for o, n in DRV_TO.items():
    body = re.sub(r"\bPX_DRV_%s\b" % o, "PX_DRV_%s" % n, body)
for o, n in CAB_TO.items():
    body = re.sub(r"\bPX_CAB_%s\b" % o, "PX_CAB_%s" % n, body)
for o, n in (("TWEED BLUES", "BLUES CRUNCH"), ("TWEED CHAMP", "LITTLE AMP"), ("HIWATT STACK", "BIG STACK"),
             ("SUPRO GRIT", "GRIT CLEAN"), ("BASS DRIVE", "LOW DRIVE"), ("BASS OCTAVE", "OCTAVE DOWN"),
             ("SYNTH BASS", "SYNTH BASS")):
    body = body.replace('"%s"' % o, '"%s"' % n)
s = s[:i] + body + s[j:]

open(DSP, "w").write(s)
open(HDR, "w").write(h)
print("v34 tables applied")
