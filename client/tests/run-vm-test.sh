#!/bin/sh
# Builds nothing: runs objects/vm_test against stand-ins for Renku's release
# (a local web server), QEMU (fake_qemu.py) and ssh (a script that succeeds).
# Copyright 2026, Ian Dumych.
# SPDX-License-Identifier: MIT
set -eu
cd "$(dirname "$0")/.."
T=$PWD/objects/vmtest
rm -rf "$T"
mkdir -p "$T/www" "$T/bin"

# Renku's nightly, with a small "image" and its checksum
head -c 3000000 /dev/urandom > "$T/www/RenkuOS-x86_gcc2h-20261118-build00093.iso"
(cd "$T/www" && sha256sum RenkuOS-x86_gcc2h-20261118-build00093.iso \
	> RenkuOS-x86_gcc2h-20261118-build00093.iso.sha256)
# and its repository, with the devel packages HaikuPorter needs
mkdir -p "$T/repo/packages"
echo devel > "$T/repo/packages/haiku_devel-r1~beta6_hrev1-1-x86_gcc2.hpkg"
echo devel > "$T/repo/packages/haiku_x86_devel-r1~beta6_hrev1-1-x86_gcc2.hpkg"
echo other > "$T/repo/packages/zlib-1.3-1-x86_gcc2.hpkg"
(cd "$T/repo" && tar cf "$T/www/renkuos-x86_gcc2-repo.tar" packages)
(cd "$T/www" && sha256sum renkuos-x86_gcc2-repo.tar > renkuos-x86_gcc2-repo.tar.sha256)
port=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1])')
U=http://127.0.0.1:$port
cat > "$T/www/release.json" <<EOF
{"assets": [
 {"name": "RenkuOS-arm64-20261118-build00093.img.zst", "browser_download_url": "$U/nothing"},
 {"name": "RenkuOS-x86_gcc2h-20261118-build00093.iso", "browser_download_url": "$U/RenkuOS-x86_gcc2h-20261118-build00093.iso"},
 {"name": "RenkuOS-x86_gcc2h-20261118-build00093.iso.sha256", "browser_download_url": "$U/RenkuOS-x86_gcc2h-20261118-build00093.iso.sha256"},
 {"name": "renkuos-x86_gcc2-repo.tar", "browser_download_url": "$U/renkuos-x86_gcc2-repo.tar"},
 {"name": "renkuos-x86_gcc2-repo.tar.sha256", "browser_download_url": "$U/renkuos-x86_gcc2-repo.tar.sha256"}
]}
EOF
python3 -m http.server "$port" --bind 127.0.0.1 --directory "$T/www" 2> "$T/http.log" &
server=$!
trap 'kill $server 2>/dev/null' EXIT

# QEMU and ssh stand-ins, found first on PATH
cp tests/fake_qemu.py "$T/bin/qemu-system-i386"
chmod +x "$T/bin/qemu-system-i386"
cat > "$T/bin/ssh" <<'EOF'
#!/bin/sh
# fake ssh: logs the remote command and succeeds
for last; do :; done
echo "$last" >> "$(dirname "$0")/../ssh.log"
case $last in *guest-build.sh*) echo "===> Building zlib"; echo "[] done";; esac
exit 0
EOF
chmod +x "$T/bin/ssh"
for i in $(seq 1 50); do
	python3 -c "import urllib.request; urllib.request.urlopen('$U/release.json')" 2>/dev/null && break
	sleep 0.1
done
PATH="$T/bin:$PATH" ./objects/vm_test "$T" "$U"
