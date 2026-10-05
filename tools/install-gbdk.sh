#!/bin/sh
# Install the pinned GBDK-2020 4.5.0 (SDCC 4.5.1 #15267), checking the release's sha256. The release ROM is
# byte-reproducible only with this compiler.
#
# Usage: sh tools/install-gbdk.sh   (installs into $GBDK_HOME, default ~/.cache/gbdk-4.5.0)
#
# GBDK must not live under a path containing ".exe": SDCC aborts on any argument that contains it, and lcc passes
# its own install path to SDCC. That rules out a toolchain directory inside a clone named crucible.exe.
set -eu

VERSION=4.5.0
DEST="${GBDK_HOME:-${XDG_CACHE_HOME:-$HOME/.cache}/gbdk-$VERSION}"

case "$DEST" in
  *.exe*) echo "GBDK_HOME must not contain '.exe' (SDCC aborts on such paths): $DEST" >&2; exit 1 ;;
esac

case "$(uname -s)-$(uname -m)" in
  Linux-x86_64) ASSET=gbdk-linux64.tar.gz SHA=d7857a5f6d135ee4c249043ca26aad9f2ec8ab5d4106d97720d404114f42605c ;;
  Linux-aarch64 | Linux-arm64) ASSET=gbdk-linux-arm64.tar.gz SHA=31eb2235f0fdb60163d0b1e9574a022098d6069cd56606a1daca4478a46e0439 ;;
  Darwin-arm64) ASSET=gbdk-macos-arm64.tar.gz SHA=289ee60e46c5a2785a21e35533f84a5131ed4a063b21b0dbdedc9a10af15bf78 ;;
  Darwin-x86_64) ASSET=gbdk-macos.tar.gz SHA=1aa549d12032d8f6509d11923bb28b1a453098f42597feb378e9a42541f8fd89 ;;
  *) echo "no pinned GBDK $VERSION build for $(uname -s)-$(uname -m)" >&2; exit 1 ;;
esac

if [ -x "$DEST/bin/lcc" ] && "$DEST/bin/sdcc" --version 2>/dev/null | grep -q '4.5.1 #15267'; then
  echo "GBDK $VERSION already in $DEST"
  exit 0
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
curl -fsSL -o "$TMP/$ASSET" "https://github.com/gbdk-2020/gbdk-2020/releases/download/$VERSION/$ASSET"
if command -v sha256sum >/dev/null 2>&1; then
  echo "$SHA  $TMP/$ASSET" | sha256sum -c -
else
  echo "$SHA  $TMP/$ASSET" | shasum -a 256 -c -
fi
tar -xzf "$TMP/$ASSET" -C "$TMP"
rm -rf "$DEST"
mkdir -p "$(dirname "$DEST")"
mv "$TMP/gbdk" "$DEST"
# macOS quarantines downloaded binaries; GBDK's are unsigned.
if [ "$(uname -s)" = Darwin ]; then xattr -dr com.apple.quarantine "$DEST" 2>/dev/null || true; fi
"$DEST/bin/sdcc" --version | head -1
echo "installed in $DEST"
