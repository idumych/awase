#!/bin/sh
# awase-mock-build.sh <port> <version> <outdir>
# A stand-in build command, for trying Awase against a coordinator without
# building anything. It follows the RenkuOSPorts portal's build-command
# contract and makes one small, fake <port>-<version>-1-<arch>.hpkg, the
# same bytes every time, unless MOCK_VARIANT=<text> is set (it adds the
# text: a build that won't match another). Ports named fail* fail; ports
# named slow* take SLOW_SECONDS (default 20) and print HaikuPorter-like
# steps on the way; ports named withdep* also leave a dependency's package
# (depof_<port>), which the coordinator records but doesn't publish.
#
# Copyright 2026, Ian Dumych, idumych@gmail.com.
# SPDX-License-Identifier: MIT
set -eu
[ $# -eq 3 ] || { echo "usage: $0 <port> <version> <outdir>" >&2; exit 2; }
port=$1 version=$2 out=$3
arch=${RENKUPORTS_PLATFORM:-x86_64}
echo "mock build of $port $version for $arch on ${AWASE_CORES:-1} cores"
case $port in
fail*)
	echo "error: mock failure for $port" >&2
	exit 1 ;;
slow*)
	step=$(( ${SLOW_SECONDS:-20} / 4 ))
	echo "Downloading $port-$version.tar.gz"; sleep "$step"
	echo "Running PREPARE()"; sleep "$step"
	echo "Running BUILD()"; sleep "$step"
	echo "Running INSTALL()"; sleep "$step" ;;
esac
mkdir -p "$out"
{
	printf 'mock hpkg\nport=%s\nversion=%s\narch=%s\n' "$port" "$version" "$arch"
	[ -z "${MOCK_VARIANT:-}" ] || printf 'variant=%s\n' "$MOCK_VARIANT"
} > "$out/$port-$version-1-$arch.hpkg"
echo "wrote $out/$port-$version-1-$arch.hpkg"
case $port in
withdep*)
	printf 'mock dependency of %s\n' "$port" > "$out/depof_$port-1.0-1-$arch.hpkg"
	echo "wrote $out/depof_$port-1.0-1-$arch.hpkg" ;;
esac
