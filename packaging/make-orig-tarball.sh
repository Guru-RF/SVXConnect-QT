#!/bin/sh
# SPDX-License-Identifier: MIT
# Build the .orig tarball for the Debian source package.
#
# The C core is a git submodule, and submodules do not survive dpkg-source, so
# both trees are exported at their committed revisions and combined. See
# debian/README.source.
set -eu

cd "$(dirname "$0")/.."
ROOT=$(pwd)

VERSION=${1:-$(dpkg-parsechangelog -SVersion | sed 's/-[^-]*$//')}
NAME="svxconnect-qt-${VERSION}"
OUT="../${NAME%%-$VERSION}_${VERSION}.orig.tar.xz"

if [ -n "$(git status --porcelain)" ]; then
    echo "warning: working tree is dirty; the tarball exports COMMITTED state" >&2
fi

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

mkdir -p "$TMP/$NAME"
git archive HEAD | tar -x -C "$TMP/$NAME"

# The submodule, at exactly the revision this repository pins.
mkdir -p "$TMP/$NAME/third_party/svxconnect-cli"
git -C third_party/svxconnect-cli archive HEAD \
    | tar -x -C "$TMP/$NAME/third_party/svxconnect-cli"

# debian/ belongs to the .debian tarball, not the .orig one.
rm -rf "$TMP/$NAME/debian"

tar --sort=name --owner=0 --group=0 --numeric-owner \
    --mtime="@$(git log -1 --format=%ct)" \
    -C "$TMP" -cJf "$ROOT/$OUT" "$NAME"

echo "wrote $OUT"
echo "  core pinned at $(git -C third_party/svxconnect-cli rev-parse --short HEAD)"
