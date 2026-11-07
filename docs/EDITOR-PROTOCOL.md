# OpenPocket editor protocol (SysEx over USB-MIDI)

The web editor (`docs/editor.html`, `docs/pxedit.js`) talks to the pedal with this protocol. The firmware side is
`firmware/px_editor.h`; `tests/edsim.c` runs the same code on a PC for the tests. Protocol version 2 (OpenPocket v37; version 1: v32 to v36,
the same without the NAM commands 19..22).

## Framing

A request is `F0 7D 4F 50 <cmd> <args...> F7` (`7D` = the non-commercial SysEx ID, `4F 50` = "OP"). Every request
gets exactly one reply with the same header and the same `<cmd>`; a request the pedal does not understand gets no
reply. Every data byte is 7 bit. Send the next request only after the reply to the previous one (the pedal keeps
one incoming frame at a time). While the editor watches (`WATCH`), the pedal also sends `STATE` frames by itself.

| Item | Encoding |
| --- | --- |
| byte | one 7-bit byte |
| string | ASCII 32..126, ended by a 0 byte |
| color | an RGB565 color in 3 bytes: bits 0-6, 7-13, 14-15 |
| preset `P` | 110 bytes, the firmware's `px_preset_t` as is (every byte is below 128): `name` 14 (ASCII, 0-padded, at most 13 characters), `on` 12 (0/1), `type` 12, `p` 12 x 5 (0..100), `order` 12 (a permutation of 0..11) |

The 12 blocks, by index: 0 GATE, 1 COMP, 2 PITCH, 3 SFX, 4 DRIVE, 5 AMP, 6 CAB, 7 EQ, 8 MOD, 9 DELAY, 10 REVERB,
11 SYN (`CAT_BLOCK` gives their labels and names). `order` is the processing and display order.

## Commands

| cmd | Request args | Reply args |
| --- | --- | --- |
| 1 INFO | — | protocol (1), firmware string ("v32"), identity string ("PocketX_932"), blocks (12), parameters per block (5), slots (52), name length (13) |
| 2 CAT_BLOCK | block | block, label string ("AMP"), name string ("AMPLIFIER"), types, color |
| 3 CAT_TYPE | block, type | block, type, name string, parameters n, text mask (bit k: parameter k has its own value texts, see `TEXTS`), then n parameter-name strings |
| 4 TEXTS | block, type, k | block, type, k, then 101 strings: the text the pedal shows for values 0..100 ("" = the number itself) |
| 5 STATE | — | slot (0..51), flags (bit 0: the sound differs from the slot's, bit 1: the slot holds a saved preset), focused block, `P` (the current sound) |
| 6 SET | block, what, k, v | block, what, k, v (after clamping). what 0: parameter k = v (0..100); 1: on = v (0/1); 2: type = v |
| 7 ORDER | 12 bytes | rc (0 ok, 1 not a permutation) |
| 8 NAME | string | the name as stored (13 characters at most, other characters become "?") |
| 9 PUT | `P` | rc (0 ok, 1 invalid). Replaces the current sound (not saved) |
| 10 SELECT | slot | slot, rc. Loads the slot as the current sound (unsaved changes are dropped, as on the pedal) |
| 11 FOCUS | block | block. The pedal shows that block's page |
| 12 LIST | start, count (1..26) | start, count, then per slot: saved (0/1), name string |
| 13 SLOT_GET | slot | slot, saved, `P` (the factory preset when not saved) |
| 14 SLOT_PUT | slot, flags, [`P`] | slot, rc (0 ok, 1 invalid, 2 flash). flags bit 0: `P` follows and is stored as a saved preset (else the slot goes back to its factory preset); bit 1: write the flash now (else only in memory until `COMMIT`) |
| 15 COMMIT | — | rc (0 ok, 2 flash). Writes every slot to the flash (one write, about 0.3 s) |
| 16 SAVE | slot | slot, rc. Saves the current sound into the slot, writes the flash, the slot becomes the current one (as the pedal's SAVE) |
| 17 WATCH | on (0/1) | on. While on, the pedal pushes `STATE` (cmd 5) when the sound, the slot or the focused block changes on the pedal; it stops by itself 5 s after the last request |
| 18 PING | — | DSP load % (0..127), flags (bit 0: bypass, bit 1: second core) |
| 19 NAM_LIST | — | factory models (12), user slots (16), then per model (factory first, then the user slots): present (0/1), name string (11 characters at most; an empty user slot reads "U5 EMPTY") |
| 20 NAM_WRITE | piece (2 bytes: bits 0-6, 7-13), 128 packed bytes | the 2 piece bytes, rc (0 ok, 1 invalid). Piece i holds bytes 112 i .. 112 i + 111 of a model (the last piece fewer), packed 7 bit: groups of a byte of high bits (bit j = bit 7 of byte j) then 7 bytes. Stored in RAM only |
| 21 NAM_COMMIT | slot (0..15) | slot, rc (0 ok, 1 invalid: header, a weight that is not a number, CRC; 2 flash). Checks the pieces received and writes them to the user slot |
| 22 NAM_ERASE | slot (0..15) | slot, rc (0 ok, 1 invalid, 2 flash). Empties the user slot |
| 23 DIAG | — | n (5), then n strings: this boot, the last boot, the status line, the link, the trace. Read only. Only the Bluetooth test image answers: other firmware gives no reply. `docs/BLE-TEST.md`, "Reading the pedal from a computer" |

### NAM models (protocol 2)

The AMP block's ninth type, `NAM`, plays a neural amp model (NAM A2, its 3-channel "Lite" size, 1871 weights:
`engine/px_nam.h`). Its first parameter, MODEL, picks one of 28 models: 0..11 the factory ones (in the firmware),
12..27 the 16 user slots in the pedal's flash (`firmware/px_models.h`, 0xC0000..0xE0000, kept by firmware updates).
`TEXTS` of AMP type 8, parameter 0 gives each value's model name; the editor asks for it again after a change of
the user slots.

A user model is 7548 bytes, little endian: a 64-byte header, then the 1871 weights as float32 in the `.nam`
file's order.

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 4 | magic `0x4D4E504F` ("OPNM") |
| 4 | 2 | version, 1 |
| 6 | 2 | weights, 1871 |
| 8 | 12 | name, ASCII, 0-ended (11 characters at most) |
| 20 | 2 | level trim, int16, 0.01 dB (added to the model's output) |
| 22 | 2 | CRC16 of the weights (polynomial 0x1021, initial value 0) |
| 24 | 40 | 0 |

The editor (`docs/pxnam.js`) reads a `.nam` file, takes its A2-Lite part (an A2 file is a "SlimmableContainer"
with several sizes; a plain A2-Lite WaveNet also works), measures its loudness on `docs/nam_di.wav` and sets the
level trim so that every model plays at -20 LUFS on that clip, then sends 68 `NAM_WRITE` and one `NAM_COMMIT`.
A1 models (the older "standard" / "lite" / "feather" WaveNets) and other architectures are refused: the pedal's CPU
runs A2-Lite only.

Writing the flash (`SLOT_PUT` with bit 1, `COMMIT`, `SAVE`, `NAM_COMMIT`, `NAM_ERASE`) mutes the sound for a moment, as
saving on the pedal does. A whole import sends `SLOT_PUT` without
bit 1 for every slot, then one `COMMIT`.

## Preset files

The editor saves presets as JSON: `{"format": "openpocket-presets", "version": 1, "presets": [...]}`, each preset
`{"slot": 1..52, "name", "blocks": [{"block": "AMP", "on", "type": "type name", "params": [5 numbers]}...],
"order": ["GATE", ...]}`. Blocks and types are written by name, so a file stays readable and survives a firmware
that adds types; a single preset is the same object with one preset in `presets`.

Each preset written by OpenPocket v36 carries `"v": 36` (v35: `35`). Older presets are converted on import: without
`"v": 36`, the preset LEVEL (the GT block's 3rd setting, `PRESET LVL`, unused before) is set to 50 = 0 dB; without `"v": 35`,
its delay TIME is moved to v35's scale (the same delay time; REVERSE excepted); amp / drive / cab types from before
v34 (`"RECTIFIED"`...) become the nearest current type. Each change of the delay or the types is listed in the import
notes.
