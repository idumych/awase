#!/usr/bin/env python3
# fake_qemu.py: stands in for qemu-system-i386 in vm_test.
#   FAKE_QEMU_NVMM=fail  -> exits at once as QEMU does when NVMM can't run a VM
#   -S (the NVMM test)   -> stays up until killed
#   with a second disk    -> the one-time install: reports progress on the
#                            serial file as the guest does, then powers off
#   otherwise             -> the installed VM: serves the monitor socket and
#                            powers off on system_powerdown
# Copyright 2026, Ian Dumych.
# SPDX-License-Identifier: MIT
import os, socket, sys, time

args = sys.argv[1:]
if os.environ.get('FAKE_QEMU_NVMM') == 'fail':
    sys.stderr.write('qemu-system-i386: NVMM: Unable to init: Operation not supported\n')
    sys.exit(1)
if '-S' in args:
    time.sleep(60)
    sys.exit(0)

def value(prefix):
    for a in args:
        if a.startswith(prefix):
            return a[len(prefix):]
    return None

serial = value('file:')
if any('bus=ide.1' in a for a in args):
    with open(serial, 'a') as f:
        for line in ('awase-setup: 5 formatting the 20 GB disk',
                     'awase-setup: 50 copying the system',
                     'awase-setup: 95 making the disk bootable',
                     'awase-setup: 100 done'):
            f.write(line + '\n'); f.flush(); time.sleep(0.3)
    sys.exit(0)

monitor = None
for i, a in enumerate(args):
    if a == '-monitor':
        monitor = args[i + 1][len('unix:'):].split(',')[0]
s = socket.socket(socket.AF_UNIX)
s.bind(monitor); s.listen(4)
while True:
    c, _ = s.accept()
    data = c.recv(4096).decode()
    c.close()
    if 'system_powerdown' in data:
        os.unlink(monitor)
        sys.exit(0)
