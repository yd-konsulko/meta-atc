#!/bin/sh
# scripts/atcd-add-release.sh: add the current atcd release of a track as
# recipes-core/atc/atcd_<version>.bb, a recipe that requires atcd.inc.
#
# install-atcd.sh fetches the binary of each target and checks the signature
# of the release index and the binary's sha256, so the digests written into the
# recipe come from the signed index.
#
# Usage: scripts/atcd-add-release.sh [track]   (default: stable)
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TRACK="${1:-stable}"

die() {
  echo "atcd-add-release: $*" >&2
  exit 1
}

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

version=""
sha_aarch64=""
sha_x86_64=""
for target in linux-aarch64-musl linux-x86_64-musl; do
  log="$(ATC_TRACK="$TRACK" ATC_TARGET="$target" ATC_DOWNLOAD_ONLY="$work/atcd-$target" \
    sh "$ROOT/scripts/install-atcd.sh" 2>&1)" || {
    printf '%s\n' "$log" >&2
    die "install-atcd.sh failed for $target"
  }
  # install-atcd.sh prints "atcd <version>, sha256 <digest>".
  line="$(printf '%s\n' "$log" | sed -n 's/^install-atcd: atcd \([^ ,]*\), sha256 \([0-9a-f]\{64\}\)$/\1 \2/p')"
  [ -n "$line" ] || die "install-atcd.sh printed no version and sha256 for $target"
  v="${line% *}"
  sha="${line#* }"
  # PV comes from the file name: no '_', and nothing a path would misread.
  case "$v" in
    "" | *[!0-9A-Za-z.+~-]*) die "release version '$v' cannot be a recipe version" ;;
  esac
  [ -z "$version" ] || [ "$v" = "$version" ] ||
    die "track $TRACK moved from $version to $v between two targets; run again"
  version="$v"
  case "$target" in
    linux-aarch64-musl) sha_aarch64="$sha" ;;
    linux-x86_64-musl) sha_x86_64="$sha" ;;
  esac
done

recipe="$ROOT/recipes-core/atc/atcd_$version.bb"
content="require atcd.inc

ATCD_SHA256:aarch64 = \"$sha_aarch64\"
ATCD_SHA256:x86-64 = \"$sha_x86_64\""

if [ -f "$recipe" ]; then
  [ "$(cat "$recipe")" = "$content" ] ||
    die "$recipe exists with other digests. The same version with other bytes: check it"
  echo "atcd-add-release: atcd $version is already in the layer"
  exit 0
fi
printf '%s\n' "$content" > "$recipe"
echo "atcd-add-release: added recipes-core/atc/atcd_$version.bb (track $TRACK)"
