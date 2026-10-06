/** Build the cartridge: `node tools/build-rom.ts [--verify] [--jobs N]`.
 *
 * Compiles every translation unit with GBDK's lcc, links an 8 MB MBC5 ROM, finishes its header and writes
 * build/crucible.gbc with its .map and .noi beside it, plus build/obj (objects and listings, which the harnesses
 * read). --verify fails unless the ROM is byte-identical to release/crucible.exe.gbc (release/SHA256SUMS). */
import { spawn, spawnSync } from "node:child_process";
import { createHash } from "node:crypto";
import { mkdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { availableParallelism } from "node:os";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { parseArgs } from "node:util";
import { finishHeader } from "./lib/rom-header.ts";
import { collectSources, INCLUDE_DIRS, type Source } from "./lib/sources.ts";
import { findToolchain } from "./lib/toolchain.ts";

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const BUILD = "build";
const OBJ = join(BUILD, "obj");
const ROM = join(BUILD, "crucible.gbc");

/** SDCC's optimizer notes (110: "conditional flow changed", 126: "unreachable code") fire on constant conditions
 * that macros produce by design; every other warning is an error. */
const COMPILE_FLAGS = [
  "-c",
  "-Wa-l",
  "-Wf--Werror",
  "-Wf--disable-warning",
  "-Wf110",
  "-Wf--disable-warning",
  "-Wf126",
  // The core's boss answers from the shelf (the cartridge's hand fight, docs/fight-system.md section 12).
  "-DCRU_HAND_FIGHT",
  ...INCLUDE_DIRS.map((d) => `-I${d}`),
];

/** Per-unit waivers. The catalogue accessors take the tables context for host builds; in fixed ROM banks it is
 * unused (85). The other is a real finding whose fix changes the generated code, so it waits for a release that is
 * allowed to move the ROM's bytes: a ternary that mixes char* and a string literal (196). */
const WAIVERS: Readonly<Record<string, readonly string[]>> = {
  crucible_core_tables: ["-Wf--disable-warning", "-Wf85"],
  crucible_menu: ["-Wf--disable-warning", "-Wf196"],
};

/** MBC5 + RAM + battery, CGB-only, 16 SRAM banks. The stack tops WRAM bank 1 (it grows down from 0xE000); the
 * frame cache's banks 2..7 are switched in only with interrupts off. */
const LINK_FLAGS = [
  "-autobank",
  "-Wb-ext=.rel",
  "-Wb-max=255",
  "-Wm-yt0x1B",
  "-Wm-yoA",
  "-Wm-ya16",
  "-Wm-yC",
  "-Wm-yS",
  "-Wl-j",
  "-Wl-m",
  "-Wl-g.STACK=0xE000",
];

const { values } = parseArgs({
  options: {
    verify: { type: "boolean", default: false },
    jobs: { type: "string", default: String(availableParallelism()) },
  },
});

process.chdir(ROOT);
const started = performance.now();
const toolchain = findToolchain();
rmSync(BUILD, { recursive: true, force: true });
mkdirSync(OBJ, { recursive: true });

const units = collectSources(join(BUILD, "core"));
await compileAll(units, Math.max(1, Number(values.jobs)));
const objects = units.map((u) => objectPath(u));
run([...LINK_FLAGS, "-o", ROM, ...objects]);

const rom = finishHeader(readFileSync(ROM));
writeFileSync(ROM, rom);
const sha256 = createHash("sha256").update(rom).digest("hex");
const seconds = ((performance.now() - started) / 1000).toFixed(1);
process.stdout.write(`${ROM}  ${sha256}  ${units.length} units  ${seconds}s  (${toolchain.sdccVersion})\n`);

if (values.verify) {
  const expected = readFileSync("release/SHA256SUMS", "utf8").split(/\s+/)[0];
  if (sha256 !== expected) {
    process.stderr.write(`ROM differs from the release: expected ${expected ?? "(none)"}\n`);
    if (process.platform !== "darwin") {
      // Same GBDK release, different host: the Linux SDCC build allocates registers differently in a few units.
      process.stderr.write("The release is reproducible with the macOS GBDK 4.5.0 build; see README, Build.\n");
    }
    process.exit(1);
  }
  process.stdout.write("matches release/crucible.exe.gbc\n");
}

function objectPath(u: Source): string {
  return join(OBJ, `${u.name}.o`);
}

async function compileAll(list: readonly Source[], jobs: number): Promise<void> {
  let next = 0;
  const failures: string[] = [];
  const worker = async (): Promise<void> => {
    for (let u = list[next++]; u !== undefined; u = list[next++]) {
      const flags = [...COMPILE_FLAGS, ...(WAIVERS[u.name] ?? []), "-o", objectPath(u), u.path];
      const result = await runAsync(flags);
      if (result.code !== 0) failures.push(`${u.path}\n${result.output}`);
      else if (result.output.trim()) process.stderr.write(result.output);
    }
  };
  await Promise.all(Array.from({ length: Math.min(jobs, list.length) }, worker));
  if (failures.length) {
    process.stderr.write(failures.join("\n"));
    throw new Error(`${failures.length} translation units failed to compile`);
  }
}

function runAsync(args: readonly string[]): Promise<{ code: number; output: string }> {
  return new Promise((done, fail) => {
    const child = spawn(toolchain.lcc, args);
    let output = "";
    child.stdout.on("data", (d: Buffer) => (output += d.toString()));
    child.stderr.on("data", (d: Buffer) => (output += d.toString()));
    child.on("error", fail);
    child.on("close", (code) => {
      done({ code: code ?? 1, output });
    });
  });
}

function run(args: readonly string[]): void {
  const r = spawnSync(toolchain.lcc, args, { encoding: "utf8" });
  if (r.status !== 0) throw new Error(`lcc ${args[0] ?? ""} failed:\n${r.stdout}${r.stderr}`);
}
