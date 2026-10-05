/** Execute cartridge behavior with sanitizers: run clocks, layered music, arrivals and seeded avatar choices. */
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
        "-fno-sanitize-recover=all",
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

await test("random bench encounters arrive without mixes or unlocks, remain optional and respect mode boundaries", () => {
  runModule(
    "crucible_flow.c",
    String.raw`
#include <stdint.h>
#include <string.h>
#include <assert.h>
#define BANKED
#include "crucible_core.h"
#include "crucible_time.h"
#include "crucible_flow.h"
#define FLOW_TEST
#define J_UP 4u
#define J_DOWN 8u
#define GLYPH(c) ((uint8_t)(c))
#define UI_A 1u
#define UI_B 2u
#define SFX_SWAP 6u
#define SFX_DENY 2u
#define SFX_MOVE 1u
#define PF_FIRST_DUEL 1u
#define FIGHT_DUEL 8u
#define LCD_EVENTS 4u
#define LCD_OBJ_ON 2u
#define LCDCF_OBJON 2u
#define STORY_NONE 0u
#define STORY_VISIT 3u
#define STORY_BOSS 4u
#define STORY_LOSS 1u
#define STORY_OVER 2u
#define LINK_COOP 0u
#define LINK_FIGHT 2u
static crucible_core core;
static crucible_story saga;
static struct { uint8_t flags; } pl;
static uint16_t sys_time;
static uint8_t story_on,story_slot_at,link_on,link_started,link_mode,flow_gauntlet,fight_dbg;
static uint8_t VBK_REG,SCY_REG,SCX_REG,LCDC_REG,DIV_REG,lcd_kind[LCD_EVENTS],win_pos_y;
static unsigned saves,ignored_acts;
static crucible_story *talk_saga(void) { return &saga; }
static void set_bkg_tiles(uint8_t,uint8_t,uint8_t,uint8_t,const uint8_t*) {}
static void set_sprite_data(uint8_t,uint8_t,const uint8_t*) {}
static void move_sprite(uint8_t,uint8_t,uint8_t) {}
static void set_sprite_tile(uint8_t,uint8_t) {}
static void set_sprite_prop(uint8_t,uint8_t) {}
static void vsync(void) { sys_time++; }
static void sound_tick(void) {}
static void sound_play(uint8_t) {}
static uint8_t input_take(void) { return 0; }
static uint8_t room_door(void) { return 0; }
static uint8_t player_xp(uint16_t) { return 0; }
static uint16_t player_owned_next(uint16_t) { return 0; }
static uint8_t fr_stance(uint16_t) { return 1; } // no DREAM discovery
static uint8_t fight_rival(void) { return 255; } // no hostile or wary faction
static void story_save(void) { saves++; }
void cru_story_act(crucible_story*,uint8_t) { ignored_acts++; }
void crucible_time_context(crucible_time_ctx *c) { memset(c,0,sizeof *c); }
static uint8_t link_result(void) { return 0; }
static uint8_t link_scene_pull(void) { return 0; }
uint8_t time_angel_take(void) { return 0; }
void time_mark_act(void) {}
static void talk_angel(uint16_t) {}
void time_angel_arm(void) {}
static void fight_debug(void) {}
static void crucible_get_name(uint16_t,char *out) { strcpy(out,"THING"); }
uint8_t cru_filter_apply(crucible_core*,uint8_t) { return 1; }
void cru_filter_close(crucible_core*) {}
`,
    String.raw`
static void reset_test(uint8_t mode) {
 memset(&core,0,sizeof core); memset(&saga,0,sizeof saga); pl.flags=0;
 core.items=4; core.found[0]=4; core.slot_a=CRU_NONE; core.mix.result=CRU_NONE; core.rng=1234;
 story_on=mode; story_slot_at=0; link_on=link_started=0; link_mode=0;
 flow_hint=flow_force=flow_pending=0; flow_since=4;flow_fgap=6;flow_tgap=4;flow_count=0;
 inited=0; last=0; sys_time=0; seed=0x51f7; beat=chap_seen=255; owe=0; saves=ignored_acts=0;
}
static uint8_t frame(uint8_t screen=0,uint8_t p=0) { sys_time++; return flow_tick(screen,&p); }
int main() {
 for(uint16_t run=1;run<=30;run++) {
  reset_test(1); core.rng=(uint16_t)(run*127u);
  unsigned first=0,boss=0,talks=0;
  for(unsigned f=0;f<12000;f++) {
   frame();
   if(flow_hint) {
    if(!first)first=f+1;
    assert(!flow_force && !flow_count);
    if(flow_hint==FLOW_FIGHT) { boss=f+1; assert(flow_arg<CRU_FACTIONS); break; }
    talks++; assert(talks<=2);
    for(unsigned k=0;k<1205 && flow_hint;k++)frame();
   }
  }
  assert(first>=1080 && first<=2700 && boss); // starter-only idle play gets actual bosses
  assert(flow_since==4 && flow_fgap==6 && !pl.flags); // no recipe or tutorial unlock was faked
  assert(saves==0 && ignored_acts==0); // ignoring a random arrival costs nothing
  assert(frame(0,J_DOWN)==FLOW_FIGHT && flow_count==1 && !flow_hint); // same existing approach
  for(unsigned k=0;k<300;k++)frame();
  assert(!flow_hint && !flow_force); // breath by time, even without more mixes
 }
 reset_test(0);
 for(unsigned f=0;f<12000;f++)frame();
 assert(!flow_hint && !flow_force && !flow_count); // Free Play keeps its existing visitor rules
 reset_test(1);
 for(unsigned f=0;f<6000;f++)frame(5); // menus do not consume encounter time
 assert(!flow_hint);
 for(unsigned f=0;f<1000;f++)frame();
 assert(!flow_hint);
 for(unsigned f=0;f<1800&&!flow_hint;f++)frame();
 assert(flow_hint);
 reset_test(1); link_on=1; link_started=1;link_mode=1;
 for(unsigned f=0;f<12000;f++)frame();
 assert(!flow_hint && !flow_force && !flow_count); // no race/fight interruption
 link_mode=LINK_COOP;
 for(unsigned f=0;f<12000;f++)frame();
 assert(!flow_hint && !flow_force); // random local arrivals do not desync co-op
 reset_test(1); flow_arg=1;flow_force=FLOW_FIGHT;dirty=1;
 assert(!frame());
 for(unsigned f=0;f<130 && !flow_count;f++)frame();
 assert(flow_count==1); // authored progression still forces its announced encounter
 return 0;
}
`,
  );
});

await test("Boy and Girl are always available and rerolls, undo and confirmation keep the seeded genome", () => {
  const player = readFileSync(join(root, "cartridge/src/crucible_player.c"), "utf8");
  const counts = player.slice(
    player.indexOf("static const uint8_t AV_COUNT"),
    player.indexOf("/* never in the level order"),
  );
  const roll = player.slice(player.indexOf("static uint8_t pick_open"), player.indexOf("void player_defaults"));
  runModule(
    "crucible_avatar_edit.c",
    String.raw`
#include <stdint.h>
#include <string.h>
#include <assert.h>
#define BANKED
#include "crucible_avatar.h"
#define AV_ROWS 9u
#define AV_OPTIONS 81u
#define PF_DMG 32u
#define J_A 1u
#define J_B 2u
#define J_UP 4u
#define J_DOWN 8u
#define J_LEFT 16u
#define J_RIGHT 32u
#define J_START 64u
#define J_SELECT 128u
#define GLYPH(c) ((uint8_t)(c))
#define UI_LEFT '<'
#define UI_RIGHT '>'
#define UI_A 'A'
#define UI_B 'B'
#define SFX_OPEN 1u
#define SFX_CLOSE 2u
#define SFX_UNDO 3u
#define SFX_MOVE 4u
#define SFX_DENY 5u
#define SFX_SWAP 6u
static uint8_t fr_scratch[32], menu_slot, VBK_REG, locked;
static uint8_t tiles[32][32], face[6];
static struct { uint8_t genome[6],flags; } pl;
static void set_bkg_tiles(uint8_t x,uint8_t y,uint8_t w,uint8_t h,const uint8_t *p) {
 assert(x+w<=32 && y+h<=32);
 if(!VBK_REG)for(uint8_t j=0;j<h;j++)memcpy(tiles[y+j]+x,p+j*w,w);
}
void avatar_make_genome(const uint8_t *g) { memcpy(face,g,6); }
void avatar_glitch(uint8_t) {}
static void sound_play(uint8_t) {}
static uint8_t player_unlocked(uint8_t o);
` +
      counts +
      String.raw`
static uint8_t player_unlocked(uint8_t o) {
 if(locked)return 0;
 for(unsigned i=0;i<sizeof START;i++)if(START[i]==o)return 1;
 return 0;
}
` +
      roll +
      String.raw`
static void player_story_load(uint8_t,uint16_t seed) {
 player_genome_roll(pl.genome,seed); pl.genome[4]|=0x28; pl.genome[5]=0x83;
}
`,
    String.raw`
int main() {
 for(uint16_t seed=1;seed<=128;seed++) {
  uint8_t original[6],boy[6],girl[6],rerolled[6];
  locked=0; av_edit_open(seed); memcpy(original,av_genome,6);
  assert(row_==LOOK_ROW && !get_(LOOK_ROW) && !av_genome_set);
  assert(!memcmp(tiles[1]+2,"LOOK",4) && !memcmp(tiles[1]+10,"DREAM",5));
  assert(!memcmp(tiles[8]+2,"STYLE",5)); // existing cosmetic rows stay in place
  locked=1; // no cosmetic option unlocked: the choice is still reachable
  av_edit_tick(J_RIGHT); assert(get_(LOOK_ROW)==AV_LOOK_BOY && (av_genome[0]&15)==1);
  assert(!memcmp(tiles[1]+10,"BOY",3)); memcpy(boy,av_genome,6);
  av_edit_tick(J_RIGHT); assert(get_(LOOK_ROW)==AV_LOOK_GIRL && (av_genome[0]&15)==1);
  assert(!memcmp(tiles[1]+10,"GIRL",4)); memcpy(girl,av_genome,6);
  av_edit_tick(J_B); assert(!memcmp(boy,av_genome,6)); // includes the look bits
  av_edit_tick(J_RIGHT); locked=0;
  av_edit_tick(J_SELECT); assert(get_(LOOK_ROW)==AV_LOOK_GIRL && (av_genome[0]&15)==1);
  assert(av_genome[5]==girl[5] && (av_genome[4]&0xf8)==(girl[4]&0xf8));
  assert(!memcmp(face,av_genome,6)); memcpy(rerolled,av_genome,6);
  av_edit_tick(J_B); assert(!memcmp(girl,av_genome,6));
  av_edit_tick(J_DOWN); assert(row_==1); // the human style row is skipped
  av_edit_tick(J_UP); assert(row_==LOOK_ROW);
  av_edit_tick(J_RIGHT); assert(get_(LOOK_ROW)==AV_LOOK_DREAM);
  av_edit_tick(J_DOWN); assert(row_==0); // DREAM permits the other seeded forms
  av_edit_open(seed); av_edit_tick(J_RIGHT); av_edit_tick(J_RIGHT); av_edit_tick(J_SELECT);
  assert(!memcmp(rerolled,av_genome,6)); // identical seed + input history reproduces the face
  assert(av_edit_tick(J_A)==1 && av_genome_set);
  av_edit_open(seed); assert(!memcmp(original,av_genome,6));
  assert(av_edit_tick(J_B)==2 && !av_genome_set); // back without modifying the saved player
 }
 return 0;
}
`,
  );
});

await test("seeded human looks render deterministically with distinct silhouettes using the cartridge renderer", () => {
  const dir = mkdtempSync(join(tmpdir(), "crucible-avatar-"));
  try {
    const runner = join(dir, "test.c");
    writeFileSync(
      runner,
      String.raw`
#include <stdint.h>
#include <string.h>
#include <assert.h>
#define BANKED
#include "crucible_avatar.h"
extern uint8_t work_[AVATAR_BYTES],style_,hair_;
int main(void) {
 for(unsigned seed=1;seed<=256;seed++) {
  uint8_t g[6]={(uint8_t)((seed%9)|((seed%16)<<4)),(uint8_t)(seed%128),
                (uint8_t)(seed*13),(uint8_t)(seed*7),(uint8_t)(seed%8),0};
  uint8_t boy[AVATAR_BYTES],girl[AVATAR_BYTES]; uint16_t pal[4],again[4];
  avatar_make_genome(g); // every legacy DREAM style also stays within the renderer bounds
  g[5]=AV_LOOK_BOY<<3; avatar_make_genome(g);
  assert(style_==1 && (hair_==1 || hair_==4));
  memcpy(boy,work_,sizeof boy); avatar_palette(pal);
  avatar_make_genome(g); avatar_palette(again);
  assert(!memcmp(boy,work_,sizeof boy) && !memcmp(pal,again,sizeof pal));
  g[5]=AV_LOOK_GIRL<<3; avatar_make_genome(g);
  assert(style_==1 && (hair_==2 || hair_==3));
  memcpy(girl,work_,sizeof girl); avatar_palette(again);
  assert(memcmp(boy,girl,sizeof boy) && !memcmp(pal,again,sizeof pal));
  avatar_make_genome(g); assert(!memcmp(girl,work_,sizeof girl));
 }
 return 0;
}
`,
    );
    const binary = join(dir, "test");
    const compile = spawnSync(
      "clang",
      [
        "-std=c11",
        "-DAVATAR_HOST",
        "-fsanitize=address,undefined",
        "-fno-sanitize-recover=all",
        "-g",
        "-I" + join(root, "cartridge/include"),
        runner,
        join(root, "cartridge/src/crucible_avatar.c"),
        join(root, "cartridge/src/crucible_avatar_base.c"),
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
});
