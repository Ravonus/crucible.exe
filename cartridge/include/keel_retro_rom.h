#ifndef KEEL_RETRO_ROM_H
#define KEEL_RETRO_ROM_H
/* LR35902 ROM fast path. Only use immutable packets validated by kr_decode at
 * build time. Battery/downloaded data uses the fully validating C decoder.
 * Exactly 256 output bytes maximum; no allocation, cross-bank reads or deltas.
 * SDCC calling convention: src DE, output BC. The surrounding C is NONBANKED. */
static uint8_t kr_fast_mode;
static void kr_rom_decode(const uint8_t *src, uint8_t *out) NONBANKED __naked {
  src;
  out;
  // clang-format off
 __asm
  ld h,d
  ld l,e
  ld d,b
  ld e,c
  ld b,#0
  ld a,(hl+)
  ld (_kr_fast_mode),a
  or a
  jr z,00901$
  cp #3
  ret nc
00902$:
  ld a,(hl+)
  ld c,a
  ld a,(_kr_fast_mode)
  cp #1
  jr z,00903$
  ld a,c
  and #0xc0
  jr z,00904$
  cp #0x40
  jr z,00905$
  cp #0x80
  jr z,00906$
  ld a,c
  and #0x3f
  inc a
  ld c,a
  xor a
  jr 00908$
00903$:
  bit 7,c
  jr nz,00907$
  inc c
  jr 00909$
00904$:
  ld a,c
  and #0x3f
  inc a
  ld c,a
  jr 00909$
00905$:
  ld a,c
  and #0x3f
  add #3
  ld c,a
  ld a,(hl+)
  jr 00908$
00907$:
  res 7,c
  inc c
  ld a,(hl+)
  jr 00908$
00901$:
  ld c,#0
00909$:
  ld a,(hl+)
  ld (de),a
  inc de
  dec b
  ret z
  dec c
  jr nz,00909$
  jp 00902$
00908$:
  ld (de),a
  inc de
  dec b
  ret z
  dec c
  jr nz,00908$
  jp 00902$
00906$:
  ld a,c
  and #0x3f
  add #3
  ld c,a
  ld a,(hl+)
  push hl
  push bc
  ld c,a
  ld b,#0
  inc c
  jr nz,00910$
  inc b
00910$:
  ld h,d
  ld l,e
  ld a,l
  sub c
  ld l,a
  ld a,h
  sbc b
  ld h,a
  pop bc
00911$:
  ld a,(hl+)
  ld (de),a
  inc de
  dec b
  jr z,00912$
  dec c
  jr nz,00911$
  pop hl
  jp 00902$
00912$:
  pop hl
  ret
 __endasm;
  // clang-format on
}
#endif
