# sdk/ — official JieLi drivers (Apache-2.0)

`iis.bc` (I2S/ALNK) and `dac.bc` (internal DAC): WL82 (AC791N) drivers, extracted as is from `cpu/wl82/liba/cpu.a`
(objects `iis.c.o` and `dac.c.o`) of the official JieLi SDK **fw-AC79_AIoT_SDK V1.2.0**
(https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK), licensed under Apache-2.0 (see LICENSE-JieLi-Apache-2.0).
This is LLVM bitcode (clang 4.0.1): `build.sh` compiles it for pi32v2 with our toolchain.
API: the SDK's `include_lib/driver/cpu/wl82/asm/iis.h` (reproduced in `../audio_probe.h`).

`packager/`: `uboot.boot` (the SDK's SPL), `cfg_tool.bin` and `eq_cfg_hw.bin`, as is from
`cpu/wl82/tools/` of the same SDK (Apache-2.0). `tools/pxpkg.py` puts them in the flash head of the `.fwsc` packages;
the OpenPocket update loader never writes this head (only the application area).
