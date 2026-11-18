#!/usr/bin/env python3
# alpine-sysroot.py ARCH DEST PKG...
#
# Unpacks Alpine Linux packages, with their dependencies, into DEST as plain
# archives, for cross-compiling a static awase-tui (tools/release-tui.sh).
# Nothing from the packages is run: no apk, no install scripts.
#
# Copyright 2026, Ian Dumych.
# SPDX-License-Identifier: MIT
import io, os, re, subprocess, sys, tarfile, urllib.request

BRANCH = os.environ.get('ALPINE_BRANCH', 'v3.24')
MIRROR = 'https://dl-cdn.alpinelinux.org/alpine'
arch, dest, wanted = sys.argv[1], sys.argv[2], sys.argv[3:]
cache = os.path.join(os.path.dirname(os.path.abspath(dest)), 'apk-cache', arch)
os.makedirs(cache, exist_ok=True)

def fetch(url):
    with urllib.request.urlopen(url) as r:
        return r.read()

pkgs, provides = {}, {}
for repo in ('main', 'community'):
    raw = fetch(f'{MIRROR}/{BRANCH}/{repo}/{arch}/APKINDEX.tar.gz')
    with tarfile.open(fileobj=io.BytesIO(raw)) as t:
        text = t.extractfile('APKINDEX').read().decode()
    for block in text.split('\n\n'):
        f = {}
        for line in block.splitlines():
            if len(line) > 2 and line[1] == ':':
                f[line[0]] = line[2:]
        if 'P' not in f or f['P'] in pkgs:
            continue
        f['repo'] = repo
        pkgs[f['P']] = f
        for p in f.get('p', '').split():
            provides.setdefault(re.split('[=<>~]', p)[0], f['P'])

def name(token):
    return re.split('[=<>~]', token)[0]

todo, done = list(wanted), set()
while todo:
    n = name(todo.pop())
    if n.startswith('!'):
        continue
    p = n if n in pkgs else provides.get(n)
    if p is None:
        sys.exit(f'{arch}: nothing provides {n}')
    if p in done:
        continue
    done.add(p)
    todo += pkgs[p].get('D', '').split()

os.makedirs(dest, exist_ok=True)
for p in sorted(done):
    f = pkgs[p]
    fn = f"{p}-{f['V']}.apk"
    path = os.path.join(cache, fn)
    if not os.path.exists(path):
        with open(path, 'wb') as o:
            o.write(fetch(f"{MIRROR}/{BRANCH}/{f['repo']}/{arch}/{fn}"))
    # An apk is concatenated gzip streams (signature, control, data); GNU tar
    # reads past the first with --ignore-zeros. Skip the control files.
    subprocess.run(['tar', '-xzf', path, '-C', dest, '--ignore-zeros', '--no-same-owner',
                    '--exclude=.PKGINFO', '--exclude=.SIGN.*', '--exclude=.pre-*',
                    '--exclude=.post-*', '--exclude=.trigger'], check=True,
                   stderr=subprocess.DEVNULL)
print(f'{arch}: {len(done)} packages in {dest}')
