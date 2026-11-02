# sdk/ — pilotes officiels JieLi (Apache-2.0)

`iis.bc` (I2S/ALNK) et `dac.bc` (DAC interne) : pilotes de la WL82 (AC791N), extrait tel quel de `cpu/wl82/liba/cpu.a`
(objets `iis.c.o` et `dac.c.o`) du SDK officiel JieLi **fw-AC79_AIoT_SDK V1.2.0**
(https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK), sous licence Apache-2.0 (voir LICENSE-JieLi-Apache-2.0).
C'est du bitcode LLVM (clang 4.0.1) : `build.sh` le compile pour pi32v2 avec notre toolchain.
API : `include_lib/driver/cpu/wl82/asm/iis.h` du SDK (reprise dans `../audio_probe.h`).

`packager/` : `uboot.boot` (le SPL du SDK), `cfg_tool.bin` et `eq_cfg_hw.bin`, tels quels depuis
`cpu/wl82/tools/` du même SDK (Apache-2.0). `tools/pxpkg.py` les met dans la tête de flash des paquets `.fwsc` ;
le chargeur de mise à jour d'OpenPocket n'écrit jamais cette tête (seulement la zone de l'application).
