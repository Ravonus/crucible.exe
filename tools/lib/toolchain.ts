import { spawnSync } from "node:child_process";
import { existsSync } from "node:fs";
import { homedir } from "node:os";
import { join, resolve } from "node:path";

/** The release ROM is reproducible only with this exact compiler. */
export const SDCC_VERSION = "4.5.1 #15267";
export const GBDK_VERSION = "4.5.0";

export interface Toolchain {
  readonly lcc: string;
  readonly sdccVersion: string;
}

/** GBDK from $GBDK_HOME, else where tools/install-gbdk.sh puts it. */
export function findToolchain(): Toolchain {
  const cache = process.env["XDG_CACHE_HOME"] ?? join(homedir(), ".cache");
  const home = process.env["GBDK_HOME"] ?? join(cache, `gbdk-${GBDK_VERSION}`);
  if (home.includes(".exe")) {
    // lcc hands its install path to SDCC, which aborts on any argument containing ".exe".
    throw new Error(`GBDK must not live under a path containing ".exe": ${home}`);
  }
  const lcc = resolve(home, "bin", "lcc");
  if (!existsSync(lcc)) {
    throw new Error(`GBDK ${GBDK_VERSION} not found at ${home}: run tools/install-gbdk.sh or set GBDK_HOME`);
  }
  const probe = spawnSync(resolve(home, "bin", "sdcc"), ["--version"], { encoding: "utf8" });
  const sdccVersion = `${probe.stdout}${probe.stderr}`.split("\n")[0]?.trim() ?? "";
  if (!sdccVersion.includes(SDCC_VERSION)) {
    throw new Error(`SDCC ${SDCC_VERSION} (GBDK ${GBDK_VERSION}) required, found: ${sdccVersion || "nothing"}`);
  }
  return { lcc, sdccVersion };
}
