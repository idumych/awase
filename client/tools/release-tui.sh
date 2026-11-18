#!/bin/sh
# release-tui.sh VERSION [ARCH...]
#
# Builds static awase-tui binaries for Linux, one per platform the portal
# builds packages for (x86, x86_64, arm64, riscv64, or the ARCHs given),
# into release/awase-tui-VERSION-linux-ARCH.tar.gz with SHA256SUMS.
#
# Cross-compiled with clang and lld on any Linux host, against Alpine
# sysroots (musl, OpenSSL, zlib, ncurses: tools/alpine-sysroot.py) and a
# libcurl built here with HTTP and HTTPS only, since Alpine's static
# nghttp2, psl, zstd and brotli hold GCC LTO code that lld can't link.
# Work files go to $WORK (default .release-work).
#
# Copyright 2026, Ian Dumych.
# SPDX-License-Identifier: MIT
set -eu
VERSION=$1; shift
ARCHS=${*:-x86 x86_64 arm64 riscv64}
HERE=$(cd "$(dirname "$0")/.." && pwd)
WORK=$(mkdir -p "${WORK:-$HERE/.release-work}" && cd "${WORK:-$HERE/.release-work}" && pwd)
OUT=$HERE/release
CURL=8.23.0
# Alpine's aports, main/curl/APKBUILD (3.24-stable)
CURL_SHA512=69cf0feafdc79fbad007cf2fc977b777ce6ccb05d01afd17b5a42f8bedbfcc342fc3a48193faa7f3eafe6800cd7d07948916e8733db1b9de60287ed06219b840
PKGS="musl-dev gcc g++ linux-headers openssl-dev openssl-libs-static zlib-dev zlib-static ncurses-dev ncurses-static"
JOBS=$(nproc)

cd "$WORK"
if [ ! -f curl-$CURL.tar.xz ]; then
	curl -sfLO https://curl.se/download/curl-$CURL.tar.xz
fi
echo "$CURL_SHA512  curl-$CURL.tar.xz" | sha512sum -c --quiet
mkdir -p "$OUT"

for arch in $ARCHS; do
	case $arch in
		x86) alpine=x86 ;;
		x86_64) alpine=x86_64 ;;
		arm64) alpine=aarch64 ;;
		riscv64) alpine=riscv64 ;;
		*) echo "unknown platform $arch" >&2; exit 1 ;;
	esac
	sys=$WORK/sysroot-$alpine
	[ -d "$sys/usr/lib/gcc" ] || python3 -I "$HERE/tools/alpine-sysroot.py" $alpine "$sys" $PKGS
	triple=$(ls "$sys/usr/lib/gcc" | grep alpine-linux-musl)
	cc="clang --target=$triple --sysroot=$sys -fuse-ld=lld"

	prefix=$WORK/curl-$alpine
	if [ ! -f "$prefix/lib/libcurl.a" ]; then
		rm -rf curl-build-$alpine && mkdir curl-build-$alpine
		tar xJf curl-$CURL.tar.xz -C curl-build-$alpine --strip-components=1
		(cd curl-build-$alpine && ./configure -q --host=$triple CC="$cc" CFLAGS=-O2 \
			--prefix="$prefix" --disable-shared --enable-static \
			--with-openssl --with-zlib --enable-threaded-resolver \
			--with-ca-bundle=/etc/ssl/certs/ca-certificates.crt \
			--without-libpsl --without-brotli --without-zstd --without-nghttp2 \
			--without-libidn2 --without-libssh2 --disable-ldap --disable-ldaps \
			--disable-dict --disable-file --disable-ftp --disable-gopher \
			--disable-imap --disable-mqtt --disable-pop3 --disable-rtsp \
			--disable-smb --disable-smtp --disable-telnet --disable-tftp \
			--disable-ipfs --disable-manual --disable-docs \
			&& make -s -j$JOBS -C lib && make -s -C lib install && make -s -C include install)
	fi

	bin=$WORK/awase-tui-$arch
	(cd "$HERE" && clang++ --target=$triple --sysroot="$sys" -fuse-ld=lld -static \
		-std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -pthread \
		-DAWASE_STATIC -DCURL_STATICLIB -I"$prefix/include" -Icore -o "$bin" \
		tui/main.cpp core/Pool.cpp core/DemoPool.cpp core/Json.cpp core/Sha256.cpp \
		core/Portal.cpp core/Builder.cpp core/LivePool.cpp core/Thermal.cpp \
		-L"$prefix/lib" -lcurl -lssl -lcrypto -lz -lncursesw -s)

	name=awase-tui-$VERSION-linux-$arch
	rm -rf "$WORK/$name" && mkdir "$WORK/$name"
	cp "$bin" "$WORK/$name/awase-tui"
	cp "$HERE/tui/awase.conf.example" "$HERE/scripts/awase-mock-build.sh" "$HERE/../LICENSE" "$WORK/$name/"
	cp "$HERE/README.md" "$WORK/$name/README.md"
	tar czf "$OUT/$name.tar.gz" -C "$WORK" --owner=0 --group=0 "$name"
	echo "$name: $(file -b "$bin" | cut -d, -f1-2)"
done
(cd "$OUT" && sha256sum awase-tui-$VERSION-linux-*.tar.gz > SHA256SUMS)
