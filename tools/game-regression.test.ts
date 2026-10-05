/** Execute the cartridge modules with sanitizers: independent run clocks and audible music beneath every cue. */
import { test } from "node:test";
import assert from "node:assert/strict";
import { mkdtempSync, mkdirSync, readFileSync, writeFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";
import { spawnSync } from "node:child_process";
const root = resolve(import.meta.dirname, "..");
function runModule(file: string, before: string, after: string): void {
  const dir = mkdtempSync(join(tmpdir(), "crucible-regression-"));
  try {
    mkdirSync(join(dir, "gb"));
    writeFileSync(join(dir, "gb/gb.h"), "#pragma once\n#include <stdint.h>\n#define BANKED\n");
    const source = readFileSync(join(root, "cartridge/src", file), "utf8")
      .replace(/^#include.*$/gm, "")
      .replace(/^#pragma.*$/gm, "");
    const cpp = join(dir, "test.cpp");
    writeFileSync(cpp, before + source + after);
    const binary = join(dir, "test");
    const compile = spawnSync(
      "clang++",
      [
        "-std=c++17",
        "-fsanitize=address,undefined",
        "-g",
        "-I" + dir,
        "-I" + join(root, "core/include"),
        "-I" + join(root, "cartridge/include"),
        "-I" + join(root, "cartridge/data/include"),
        cpp,
        "-o",
        binary,
      ],
      { encoding: "utf8" },
    );
    assert.equal(compile.status, 0, compile.stderr);
    const result = spawnSync(binary, [], { encoding: "utf8" });
    assert.equal(result.status, 0, result.stdout + result.stderr);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
}
await test("story clocks survive switching, restart, replacement, legacy migration and every torn write", () => {
  runModule(
    "crucible_time.c",
    String.raw`
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <setjmp.h>
#define BANKED
#include "crucible_core.h"
#include "crucible_time.h"
static uint16_t sys_time;
static uint8_t story_on, sram[131072];
static crucible_story saga;
static struct { uint8_t flags; } pl;
#define PF_SHEET 4u
#define SWITCH_RAM(n) ((void)(n))
#define DISABLE_RAM ((void)0)
static int fail_after = -1;
static jmp_buf cut;
static crucible_story *talk_saga(void) { return &saga; }
static void story_save(void) {}
static void player_unlock(uint8_t) {}
static void player_save(void) {}
void cru_story_act(crucible_story*,uint8_t) {}
void cru_story_lucid(crucible_story*,int8_t) {}
void cru_story_event(crucible_story*,uint8_t,uint16_t,uint16_t) {}
static uint8_t crucible_sram_read(void*, uint32_t at) { assert(at < sizeof sram); return sram[at]; }
static void crucible_sram_write(void*, uint32_t at, uint8_t v) {
 assert(at < sizeof sram);
 assert((at >= 0x1fe40 && at < 0x1fe80) || (at >= 0x1ff80 && at < 0x1ffc0) ||
        (at >= 0x3f80 && at < 0x3fc0) || (at >= 0x7f80 && at < 0x7fc0) || at == CT_HOST_AT);
 if (fail_after == 0) longjmp(cut,1);
 if (fail_after > 0) fail_after--;
 sram[at] = v;
}
`,
    String.raw`
int main() {
 uint8_t f[5] = {26,10,5,9,17};
 time_poll(); time_answer(CT_ASK_ADJUST,CT_ANSWER_OK,f); time_birthday_set(3,21);
 uint16_t classic = date_; uint8_t classic_records[64]; memcpy(classic_records,sram+CT_REC_A,64);
 for (uint8_t slot=0;slot<3;slot++) {
  time_new_game(slot); assert(time_ask_mode()==CT_ASK_FIRST && time_birthday_due());
  f[2]=10+slot; time_answer(CT_ASK_FIRST,CT_ANSWER_OK,f); time_birthday_set(4+slot,20);
  assert(time_ask_mode()==CT_ASK_DONE && date_==ct_days(26,10,10+slot));
 }
 story_on=1;
 for (uint8_t slot=0;slot<3;slot++) {
  time_story_opened(slot,1); assert(date_==ct_days(26,10,10+slot) && bmon_==4+slot);
 }
 time_free_play(); assert(date_==classic && bmon_==3);
 time_new_game(1); time_answer(CT_ASK_FIRST,CT_ANSWER_SKIP,f); time_birthday_set(0,0);
 time_free_play(); time_story_opened(1,1); assert(!(flags_&CT_KNOWN) && !bmon_ && time_ask_mode()==CT_ASK_DONE);
 time_story_opened(0,1); assert(date_==ct_days(26,10,10));
 time_new_game(0); assert(time_ask_mode()==CT_ASK_FIRST && !(flags_&CT_KNOWN));
 f[2]=22; time_answer(CT_ASK_FIRST,CT_ANSWER_OK,f);
 uint8_t snapshot[64]; memcpy(snapshot,sram+record_at,64); uint16_t old=date_;
 for (int n=0;n<=33;n++) {
  memcpy(sram+record_at,snapshot,64); loaded_=0; time_poll();
  fail_after=n; f[2]=23; if(!setjmp(cut)) time_answer(CT_ASK_ADJUST,CT_ANSWER_OK,f);
  fail_after=-1; loaded_=0; time_poll(); assert(date_==(n<33?old:ct_days(26,10,23)));
 }
 time_free_play(); assert(date_==classic && bmon_==3);
 /* A legacy run with no clock inherits the old clock, never another run's date. */
 memset(sram+RUN_CLOCK[2],0,64); time_story_opened(2,1); assert(date_==classic && bmon_==3);
 return 0;
}
`,
  );
});
await test("every voice and effect leaves melody and bass available, follows elapsed frames and colours future notes", () => {
  runModule(
    "crucible_sound.c",
    String.raw`
#include <stdint.h>
#include <assert.h>
#define BANKED
#include "crucible_core.h"
#include "crucible_time.h"
#include "keel_music.h"
#include "crucible_music_data.h"
#include "crucible_volume.h"
static uint16_t sys_time;
static crucible_core core;
static uint8_t story_on, link_on;
static crucible_story saga;
static crucible_story *talk_saga(void) { return &saga; }
void crucible_time_context(crucible_time_ctx *t) { t->flags=0; t->part=CT_DAY; }
#define SFX_SWAP 6u
struct Reg { uint8_t value=0; unsigned writes=0; Reg& operator=(unsigned v) { value=v; writes++; return *this; } };
static Reg NR10_REG,NR11_REG,NR12_REG,NR13_REG,NR14_REG,NR21_REG,NR22_REG,NR23_REG,NR24_REG;
static Reg NR30_REG,NR31_REG,NR32_REG,NR33_REG,NR34_REG,NR41_REG,NR42_REG,NR43_REG,NR44_REG;
static Reg NR50_REG,NR51_REG,NR52_REG,AUD3WAVE[16];
`,
    String.raw`
static void ticks(unsigned n, unsigned dt=1) { while(n--) { sys_time+=dt; sound_tick(); } }
int main() {
 core.rng=1234; core.options=0; sound_init(); music_mood(1); ticks(120);
 assert(m_state==M_PLAY && m_k==VOL_MUSIC_DEFAULT*G_FULL);
 uint16_t gameplay_rng=core.rng;
 for (uint8_t mood_=0;mood_<4;mood_++) {
  music_mood(mood_); assert(m_state!=M_OFF);
  for (uint8_t kind=0;kind<8;kind++) {
   unsigned lead=NR24_REG.writes,bass=NR34_REG.writes; uint16_t prior=m_seed;
   sound_voice(9000+kind,kind%7,kind,2);
   assert(!at[1] && !wave_left && m_k==VOL_MUSIC_DEFAULT*G_FULL && m_seed!=prior);
   ticks(160); assert(NR24_REG.writes>lead && NR34_REG.writes>bass && m_state!=M_OFF);
  }
 }
 for (uint8_t fx=0;fx<16;fx++) { sound_play(fx); ticks(20,4); assert(!at[1] && !wave_left && m_state!=M_OFF); }
 sound_voice(3,2,2,0); assert(reaction_octave==12 && reaction_left);
 const uint8_t e[2]={24,0}; play(1,e); assert(NR23_REG.value==(uint8_t)km_period[36]);
 ticks(200); assert(!reaction_left);
 sound_voice(3,2,0,0); ticks(1,4); assert(!at[0]);
 assert(core.rng==gameplay_rng);
 sound_options(vol_music_set(0,0)); assert(m_state==M_OFF); sound_play(1); assert(at[0]);
 sound_options(vol_music_set(4,4)); assert(sfx_off); uint16_t old=m_seed; sound_voice(8,1,2,0); assert(m_seed==old);
 return 0;
}
`,
  );
});
