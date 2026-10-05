/** clang-format over the hand-written C: `node tools/check-c-format.ts [--fix]`.
 * Baked data under cartridge/data keeps its generator's layout and is not checked. */
import { spawnSync } from "node:child_process";
import { readdirSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { parseArgs } from "node:util";

const VERSION = "23.1.2";
const DIRS = ["cartridge/src", "cartridge/include", "core/include", "core/src", "core/test"];

const { values } = parseArgs({ options: { fix: { type: "boolean", default: false } } });
process.chdir(resolve(dirname(fileURLToPath(import.meta.url)), ".."));

const clangFormat = process.env["CLANG_FORMAT"] ?? "clang-format";
const version = spawnSync(clangFormat, ["--version"], { encoding: "utf8" });
if (version.status !== 0 || !version.stdout.includes(VERSION)) {
  process.stderr.write(`clang-format ${VERSION} required (pip install -r requirements-dev.txt)\n`);
  process.exit(1);
}

const files = DIRS.flatMap((d) => walk(d)).filter((f) => /\.[ch]$/.test(f));
const mode = values.fix ? ["-i"] : ["--dry-run", "--Werror"];
const result = spawnSync(clangFormat, [...mode, ...files], { encoding: "utf8", stdio: "inherit" });
process.exit(result.status ?? 1);

function walk(dir: string): string[] {
  return readdirSync(dir, { withFileTypes: true }).flatMap((e) =>
    e.isDirectory() ? walk(join(dir, e.name)) : [join(dir, e.name)],
  );
}
