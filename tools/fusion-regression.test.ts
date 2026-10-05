/** The faster interpolation must retain every original tile byte and sprite position. */
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync, writeFileSync, mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";
import { spawnSync } from "node:child_process";
await test("fusion lookup optimization preserves all pixels across every pose and seeded input", () => {
  const root = resolve(import.meta.dirname, "..");
  const dir = mkdtempSync(join(tmpdir(), "crucible-fusion-"));
  try {
    const clean = (path: string): string =>
      readFileSync(join(root, path), "utf8")
        .replace(/^#include.*$/gm, "")
        .replace(/^#pragma.*$/gm, "");
    const before = clean("test/fixtures/fusion-before.c"),
      after = clean("cartridge/src/crucible_fusion.c");
    const cpp =
      String.raw`
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <vector>
#define BANKED
#define ENABLE_RAM ((void)0)
#define SWITCH_RAM(n) ((void)(n))
#define DISABLE_RAM ((void)0)
#define FUSION_STEPS 16u
#define CRUCIBLE_HD_FUSION(a,b,c,d) ((void)0)
static uint8_t scratch[8192], sprites[768], oam[160];
static uint16_t crucible_shade_mask;
#define SRAM_PTR(a) (scratch+((a)-0xa000u))
static void set_sprite_data(uint8_t at,uint8_t count,uint8_t *data) { memcpy(sprites+at*16,data,count*16); }
static void set_sprite_tile(uint8_t at,uint8_t v) { oam[at*4+2]=v; }
static void set_sprite_prop(uint8_t at,uint8_t v) { oam[at*4+3]=v; }
static void move_sprite(uint8_t at,uint8_t x,uint8_t y) { oam[at*4]=y; oam[at*4+1]=x; }
namespace before {
` +
      before +
      "\n}\nnamespace after {\n" +
      after +
      String.raw`
}
static void capture(bool old,uint8_t tiles[3][256]) {
 memset(scratch,0,sizeof scratch); memset(sprites,0,sizeof sprites); memset(oam,0,sizeof oam);
 for(unsigned i=0;i<3;i++) { crucible_shade_mask=0x5555u;
  if(old) before::crucible_capture_tiles(tiles[i],i*16); else after::crucible_capture_tiles(tiles[i],i*16);
 }
}
int main() {
 uint32_t seed=0x95673311;
 for(unsigned trial=0;trial<64;trial++) {
  uint8_t tiles[3][256];
  for(auto &slot:tiles) for(auto &v:slot) { seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;v=trial<2?trial?0xff:0:(uint8_t)seed; }
  std::vector<uint8_t> expected;
  capture(true,tiles);
  for(uint8_t pose=0;pose<17;pose++) { before::fusion_frame(pose,80,48,16-pose);expected.insert(expected.end(),sprites,sprites+512);expected.insert(expected.end(),oam,oam+128); }
  capture(false,tiles);
  for(uint8_t pose=0;pose<17;pose++) { after::fusion_frame(pose,80,48,16-pose);
   assert(!memcmp(expected.data()+pose*640,sprites,512));assert(!memcmp(expected.data()+pose*640+512,oam,128));
  }
 }
 return 0;
}
`;
    writeFileSync(join(dir, "test.cpp"), cpp);
    const compile = spawnSync(
      "clang++",
      ["-std=c++17", "-fsanitize=address,undefined", join(dir, "test.cpp"), "-o", join(dir, "test")],
      { encoding: "utf8" },
    );
    assert.equal(compile.status, 0, compile.stderr);
    const run = spawnSync(join(dir, "test"), [], { encoding: "utf8" });
    assert.equal(run.status, 0, run.stdout + run.stderr);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});
