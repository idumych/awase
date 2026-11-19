#!/bin/sh
# build-bfs-shell.sh OUT_DIR [WORK_DIR]
#
# Builds Haiku's bfs_shell host tool (and the libroot_build.so it needs) from
# RenkuOS/Source, for Awase on Linux: it writes the x86 build VM's one-time
# install job into a copy of Renku's image, as Awase's mount does on Haiku
# (Linux can't write BFS). Host tools only (configure --host-only), so no
# cross compiler: a few minutes. Needs git, make, gcc/g++, bison, flex,
# zlib's headers. SOURCE=<RenkuOS/Source checkout> skips its clone, and
# JAM=<jam binary> (Haiku's buildtools' jam) skips building jam.
#
# Copyright 2026, Ian Dumych.
# SPDX-License-Identifier: MIT
set -eu
OUT=$(mkdir -p "$1" && cd "$1" && pwd)
WORK=$(mkdir -p "${2:-.bfs-shell}" && cd "${2:-.bfs-shell}" && pwd)
JOBS=$(nproc 2>/dev/null || echo 4)

SRC=${SOURCE:-$WORK/haiku}
[ -n "${SOURCE:-}" ] || [ -d "$SRC/.git" ] \
	|| git clone -q --depth 1 https://github.com/RenkuOS/Source "$SRC"
JAM=${JAM:-$(ls "$WORK"/buildtools/jam/bin.*/jam 2>/dev/null | head -1 || true)}
if [ -z "$JAM" ]; then
	# Haiku's buildtools (jam), from its GitHub mirror
	[ -d "$WORK/buildtools/.git" ] || git clone -q --depth 1 \
		https://github.com/haiku/buildtools "$WORK/buildtools"
	make -C "$WORK/buildtools/jam" -j"$JOBS" > "$WORK/jam.log" 2>&1
	JAM=$(ls "$WORK"/buildtools/jam/bin.*/jam | head -1)
fi

mkdir -p "$WORK/generated" "$WORK/bin"
# configure asks nasm for its ELF formats even for host tools, which never
# use it: a stand-in answers
cat > "$WORK/bin/nasm-stub" <<'STUB'
#!/bin/sh
printf '  elf32 \n  elf64 \n'
STUB
chmod +x "$WORK/bin/nasm-stub"
[ -f "$WORK/generated/build/BuildConfig" ] \
	|| ( cd "$WORK/generated" && HAIKU_NASM="$WORK/bin/nasm-stub" "$SRC/configure" --host-only \
		> "$WORK/configure.log" 2>&1 ) || { tail -20 "$WORK/configure.log" >&2; exit 1; }
( cd "$WORK/generated" && "$JAM" -q -j"$JOBS" '<build>bfs_shell' > "$WORK/jam-bfs.log" 2>&1 ) \
	|| { tail -30 "$WORK/jam-bfs.log" >&2; exit 1; }

G=$WORK/generated/objects/linux
cp "$(ls -d "$G"/*/release/tools/bfs_shell/bfs_shell | head -1)" "$OUT/bfs_shell"
cp "$G/lib/libroot_build.so" "$OUT/"
# Found next to itself, wherever it's installed
command -v patchelf >/dev/null 2>&1 && patchelf --set-rpath '$ORIGIN' "$OUT/bfs_shell" || :
echo "built $OUT/bfs_shell"
