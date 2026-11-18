# Awase client

The app volunteers run: a native Be API app for Renku OS, in the Awase
ink-and-seal look. It builds for a **RenkuOSPorts coordinator**, the
portal at [Druware/RenkuOSPorts-Portal](https://github.com/Druware/RenkuOSPorts-Portal),
or shows a **simulated demo pool** of the two-build design, in which
nothing is built or uploaded.

## Building for a coordinator

Choose Pool › Coordinator… and enter the coordinator's address, this
machine's build client ID and secret (made on the coordinator's Clients
page; the secret is shown once), and a build command. Awase then does
what the portal's own client does, with the same build-command contract:

- claims the oldest queued job for its client's platform;
- runs `<build command> <port> <version> <outdir>` with
  `RENKUPORTS_PLATFORM`, `RENKUPORTS_JOB_ID` and `RENKUPORTS_RECIPE_PATH`
  set (and `AWASE_CORES`, the cores you lend);
- uploads every `.hpkg` the command left in `<outdir>`, each with its
  SHA-256, then completes the job with the log, or fails it with a reason;
- on quit, stops a running build and reports it failed ("client
  stopped"), so the job goes back to the queue at once.

Pause and "When idle" stop the build's whole process group and resume
it. The window shows the build's step (from HaikuPorter's messages,
roughly), its last log line, the time so far, and the coordinator's
published packages for the platform, with this machine's ringed. The
coordinator publishes a package from one build; the two-build check is
only in the demo for now.

`scripts/awase-mock-build.sh` is a stand-in build command that makes a
small fake package, for trying the whole path without building anything.
A real build command for Renku (HaikuPorter) is still to do.

The settings, including the client's secret, are kept in
`~/config/settings/Awase`, readable only by you.

## The demo pool

The ports in dependency order, builds travelling from each simulated
volunteer's machine to the package, and the 合 seal landing when two
builds match; a mismatch flashes gold and the package is built again.
Hover a package or a machine for its details. Every volunteer and figure
in it is made up, and the window says so.

## Layout

```
core/        portable C++, no Haiku headers:
             Pool, DemoPool    the pool's rules and the demo
             Portal            the coordinator's build-client API (libcurl)
             Builder           runs a build command, pauses it
             LivePool          claim, build, upload, report, on a thread
             Json, Sha256
app/         the Be API app: the window, its hand-drawn views, and the
             Coordinator window
scripts/     awase-mock-build.sh
tests/       core_test (the rules), live_test (against portal_stub.py,
             a stand-in for the portal's build-client API)
tools/       make-sysroot.sh (cross-building), make-icon.py (the icon)
```

## Building

On Renku OS: `make`. It needs libcurl's headers (`curl_devel`).

On Linux, cross-building for Renku x86_64 from a Renku build tree that
has built an image (it supplies the haiku and haiku_devel packages and
the host tools; the script fetches curl_devel):

```
sh tools/make-sysroot.sh ~/path/to/Source/generated.x86_64
. .sysroot-x86_64/env.sh
make HAIKU_CROSS=1
```

`make test` builds and runs the tests with the host compiler (it needs
libcurl and python3): the pool's rules, then the coordinator client
against `tests/portal_stub.py`, which follows the portal's
`BuildClientApi.cs`: claiming, uploads with their SHA-256 checked, a 409
for a different package of the same name, failing, pausing a build, and
stopping mid-build.

## Tested

- On the host: `make test`.
- In QEMU (x86_64, KVM) on a nightly image of RenkuOS/Source `main`: the
  demo pool, hovering, the sliders, When idle/Always, Pause/Resume,
  settings kept on quit, the icon in Tracker. From inside Renku against
  the stand-in server on the host: connecting through the Coordinator
  window, then four queued jobs, three built with the mock command,
  uploaded and published, and one failed and reported with its log.
- Not tested yet: the real portal server (it needs SQL Server and blob
  storage; the stand-in follows its code), a real HaikuPorter build, real
  hardware, the Linux build through Hokku.
