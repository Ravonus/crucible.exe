import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { test } from "node:test";
import { finishHeader, globalChecksum, headerChecksum, ROM_TITLE } from "./rom-header.ts";

const release = new URL("../../release/crucible.exe.gbc", import.meta.url);

await test("the release ROM's header is already finished", () => {
  const rom = new Uint8Array(readFileSync(release));
  assert.deepEqual(finishHeader(rom), rom);
});

await test("title, cartridge type and both checksums", () => {
  const rom = finishHeader(new Uint8Array(0x8000));
  assert.equal(new TextDecoder().decode(rom.subarray(0x134, 0x134 + ROM_TITLE.length)), ROM_TITLE);
  assert.equal(rom[0x147], 0x1b);
  assert.equal(rom[0x149], 0x04);
  assert.equal(rom[0x14d], headerChecksum(rom));
  assert.equal(((rom[0x14e] ?? 0) << 8) | (rom[0x14f] ?? 0), globalChecksum(rom));
});

await test("titles longer than the header field are refused", () => {
  assert.throws(() => finishHeader(new Uint8Array(0x8000), "A".repeat(16)));
});
