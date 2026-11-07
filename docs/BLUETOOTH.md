# Bluetooth MIDI (v37)

From v37 the normal firmware can be a **Bluetooth MIDI device** named `OpenPocket`. A Mac, an iPhone or iPad, an Android
phone or a Bluetooth MIDI controller (a Chocolate Plus, for example) connects to it, and Program Change and Control Change
then do exactly what they do over USB ([MIDI.md](MIDI.md): presets, blocks on and off, models, settings, the clock).

It is **off by default**: a pedal that never uses it starts no radio and pays nothing.

## Switch it on

1. Hold **FX** for 0.8 s: the **MIDI** page opens.
2. Press **PUSH** until the **BLUETOOTH** row is selected, turn the **KNOB** to **ON**.
3. Press **HOME**. The setting is saved, and the radio starts as the page closes.

Open the MIDI page again to see what the radio is doing:

| The page says | Meaning |
| --- | --- |
| `BT: STARTING` | the radio is starting (a second or two) |
| `BT: VISIBLE AS "OPENPOCKET"` | ready: a device can connect |
| `BT: DEVICE CONNECTED` | a device is connected; the status corner of the PLAY page shows `BT` before the load (`BT 2C 78%`) |
| `BT: ON WHEN YOU LEAVE` | switched on here: it starts when you press HOME |
| `BT: OFF AFTER A RESTART` | switched off here: the radio stops at the next power-on (until then what it receives is ignored) |
| `BT: FAILED TO START` | the radio did not start this time (the pedal works as usual; the log channel says why, see below) |
| `BT: FAILED - SWITCH OFF AND ON` | the pedal restarted while the radio was starting; it stays off until you switch the pedal off and on |

The setting is kept with the MIDI settings (the preset store's settings byte, bit 7) and survives firmware updates.

## Connect

- **Mac:** *Audio MIDI Setup* → *Window* → *Show MIDI Studio* → the Bluetooth button → `OpenPocket` → *Connect*. It then
  appears as a MIDI device in every app.
- **iPhone / iPad:** in an app that has a Bluetooth MIDI page (GarageBand: *Settings* → *Advanced* → *Bluetooth MIDI
  Devices*; many MIDI apps have the same page). The system Bluetooth settings do not list MIDI devices.
- **Android:** an app such as *MIDI BLE Connect*, then any MIDI app.
- **Chocolate Plus:** switch it to **H**; it connects by itself a few seconds after the pedal starts. Its footswitches are set up
  in [tools/chocolate/README.md](../tools/chocolate/README.md). (To reach the pedal from a Mac instead, put the Chocolate on **O**.)

No PIN is asked. One device at a time: while one is connected the pedal is not visible to others.

## What it does not do (yet)

- The pedal only **receives** over Bluetooth: its own changes are sent over USB, not over Bluetooth.
- **Pairing is not remembered** across a power cycle. Devices that connect without pairing (the Chocolate, a Mac) do not
  notice; one that insists on its bond may need to forget `OpenPocket` and connect again.
- Every OpenPocket pedal has the same Bluetooth address: two of them in one room get in each other's way.
- Switching it off takes effect at the next power-on.

## Please report

The radio works on a BlackBox with a Mac and a Chocolate Plus (the test image, [BLE-TEST.md](BLE-TEST.md)). What is not
measured yet is the sound **while a device is connected**: please send the `BT 2C nn%` reading on a few presets with a device
connected and without, and say if you hear any crackle. If it does not start, a photo of the MIDI page and the output of
`node tools/op.mjs log` (USB cable, the pedal running) say where it stopped.

## How it works

- JieLi's own Bluetooth LE controller and host (Apache-2.0, LLVM bitcode from the AC79 SDK, `third_party/jieli-ble/`), with a
  small cooperative scheduler for its tasks (`firmware/ble/ble_os.c`) run from the main loop, and the radio start-up replayed
  from the stock firmware's (`firmware/ble/ble_rf.c`). The GATT table and the MIDI packet reader are ours
  (`firmware/ble/ble_midi.c`, `firmware/px_ble_midi.h`); what it receives goes through the same `px_midi_in` as USB.
- Its interrupts run at priority 2, below the audio: the audio never waits for the radio. The radio's work runs in the main
  loop, not on the DSP core.
- **Memory.** The controller needs about 44 KB of RAM; with the NAM network there is not that much left in the 96 KB RAM. Its
  three task stacks (14.7 KB) and the tuner's buffers (9 KB) live in the data cache used as RAM (`firmware/px_cacheram.h`,
  the `CRAM` region of `firmware/app.ld`), which only the CPU uses; its heap (24 KB, which holds the radio's own buffers) stays
  in RAM. The build prints both figures.
- **The stack guard** stays on: before the controller's tasks first run, the firmware raises the top of the guard's window to
  the end of their stacks (`ble_guard_window` in `firmware/openpocket.c`).
- **Start-up safety.** A reset while the radio is starting (a hang the watchdog ends, a crash) is noted in memory that survives
  a reset: the following boots leave the radio off until a power cycle, so a radio that will not start on some unit costs one
  restart, never a loop. After any failed boot (the `LAST BOOT FAILED` screen) the next boot runs without it too.
- The log channel (`node tools/op.mjs log`, editor command 23 `DIAG`) answers in every firmware with Bluetooth built in: the
  boot, the last boot, the radio's state, the link and the last events, as described in [BLE-TEST.md](BLE-TEST.md).
- `OP_NOBLE=1 sh firmware/build.sh` builds the firmware without it. `make ble` builds the test image: the same firmware with the
  radio always on and its state scrolling in the status area.
