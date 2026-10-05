/** Cartridge header fields the linker cannot set the way this cartridge needs them. */

const TITLE_START = 0x134;
const TITLE_LENGTH = 15; // the old-style title; 0x143 is the CGB flag and stays as linked
const CARTRIDGE_TYPE = 0x147;
const RAM_SIZE = 0x149;
const HEADER_CHECKSUM = 0x14d;
const GLOBAL_CHECKSUM = 0x14e;

const MBC5_RAM_BATTERY = 0x1b;
const RAM_128K = 0x04; // sixteen 8 KB SRAM banks: the save records and the story slots

export const ROM_TITLE = "CRUCIBLE.EXE";

/** Write the title (the link step leaves it empty), declare MBC5 + 128 KB battery RAM, then recompute the header
 * checksum (the boot ROM refuses a cartridge whose header checksum is wrong) and the global checksum. */
export function finishHeader(rom: Uint8Array, title: string = ROM_TITLE): Uint8Array {
  if (title.length > TITLE_LENGTH || !/^[ -~]*$/.test(title)) {
    throw new Error(`ROM title must be at most ${TITLE_LENGTH} printable ASCII characters`);
  }
  if (rom.length < 0x8000) throw new Error("ROM is shorter than two banks");
  const out = Uint8Array.from(rom);
  for (let i = 0; i < TITLE_LENGTH; i++) out[TITLE_START + i] = i < title.length ? title.charCodeAt(i) : 0;
  out[CARTRIDGE_TYPE] = MBC5_RAM_BATTERY;
  out[RAM_SIZE] = RAM_128K;
  out[HEADER_CHECKSUM] = headerChecksum(out);
  const global = globalChecksum(out);
  out[GLOBAL_CHECKSUM] = global >> 8;
  out[GLOBAL_CHECKSUM + 1] = global & 0xff;
  return out;
}

export function headerChecksum(rom: Uint8Array): number {
  let sum = 0;
  for (let i = TITLE_START; i < HEADER_CHECKSUM; i++) sum = (sum - byteAt(rom, i) - 1) & 0xff;
  return sum;
}

/** Sum of every byte except the two checksum bytes themselves, modulo 2^16. */
export function globalChecksum(rom: Uint8Array): number {
  let sum = 0;
  for (let i = 0; i < rom.length; i++) {
    if (i !== GLOBAL_CHECKSUM && i !== GLOBAL_CHECKSUM + 1) sum = (sum + byteAt(rom, i)) & 0xffff;
  }
  return sum;
}

function byteAt(rom: Uint8Array, i: number): number {
  const b = rom[i];
  if (b === undefined) throw new RangeError(`ROM offset ${i} out of range`);
  return b;
}
