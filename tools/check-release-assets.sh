#!/bin/sh
# Checks the assets of a published GitHub release of ftpd. Read-only.
#
#   tools/check-release-assets.sh vX.Y.Z [NOTES_FILE]
#
# 1. The release must carry the SMP files (ftpd-X.Y.Z-dist.tar.gz, -dist.zip,
#    -load.xmit), one PDF of each book (ml06-0001-*.pdf, ml06-0002-*.pdf) and
#    SHA256SUMS.
# 2. Every file name in the release text must be an asset of the release, and
#    every asset must be named in the text. The text is NOTES_FILE when given
#    (the page as drafted, before it is published), else the body of the release.
# 3. SHA256SUMS must list exactly the other assets, and every sum must match the
#    file as downloaded from the release.
#
# Exit 0 only when all three hold; anything else stops the release.
set -eu

[ $# -ge 1 ] && [ $# -le 2 ] || { echo "usage: $0 vX.Y.Z [NOTES_FILE]" >&2; exit 2; }
tag=$1
notes=${2:-}
ver=${tag#v}
repo=${GITHUB_REPOSITORY:-mvslovers/ftpd}

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT INT TERM
fail=0
bad() { echo "[check-release-assets] FAIL: $*" >&2; fail=1; }

gh release view "$tag" -R "$repo" --json assets --jq '.assets[].name' | sort > "$tmp/assets"
[ -s "$tmp/assets" ] || { echo "[check-release-assets] FAIL: $tag has no assets (or does not exist)" >&2; exit 1; }

for f in "ftpd-$ver-dist.tar.gz" "ftpd-$ver-dist.zip" "ftpd-$ver-load.xmit" SHA256SUMS; do
    grep -qx "$f" "$tmp/assets" || bad "$tag lacks the asset $f"
done
for b in ml06-0001 ml06-0002; do
    grep -q "^$b-[0-9]*\.pdf\$" "$tmp/assets" || bad "$tag lacks a PDF of $b"
done

if [ -n "$notes" ]; then
    cp "$notes" "$tmp/text" || exit 2
else
    gh release view "$tag" -R "$repo" --json body --jq .body > "$tmp/text"
fi
grep -o '[A-Za-z0-9][A-Za-z0-9._-]*\.\(tar\.gz\|zip\|xmit\|pdf\)\|SHA256SUMS' "$tmp/text" | sort -u > "$tmp/named"
while read -r n; do
    grep -qx "$n" "$tmp/assets" || bad "the release text names $n, which is not an asset of $tag"
done < "$tmp/named"
while read -r a; do
    grep -qx "$a" "$tmp/named" || bad "the asset $a of $tag is not named in the release text"
done < "$tmp/assets"

if grep -qx SHA256SUMS "$tmp/assets"; then
    mkdir "$tmp/dl"
    gh release download "$tag" -R "$repo" -D "$tmp/dl"
    awk '{print $2}' "$tmp/dl/SHA256SUMS" | sort > "$tmp/summed"
    grep -vx SHA256SUMS "$tmp/assets" | diff - "$tmp/summed" > "$tmp/diff" ||
        bad "SHA256SUMS does not list exactly the other assets of $tag:$(printf '\n'; cat "$tmp/diff")"
    (cd "$tmp/dl" && sha256sum -c SHA256SUMS) >&2 || bad "a sum in SHA256SUMS does not match the downloaded file"
fi

if [ "$fail" -ne 0 ]; then
    echo "[check-release-assets] $tag: STOP. Do not publish the page; see internals/releasing.md." >&2
    exit 1
fi
echo "[check-release-assets] $tag: $(wc -l < "$tmp/assets" | tr -d ' ') assets, text and SHA256SUMS agree."
