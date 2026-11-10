// SPDX-License-Identifier: GPL-3.0-only
// Read the complete factory sound bank from running BlackBox V21 over USB MIDI.
import { pack7, unpack7 } from "./pxota.js";
import { sha256hex, ufwEntries, updateImage } from "./pxpkg.js";

export const BLACKBOX_V21 = "BlackBox_021";
const AMP_CAB_SHA = "7e77ce724e53b8880415b5f3c40e60b5760a49ea110012931beeab6b93e7ec0d";
const PRESET_SHA = "a76c5a3b710b38ee0d3859977987ebb29d50dee3e10527ef73c09accc9194467";
// The on-flash file ends with a ten-byte trailer. V21's MIDI service exposes the records.
const LIVE_PRESET_SHA = "506e835c3315971a3914661cbbffa144a5221dedc06cea2562f35711d5d47444";
const BANK_START = 0xbf000;
const APP_SIZE = 0x9f000;

export async function factoryBank(models, presets) {
  if (models.length !== 0x3c000 || await sha256hex(models) !== AMP_CAB_SHA)
    throw new Error("Choose the original BlackBox AM4 amp/cab file from M-EFCS.");
  if (presets.length !== 7370 || await sha256hex(presets) !== PRESET_SHA)
    throw new Error("Choose the original BlackBox AM4 preset file from M-EFCS.");
  const bank = new Uint8Array(models.length + presets.length);
  bank.set(models);
  bank.set(presets, models.length);
  return bank;
}

export function factoryUpdate(official, bank, recoveryLoaderPackage) {
  if (official?.product !== BLACKBOX_V21 || !official.known || bank.length !== 0x3dcca)
    throw new Error("The original V21 firmware and verified AM4 bank are required.");
  const app = ufwEntries(official.image)?.find(e => e.type === 0 && e.crcOk && e.len === APP_SIZE);
  const source = recoveryLoaderPackage.image;
  const loader = ufwEntries(source)?.find(e => e.type === 100 && e.crcOk);
  if (!app || !loader) throw new Error("The V21 image or recovery loader is invalid.");
  const flash = new Uint8Array(BANK_START + bank.length).fill(0xff);
  flash.set(official.image.subarray(app.off, app.off + app.len));
  flash.set(bank, BANK_START);
  return updateImage(flash, source.subarray(loader.off, loader.off + loader.len));
}

const checksum = (bytes) => ~bytes.reduce((sum, b) => sum + b, 0) & 0xff;

async function read(link, object, address, length) {
  const body = Uint8Array.of(0, 0x59, 0x23, 8, 0, 0, object, 0, 0, 0, 0, 0, 0, 0, 0);
  const fields = new DataView(body.buffer);
  fields.setUint32(7, address, true);
  fields.setUint16(11, length, true);
  body[14] = checksum(body.subarray(6, 14));
  link.drain();
  if (!link.send([0xf0, ...pack7(body), 0xf7])) throw new Error("BlackBox MIDI disconnected");

  const end = Date.now() + 1800;
  while (Date.now() < end) {
    const packet = await link.read(end - Date.now());
    if (!packet) break;
    if (packet[0] !== 0xf0 || packet.at(-1) !== 0xf7) continue;
    const bytes = Uint8Array.from(unpack7(packet.slice(1, -1)));
    if (bytes[0] !== 0 || bytes[1] !== 0x59 || bytes.length < 8 ||
        checksum(bytes.subarray(6, -1)) !== bytes.at(-1)) continue;
    if (bytes.length === 8 && bytes[2] === 0 && bytes[6] !== 0)
      throw new Error("V21 refused access to its sound bank. Run the factory sound restore below.");
    if (bytes.length !== length + 15 || bytes[2] !== 0x23 || bytes[6] !== object) continue;
    const reply = new DataView(bytes.buffer);
    if (reply.getUint32(7, true) === address && reply.getUint16(11, true) === length)
      return bytes.subarray(14, -1);
  }
  throw new Error("No matching reply from the BlackBox sound bank");
}

async function readBank(link, object, start, length, chunkSize, onProgress) {
  const bank = new Uint8Array(length);
  for (let offset = 0; offset < length; offset += chunkSize) {
    bank.set(await read(link, object, start + offset, Math.min(chunkSize, length - offset)), offset);
    onProgress(offset / length);
  }
  return bank;
}

export async function verifyFactorySounds(updater, onProgress = () => {}) {
  const dev = await updater.find((id) => id.text === BLACKBOX_V21);
  if (!dev) throw new Error("Connect a BlackBox running official V21");
  try {
    const models = await readBank(dev.link, 5, 0x70000000, 0x3C000, 512, (p) => onProgress(p * 0.95));
    const presets = await readBank(dev.link, 4, 0, 7360, 92, (p) => onProgress(0.95 + p * 0.05));
    if (await sha256hex(models) !== AMP_CAB_SHA || await sha256hex(presets) !== LIVE_PRESET_SHA)
      throw new Error("The sound bank differs from the vendor’s AM4 factory bank");
    onProgress(1);
  } finally {
    dev.link.close();
  }
}
