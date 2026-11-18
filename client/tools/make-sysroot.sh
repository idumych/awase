#!/bin/sh
# make-sysroot.sh RENKU_GENERATED [OUT]
# A sysroot for cross-building Awase for Renku OS x86_64 on Linux, from a
# Renku build tree's generated.x86_64 (after an image build): the haiku and
# haiku_devel packages it built, plus the gcc_syslibs, ICU and zlib build
# packages, and libcurl. Writes OUT (default ./.sysroot-x86_64) and OUT/env.sh.
#
# Copyright 2026, Ian Dumych, idumych@gmail.com.
# SPDX-License-Identifier: MIT
set -eu
G=$(cd "$1" && pwd)
OUT=$(mkdir -p "${2:-.sysroot-x86_64}" && cd "${2:-.sysroot-x86_64}" && pwd)
PKG=$G/objects/linux/x86_64/release/tools/package/package
TOOLS=$G/objects/linux/x86_64/release/tools
LIBS=$G/objects/linux/lib
rm -rf "$OUT/sys"; mkdir -p "$OUT/sys"
for p in haiku haiku_devel; do
	(cd "$OUT/sys" && LD_LIBRARY_PATH=$LIBS "$PKG" extract -C . "$G/objects/haiku/x86_64/packaging/packages/$p.hpkg")
done
for d in "$G"/build_packages/gcc_syslibs-* "$G"/build_packages/gcc_syslibs_devel-* \
		"$G"/build_packages/icu*-x86_64 "$G"/build_packages/zlib-*-x86_64; do
	[ -d "$d" ] && cp -a "$d"/. "$OUT/sys/"
done
# libcurl, for talking to a build coordinator: the runtime package is on the
# Renku image; the headers come from the same build-packages repository the
# Renku build uses.
CURL=curl-8.21.0-1
PORTS=https://eu.hpkg.haiku-os.org/haikuports/master/build-packages/packages
mkdir -p "$OUT/dl"
for p in $CURL curl_devel-${CURL#curl-}; do
	f=$OUT/dl/$p-x86_64.hpkg
	[ -f "$f" ] || cp "$G/download/$p-x86_64.hpkg" "$f" 2>/dev/null \
		|| curl -sfL -o "$f" "$PORTS/$p-x86_64.hpkg"
	(cd "$OUT/sys" && LD_LIBRARY_PATH=$LIBS "$PKG" extract -C . "$f")
done

CROSS=$(dirname "$G")/../renku-src/generated.x86_64/cross-tools-x86_64
[ -d "$CROSS" ] || CROSS=$G/cross-tools-x86_64
cat > "$OUT/env.sh" <<EOT
export HAIKU_SYSROOT="$OUT/root"
export HAIKU_CXX="$(cd "$CROSS" && pwd)/bin/x86_64-unknown-haiku-g++"
export HAIKU_TOOLS="$TOOLS"
export LD_LIBRARY_PATH="$LIBS\${LD_LIBRARY_PATH:+:\$LD_LIBRARY_PATH}"
EOT
mkdir -p "$OUT/root/boot" && ln -sfn ../../sys "$OUT/root/boot/system"
echo "sysroot: $OUT/sys"
