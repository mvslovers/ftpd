#!/bin/sh
# Compares the file names that the installation and removal texts name with the
# files inside the SMP package. Read-only.
#
#   tools/check-install-docs.sh ftpd-X.Y.Z-dist.tar.gz
#   tools/check-install-docs.sh --release vX.Y.Z     (downloads the archive)
#
# 1. Every ftpd-<version>-*.jcl and *.xmit name in docs/installation.md,
#    docs/uninstall.md, ug-install.typ and ug-remove.typ must be a file of the
#    archive, with the version as written in the text or as <version>.
# 2. Every file of the archive must be named in docs/installation.md, which is
#    the README of the package (README.md itself excepted).
#
# It compares names only. The steps of each job still have to be compared by
# hand (internals/releasing.md).
set -eu

usage() { echo "usage: $0 ARCHIVE.tar.gz | $0 --release vX.Y.Z" >&2; exit 2; }
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT INT TERM

case "${1:-}" in
    --release)
        [ $# -eq 2 ] || usage
        gh release download "$2" -R "${GITHUB_REPOSITORY:-mvslovers/ftpd}" -p '*-dist.tar.gz' -D "$tmp"
        archive=$(find "$tmp" -name '*-dist.tar.gz')
        ;;
    "") usage ;;
    *)
        [ $# -eq 1 ] || usage
        archive=$1
        ;;
esac

root=$(cd "$(dirname "$0")/.." && pwd)
tar tzf "$archive" | sed 's|.*/||' | grep -v '^$' | sort > "$tmp/files"
[ -s "$tmp/files" ] || { echo "[check-install-docs] FAIL: $archive is empty" >&2; exit 1; }
ver=$(sed -n 's/^ftpd-\([0-9][0-9.]*\)-.*/\1/p' "$tmp/files" | sort -u)
[ "$(echo "$ver" | wc -l)" -eq 1 ] && [ -n "$ver" ] ||
    { echo "[check-install-docs] FAIL: cannot read one version from the file names" >&2; exit 1; }

fail=0
bad() { echo "[check-install-docs] FAIL: $*" >&2; fail=1; }
pat='ftpd-\([0-9][0-9.]*\|<version>\)-[a-z]*\.\(jcl\|xmit\)'

for t in docs/installation.md docs/uninstall.md \
         docs/books/guide/ug-install.typ docs/books/guide/ug-remove.typ; do
    [ -f "$root/$t" ] || { bad "$t does not exist"; continue; }
    grep -o "$pat" "$root/$t" | sort -u |
        sed "s/<version>/$ver/" | sort -u > "$tmp/named"
    while read -r n; do
        grep -qx "$n" "$tmp/files" || bad "$t names $n, which is not in $(basename "$archive")"
    done < "$tmp/named"
done

grep -o "$pat" "$root/docs/installation.md" | sed "s/<version>/$ver/" | sort -u > "$tmp/readme"
while read -r f; do
    [ "$f" = README.md ] && continue
    grep -qx "$f" "$tmp/readme" || bad "$f is in the archive but docs/installation.md does not name it"
done < "$tmp/files"

if [ "$fail" -ne 0 ]; then
    echo "[check-install-docs] STOP: the texts and the package disagree; see internals/releasing.md." >&2
    exit 1
fi
echo "[check-install-docs] $(wc -l < "$tmp/files" | tr -d ' ') files in the archive, the texts name them and no others."
