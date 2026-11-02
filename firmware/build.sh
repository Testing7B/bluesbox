#!/bin/sh
# Build OpenPocket -> firmware/openpocket.bin (app), build/loader/ota.bin (update loader),
#                    docs/firmware/*.fwsc (packages for the web installer)   (WSL via build.bat, or Linux)
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
BSP="${BSP:-$HERE/bsp}"
TC="${JIELI_TOOLCHAIN:-$HOME/.jieli/toolchain}"
echo "Toolchain : $TC"
echo "BSP       : $BSP"
if [ ! -x "$TC/pi32v2/bin/clang" ]; then
  echo "ERREUR : pas de toolchain JieLi dans $TC"
  echo "  -> c'est celui que tu utilises deja pour compiler CLIPPER."
  echo "  -> relance avec :  JIELI_TOOLCHAIN=/chemin/vers/toolchain wsl bash build.sh"
  exit 1
fi
if [ ! -x "$TC/pi32v2/bin/ld" ]; then
  echo "ERREUR : linker '$TC/pi32v2/bin/ld' manquant (toolchain incomplet)."
  exit 1
fi
O="$HERE/build"; rm -rf "$O"; mkdir -p "$O"
CC="$TC/pi32v2/bin/clang -target pi32v2"
$CC -c "$BSP/chip/crt0.S"    -o "$O/crt0.o"
$CC -c "$BSP/chip/fm1_vec.S" -o "$O/vec.o"
$CC -c "$BSP/chip/fm1_isr.S" -o "$O/isr.o"
# 2e cœur (entrée + piles, Felucca) : seulement si openpocket.c l'active (#define DSP_CPU1 1)
CPU1OBJ=""
if grep -q "^#define DSP_CPU1 1" "$HERE/openpocket.c"; then
  $CC -c "$BSP/chip/fm1_cpu1.S" -o "$O/cpu1.o"; CPU1OBJ="$O/cpu1.o"
fi
# FPU matériel de l'AC791N (mêmes options que le SDK JieLi pour la WL82) + moteur DSP à 44,1 kHz
FP="-mcpu=r3 -mfprev1"
DSPDEF="-DPX_SR=44100.0f -DPX_OVERSAMPLE=4"
INC="-I$HERE/inc -I$HERE/../engine"
$CC $FP -Os -ffunction-sections -fno-builtin -Wall -Wno-unused-function $DSPDEF $INC \
    -I"$BSP/chip" -I"$BSP/boards/fm1" -c "$HERE/openpocket.c" -o "$O/openpocket.o"
$CC $FP -O2 -fno-builtin $DSPDEF $INC -c "$HERE/../engine/px_dsp.c" -o "$O/px_dsp.o"
$CC $FP -O2 -fno-builtin -c "$HERE/px_math.c" -o "$O/px_math.o"
# pilote I2S officiel JieLi (bitcode LLVM du SDK AC79, Apache-2.0) -> objet pi32v2
$CC -Os -c -x ir "$HERE/sdk/iis.bc" -o "$O/iis.o"
"$TC/pi32v2/bin/ld" -T "$HERE/app.ld" "$O/crt0.o" "$O/vec.o" "$O/isr.o" $CPU1OBJ "$O/openpocket.o" "$O/px_dsp.o" "$O/px_math.o" "$O/iis.o" -o "$O/openpocket.elf"
# carte des symboles : sert à traduire une adresse de crash (PC) en nom de fonction
"$TC/common/bin/objdump" -t "$O/openpocket.elf" 2>/dev/null | sort > "$O/symbols.txt" || true
"$TC/common/bin/objcopy" -O binary -j .text     "$O/openpocket.elf" "$O/text.bin"
"$TC/common/bin/objcopy" -O binary -j .data     "$O/openpocket.elf" "$O/data.bin"
"$TC/common/bin/objcopy" -O binary -j .ram_text "$O/openpocket.elf" "$O/rt.bin" 2>/dev/null || : > "$O/rt.bin"
python3 - "$TC" "$O" "$HERE/openpocket.bin" <<'PY'
import re,subprocess,sys
tc,out,dest=sys.argv[1:]
syms=subprocess.run([f"{tc}/common/bin/objdump","-t",f"{out}/openpocket.elf"],capture_output=True,text=True,check=True).stdout
def sym(n):
    m=re.search(rf"^([0-9a-f]+) .*\s{n}$",syms,re.M); return int(m.group(1),16) if m else None
load=sym("_data_load"); rt_load=sym("_rt_load"); start=sym("_start")
assert start==0x02000120,hex(start or 0)
img=bytearray(open(f"{out}/text.bin","rb").read())
for at,part in sorted([(rt_load,open(f"{out}/rt.bin","rb").read()),(load,open(f"{out}/data.bin","rb").read())]):
    if part:
        assert at-0x02000120>=len(img),"sections se chevauchent"
        img+=b"\xff"*(at-0x02000120-len(img))+part
img+=b"\xff"*(-len(img)%4)
open(dest,"wb").write(img); print(f"OK -> openpocket.bin : {len(img)} octets")
PY

# ---- update loader (loader/, felucca-bsp's, Pocket X layout) -> build/loader/ota.bin ----
L="$O/loader"; mkdir -p "$L"
$CC -c "$HERE/../loader/crt0_ldr.S" -o "$L/crt0_ldr.o"
$CC -Os -ffunction-sections -fno-builtin -Wall -Wno-unused-function -I"$HERE/inc" -I"$BSP/chip" -I"$BSP/boards/fm1" \
    -c "$HERE/../loader/loader.c" -o "$L/loader.o"
"$TC/pi32v2/bin/ld" --gc-sections -e _start -T "$HERE/../loader/loader.ld" "$L/crt0_ldr.o" "$L/loader.o" -o "$L/loader.elf"
"$TC/common/bin/objcopy" -O binary -j .text "$L/loader.elf" "$L/loader.bin"
# the loader rewrites the flash: nothing of it may live in XIP (0x02xxxxxx)
"$TC/common/bin/objdump" -h "$L/loader.elf" | python3 -c '
import sys
for ln in sys.stdin:
    p = ln.split()
    if len(p) > 4 and p[1].startswith(".") and int(p[2], 16) and int(p[3], 16) >= 0x02000000:
        sys.exit("ERREUR : section " + p[1] + " du loader hors RAM")'

python3 "$HERE/../tools/mkloader.py" "$L/loader.bin" "$L/ota.bin"

# ---- update packages for the web installer -> docs/firmware/ (PocketX_9NN.fwsc, BlackBox_9NN.fwsc, manifest.json) ----
VER=$(sed -n 's/^#define OP_VERSION_NUM "\([0-9]*\)".*/\1/p' "$HERE/openpocket.c")
python3 "$HERE/../tools/pxpkg.py" "$HERE/openpocket.bin" "$L/ota.bin" "$HERE/../docs/firmware" --version "$VER" \
    --sdk-files "$HERE/sdk/packager"
