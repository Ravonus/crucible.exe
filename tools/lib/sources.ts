import { mkdirSync, readdirSync, readFileSync, writeFileSync } from "node:fs";
import { basename, join } from "node:path";

/** Every path here is relative to the repository root. SDCC aborts on any argument that contains ".exe"
 * (an internal assertion), and a clone of this repository is usually a directory named crucible.exe. */
export const INCLUDE_DIRS = ["cartridge/include", "cartridge/data/include", "core/include", "core/src"] as const;

/** core/src is portable C99 shared with host builds. On the cartridge every core function must be BANKED and every
 * call between core files a banked call: a file compiled without CORE_BANKED=BANKED emits plain `call _cru_*` into
 * the wrong bank and the cartridge panics at boot. */
const CORE_PRELUDE = "#pragma bank 255\n#include <gb/gb.h>\n#define CORE_BANKED BANKED\n#define CORE_LOCAL BANKED\n";

/** cru_tables.c reads the catalogue through CRU_T8/CRU_T16. On the cartridge the tables are the fixed catalogue
 * banks (cartridge/data/catalogue), so the accessors become crucible_cat_* reads and the context goes unused. */
const CORE_TABLES_HOOK = [
  '#include "crucible_codec.h"',
  '#include "crucible_cat.h"',
  "static const uint32_t cat_offsets[]=CRUCIBLE_CAT_OFFSETS;",
  "#define CRU_T8(c,tab,i) crucible_cat_u8(cat_offsets[tab]+(uint16_t)(i))",
  "#define CRU_T16(c,tab,i) crucible_cat_u16(cat_offsets[tab]+((uint32_t)(i)<<1))",
  "",
].join("\n");
const CORE_TABLES_TAIL = "\nconst crucible_tables cru_world_tables=CRUCIBLE_CAT_TABLES(&cru_rules_world);\n";

export interface Source {
  /** Object name; unique across the build. */
  readonly name: string;
  /** Path handed to the compiler. */
  readonly path: string;
}

/** The translation units in link order. The linker's autobanking places code in this order, so it is part of the
 * ROM's identity: sorted by object name (code-unit order), with main last. */
export function collectSources(stageDir: string): Source[] {
  const units: Source[] = [];
  for (const dir of ["cartridge/src", ...dataDirs()]) {
    for (const f of readdirSync(dir)) {
      if (f.endsWith(".c") && !f.startsWith(".") && f !== "main.c")
        units.push({ name: basename(f, ".c"), path: join(dir, f) });
    }
  }
  units.push(...stageCore(stageDir));
  units.sort((a, b) => (a.name < b.name ? -1 : a.name > b.name ? 1 : 0));
  const seen = new Set<string>();
  for (const u of units) {
    if (seen.has(u.name)) throw new Error(`two translation units named ${u.name}`);
    seen.add(u.name);
  }
  units.push({ name: "main", path: "cartridge/src/main.c" });
  return units;
}

function dataDirs(): string[] {
  return readdirSync("cartridge/data", { withFileTypes: true })
    .filter((e) => e.isDirectory() && e.name !== "include")
    .map((e) => join("cartridge/data", e.name));
}

/** The core units the cartridge links. core/src also holds units only other hosts use (cru_encounter.c). */
const CARTRIDGE_CORE_UNITS = [
  "cru_bench",
  "cru_feats",
  "cru_grant",
  "cru_lost",
  "cru_play",
  "cru_records",
  "cru_reset",
  "cru_saga",
  "cru_save",
  "cru_story",
  "cru_text",
];

/** Write the banked cartridge copies of core/src into stageDir. */
function stageCore(stageDir: string): Source[] {
  mkdirSync(stageDir, { recursive: true });
  const staged: Source[] = [];
  for (const f of [...CARTRIDGE_CORE_UNITS.map((u) => `${u}.c`), "cru_tables.c"]) {
    const text = readFileSync(join("core/src", f), "utf8");
    const name = f === "cru_tables.c" ? "crucible_core_tables" : basename(f, ".c");
    const body = f === "cru_tables.c" ? CORE_PRELUDE + CORE_TABLES_HOOK + text + CORE_TABLES_TAIL : CORE_PRELUDE + text;
    const path = join(stageDir, `${name}.c`);
    writeFileSync(path, body);
    staged.push({ name, path });
  }
  return staged;
}
