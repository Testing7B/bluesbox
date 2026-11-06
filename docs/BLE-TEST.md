# Bluetooth test image (advertise only)

An optional build of OpenPocket that turns the pedal's Bluetooth radio on and sends **one advertisement**, so the name
**`OpenPocket-BLE-TEST`** shows up in a phone or Mac Bluetooth scanner. It is stage 2 of the Bluetooth plan: the radio comes up, nothing
more. No connection is ever accepted, there is no pairing, no GATT table and no MIDI over Bluetooth. USB MIDI is not
changed.

The normal firmware and the installer's packages in `docs/firmware/` are built exactly as before: `make build` gives the
same bytes with or without this feature in the tree. The test image is its own build target, `make ble`.

> **Status: built and linked, not yet run on a pedal.** No pedal was available to the author and there is no emulator of the
> radio, so everything below about what the pedal does is the plan, not an observation. Read
> [What is not verified](#what-is-not-verified) before you flash it.

## Build

```
make ble          # firmware/openpocket-ble.bin and build/ble/PocketX_936.fwsc, BlackBox_936.fwsc, manifest.json
```

(`node tools/op.mjs ble` is the same; `OP_BLE=1 sh firmware/build.sh` inside the container.) It prints the SHA-256 of the image and the two packages. Build outputs
are `firmware/build-ble/` and `build/ble/`; `make clean` removes them.

## Flash it

This is the pedal's normal update path, with the test packages instead of the release ones. Your pedal must already run
OpenPocket (the stock-firmware scan was skipped on purpose).

1. `make serve-ble` builds the test packages if they are missing and opens the offline installer on
   `http://localhost:8765/` with `build/ble/` as its package folder. (Chrome or Edge, USB cable to the pedal.)
2. **Connect**, pick the pedal, **Install**, as for any update. The pedal restarts into the test image; its start-up
   screen reads `v36 BLE TEST`.
3. To go back to the normal firmware: run `make serve` (the release packages) and **Install** again, or use the
   **Back to the official firmware** step. The SD-card recovery (`make appimage`) works as before.

The status text at the bottom right of the screen starts with the Bluetooth state: `STARTING`, then `ADVERTISING`
(or `FAILED`).

## What you should see

Open a scanner (nRF Connect, LightBlue, or macOS *Bluetooth Explorer* / *Audio MIDI Setup* are all fine; use active
scanning, which phones do by default):

| In the scanner | Value |
| --- | --- |
| Name | `OpenPocket-BLE-TEST` (in the scan response) |
| Service UUID | BLE-MIDI `03B80E5A-EDE8-4B33-A751-6CE34EC4C700` (in the advertising data) |
| Type | scannable, not connectable (`ADV_SCAN_IND`), 100 ms interval, channels 37, 38, 39 |
| Address | the controller's default address: this image does not set one, so it can differ from the stock firmware's |

The name appearing in a scanner is the whole success test. Connecting to it will fail by design.

If the screen reads `FAILED`, or nothing appears within a few seconds, please report the screen text, the pedal model,
what the scanner shows and whether the pedal still works as a normal OpenPocket (audio, screen, USB MIDI).

## How it works

```
openpocket.c (OP_BLE_TEST)  ble_test_start() after the screen is up, ble_test_poll() in the main loop
firmware/ble/ble_test.c     a task that starts the controller, sets the advertising data and enables advertising
firmware/ble/ble_os.c       the operating-system services the library calls (below)
firmware/ble/ble_rf.c       the Wi-Fi front-end half of the radio start-up (below)
firmware/ble/ble_cfg.c      controller configuration: LE only, advertising + one slave link
firmware/ble/ble_stubs.c    empty stand-ins for the parts that are not built
third_party/jieli-ble/      JieLi's controller, as published (Apache-2.0): see its README.md
```

* **The controller is JieLi's**, from the Apache-2.0 SDK libraries (`btctrler.a`, 33 of its objects plus 3 helpers from
  `cpu.a`): HCI front end, link layer, advertising, the LE radio driver. Only the objects the image reaches are linked.
  The host stack (`btstack.a`) is not used; `ble_test.c` calls the controller's own `ll_hci_adv_*` functions.
  The OpenPocket code around it is small and separate from the vendor objects (`firmware/ble/`), which are kept
  unmodified under `third_party/jieli-ble/` with the licence text and the source URL.
* **No RTOS.** The library expects tasks, queues, semaphores and timers. `ble_os.c` provides a cooperative scheduler:
  the controller's one task runs from `ble_poll()` in the main loop, switches only where it waits (queue, semaphore,
  delay), and is woken by the two BLE interrupts (IRQ 45 and 29, priority 2: the numbers and priority the stock firmware
  uses). The heap is a 24 KB static pool; each task has a 4 KB stack. Against the normal image it needs about 44 KB more RAM (of which the heap
  and stacks are 36 KB) and 131 KB more flash; the link fits with about 15 KB of the 96 KB RAM region free.
* **Radio start-up.** The library's `bt_rf_init` calls the Wi-Fi library's `wf_rf_common_init` first. Linking that would
  pull in the Wi-Fi MAC/SDIO stack, so `ble_rf.c` runs the already committed capture of the stock start-up instead
  (`firmware/bsp/chip/ble_rf_tables_v15.h`, run by `fm1_ble_rf.h`), with this unit's trims measured by `fm1_rf_cal.h`.
  The BLE baseband itself is initialised by the library (`le_hw_init`), which makes the same register changes as the stock
  firmware's baseband start-up in the analysis (bits 13 and 14 set, bit 4 cleared, IRQs 45 and 29 at priority 2).
* **Nothing is saved.** The library asks for stored items (`syscfg_read`): none are returned. It tries to store calibration
  results (`syscfg_write`): the call is accepted and ignored. The VM (`0xA5000`) and BTIF (`0xBE000`) areas are not read
  and not written by this image.

## What is not verified

All of it was built, linked and size-checked; none of it ran on hardware. The things most likely to go wrong, in the
order I would suspect them:

1. **The radio start-up.** It replays a capture taken from another firmware, with the stored crystal trim and the other
   saved radio records **not applied** (the VM is not read, so those fields stay at the capture's defaults). The carrier
   may therefore be off by some kHz. A scanner usually still sees it; a wrong value that stops the PLL would hang the
   start-up and the screen would stay on `STARTING`.
   The brief asked for the stock TX trims to be left as found. This image does **not** meet that exactly: it neither
   reads nor writes them, and the transmit level is whatever the library's power table gives with no saved trim.
   Reading them needs the VM item format, which is not documented in this repository; that would be the next task.
2. **Interrupt return.** The library's two interrupt functions end in `rti` without the `csync` the stock ones have
   (our compiler does not add it to bitcode; `fm1_isr.S` explains it for our own interrupts). The risk is an interrupt
   taken twice. Suspect this if the pedal resets or hangs a moment after `ADVERTISING` appears.
3. **The scheduler.** The context switch (`ble_ctx.S`) is eight instructions assembled from the compiler's own prologue
   and epilogue forms, and was not run. The queue, semaphore and timer code is plain C but also unrun.
4. **Flash writes while Bluetooth runs.** The library's interrupt code runs from flash (execute in place). Saving a
   preset or an update while advertising could, in principle, fetch code while the flash is busy. Do not save presets
   while testing.
5. **Real-time load.** Starting the radio blocks the main loop for the length of the start-up (screen and knobs freeze,
   audio keeps running from its interrupt). Afterwards the cost is the two radio interrupts every 100 ms.
6. **Regulation.** The pedal was certified with the stock firmware. This image uses the standard advertising channels,
   the 1M PHY and the library's own power table; it is a hobby experiment, not a product.

## Recovery

If the image does not start, the existing safety net applies: two failed boots go back to the update loader
(`UBOOT`), and the web installer, the SD-card recovery (`make appimage`) and the official firmware's own updater all work as
before. The image does not touch the flash outside the application area.
