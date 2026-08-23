#!/bin/sh
# SPDX-License-Identifier: MIT
# SVXConnect-Qt — Copyright (c) 2026 Diëlectricum BV
#
# Assemble an apt repository under repo/debian from the .deb files sitting in
# the parent directory (where dpkg-buildpackage leaves them).
#
#   packaging/publish-apt.sh [suite] [--sign KEYID]
#
# The result is a directory of ordinary static files. Serve it over HTTPS from
# anything — object storage, GitHub Pages, a plain web server. apt needs no
# server-side support at all.
#
# WHY apt-ftparchive AND NOT reprepro
# -----------------------------------
# reprepro maintains a database and is the better tool for a repository with
# many packages, several suites and incremental uploads. This publishes a
# handful of packages from scratch every time, where a database is a liability
# rather than an asset — and apt-ftparchive is in dpkg/apt-utils, which is
# already installed anywhere a .deb gets built.
#
# SIGNING
# -------
# Unsigned by default, because a signing key is yours to create and guard, not
# something a script should conjure. apt WILL refuse an unsigned repository
# unless the client opts out per-source, so an unsigned repo is for local
# testing only.
#
# To create a key for this, once:
#
#     gpg --quick-generate-key "SVXConnect Repository <ure@on6ure.be>" \
#         default default 5y
#     gpg --list-secret-keys --keyid-format=long
#
# then publish the public half next to the repository so users can fetch it:
#
#     gpg --export KEYID > repo/debian/svxconnect-archive-keyring.pgp
#
# Note that is the raw binary form, NOT ASCII-armoured: a .pgp file referenced
# by Signed-By must be a keyring, and `gpg --armor --export` produces something
# apt will not read. Getting this wrong yields "no valid OpenPGP data found",
# which is not a helpful error.
set -eu

cd "$(dirname "$0")/.."
ROOT=$(pwd)

SUITE=${1:-trixie}
[ "${1:-}" = "--sign" ] && SUITE=trixie
KEYID=""
while [ $# -gt 0 ]; do
    case "$1" in
        --sign) KEYID=${2:?--sign needs a key id}; shift 2 ;;
        *)      shift ;;
    esac
done

REPO="$ROOT/repo/debian"
ORIGIN="SVXConnect"
LABEL="SVXConnect"
COMPONENT="main"

DEBS=$(ls "$ROOT"/../*.deb 2>/dev/null || true)
if [ -z "$DEBS" ]; then
    echo "no .deb files in $(cd "$ROOT/.." && pwd) — run dpkg-buildpackage first" >&2
    exit 1
fi

rm -rf "$REPO"
mkdir -p "$REPO/dists/$SUITE/$COMPONENT"

# pool/main/s/<source>/ — the layout apt expects, and the one that keeps
# several source packages from colliding.
for deb in $DEBS; do
    src=$(dpkg-deb -f "$deb" Source 2>/dev/null || true)
    [ -n "$src" ] || src=$(dpkg-deb -f "$deb" Package)
    src=${src%% *}                      # strip any "(version)" suffix
    letter=$(printf '%s' "$src" | cut -c1)
    dest="$REPO/pool/$COMPONENT/$letter/$src"
    mkdir -p "$dest"
    cp "$deb" "$dest/"
done

ARCHES=$(for deb in $DEBS; do dpkg-deb -f "$deb" Architecture; done | sort -u)

for arch in $ARCHES; do
    dir="$REPO/dists/$SUITE/$COMPONENT/binary-$arch"
    mkdir -p "$dir"
    ( cd "$REPO" && apt-ftparchive --arch "$arch" packages pool ) > "$dir/Packages"
    gzip -9kf "$dir/Packages"
    cat > "$dir/Release" <<EOF
Archive: $SUITE
Component: $COMPONENT
Origin: $ORIGIN
Label: $LABEL
Architecture: $arch
EOF
done

ARCH_LIST=$(echo "$ARCHES" | tr '\n' ' ' | sed 's/ *$//')

# Valid-Until is deliberately generous but present. Omit it and apt never
# notices a repository that has silently stopped being updated; set it too
# short and every user's `apt update` starts failing the moment you go on
# holiday. Re-run this script (or just re-sign) monthly from CI.
( cd "$REPO" && apt-ftparchive \
    -o "APT::FTPArchive::Release::Origin=$ORIGIN" \
    -o "APT::FTPArchive::Release::Label=$LABEL" \
    -o "APT::FTPArchive::Release::Suite=$SUITE" \
    -o "APT::FTPArchive::Release::Codename=$SUITE" \
    -o "APT::FTPArchive::Release::Components=$COMPONENT" \
    -o "APT::FTPArchive::Release::Architectures=$ARCH_LIST" \
    -o "APT::FTPArchive::Release::Description=SVXConnect for Debian and Ubuntu" \
    release "dists/$SUITE" ) > "$REPO/dists/$SUITE/Release"

if [ -n "$KEYID" ]; then
    rm -f "$REPO/dists/$SUITE/Release.gpg" "$REPO/dists/$SUITE/InRelease"
    gpg --default-key "$KEYID" --armor --detach-sign \
        -o "$REPO/dists/$SUITE/Release.gpg" "$REPO/dists/$SUITE/Release"
    gpg --default-key "$KEYID" --clearsign \
        -o "$REPO/dists/$SUITE/InRelease" "$REPO/dists/$SUITE/Release"
    gpg --export "$KEYID" > "$REPO/svxconnect-archive-keyring.pgp"
    echo "signed with $KEYID"
else
    echo "NOT SIGNED — apt will refuse this repository."
    echo "  Re-run with --sign KEYID once you have a key; see the header of this script."
fi

# The deb822 source stanza users install. Both Debian 13 and Ubuntu 24.04+
# parse this natively.
cat > "$REPO/svxconnect.sources" <<EOF
Types: deb
URIs: https://apt.svxconnect.app/debian
Suites: $SUITE
Components: $COMPONENT
Architectures: $ARCH_LIST
Signed-By: /usr/share/keyrings/svxconnect-archive-keyring.pgp
EOF

# Pin by ORIGIN, not by a package glob. The Debian wiki is explicit that a
# third-party repository must be constrained with a label the user controls: a
# glob-only pin does not stop this repository shadowing libc6, the priority-100
# catch-all does.
cat > "$REPO/svxconnect.pref" <<EOF
Package: *
Pin: origin apt.svxconnect.app
Pin-Priority: 100

Package: svxconnect svxconnect-qt
Pin: origin apt.svxconnect.app
Pin-Priority: 500
EOF

echo
echo "repository: $REPO"
find "$REPO" -type f | sed "s|$REPO|  .|" | sort
