# Awase: volunteer package builds for Renku OS

*Draft, 17 November 2026. A proposal for the Renku OS contributors to discuss before anything is built.*

> **Since then (18 November 2026):** the two-build check now runs in Dru's [RenkuOSPorts portal](https://github.com/Druware/RenkuOSPorts-Portal), which takes the place of the pool server described here, and Awase is a build client for it. Some parts changed on the way: builds are compared by contents rather than bytes, and signing is still to come ([portal issue #2](https://github.com/Druware/RenkuOSPorts-Portal/issues/2)). [How it runs](https://idumych.github.io/awase/orchestration.html) describes the pool as it is. This document is kept as it was written.

Awase (合わせ, "bringing two things together to see whether they match") is an app that volunteers install to lend their computer's spare time to building [RenkuOSPorts](https://github.com/RenkuOS/RenkuOSPorts) packages. It works like SETI@home or a mining pool. A pool server hands out build jobs and keeps the score. Every package is built twice, on two volunteers' machines, and published only if both builds match. So neither a single volunteer nor the pool server can slip a bad package in.

## Where things stand

- **One builder, one architecture.** RenkuOSPorts builds packages only for arm64, nightly, on a single self-hosted runner (`.github/workflows/arm64-packages.yml`). That runner is a 12-vCPU VM that runs arm64 Haiku under QEMU, and a gcc update takes about 2.5 days. x86_64 and x86_gcc2h images still use HaikuPorts' build packages.
- **The build guest is already sealed.** The guest has no network. The host fetches sources and checks each against the recipe's `CHECKSUM_SHA256` (`fetch-recipe.sh`).
- **Nothing is signed.** pkgman checks `repo` against `repo.sha256`, then each package against `repo` (`RefreshRepositoryRequest.cpp`, `PackageManager.cpp`). All of these come from one server, so whoever controls the builder or that server controls what users install.
- **Policy points this way.** Decision D2 in PlanAndGovernance (open, blocks the first ISO) says to "own and sign the repository". The engineering charter asks for reproducible builds, and asks who will pay for and own the build machine.

## How it feels to a volunteer

A volunteer installs the app, pastes the invitation code a maintainer gave them, and picks how much of the machine to lend. The app then builds whenever the computer is idle, up to the limits they set. It shows what it's building, how far along it is, and how much they've contributed.

Settings:

- **When to build:** only when idle, always, or between set hours; and pause on battery power.
- **How much:** how many cores, how much memory, and how much disk the cache may use.
- **Temperature:** pause above a temperature, as the arm64 runner's `temp-guard.sh` does at 85 °C.
- **Which jobs:** architectures, and the longest job they'll take on.

## The pieces

```
RenkuOSPorts main ──► pool server ──job──► volunteer A (client + sealed guest) ──signed result──┐
                          │       └─same job─► volunteer B (client + sealed guest) ──signed result──┤
                          ▼                                                                       │
                 two matching sha256? ◄────────────────────────────────────────────────────────────┘
                   │ yes                        │ no
                   ▼                            ▼
          candidate repository        flagged for a maintainer
                   │
                   ▼
   release key holder re-runs the assembly and signs offline ──► server and mirrors
```

### The client app

The client is the only thing a volunteer installs. Builds have to run on Renku, so where the client runs decides how it builds:

| Volunteer runs | How it builds | Architectures |
|---|---|---|
| Linux with KVM (most volunteers) | A Renku build guest under QEMU/KVM, with no network, as the arm64 runner does today | x86_64 and x86_gcc2h at full speed. arm64 is emulated, which is slow, so it suits only machines willing to run long jobs. |
| arm64 Linux with KVM (Ampere, Raspberry Pi 5) | An arm64 Renku guest under KVM | arm64 at full speed |
| Renku itself | HaikuPorter's chroot builds, natively, with a native app and a Deskbar replicant | The machine's own architecture. The chroot can't cut off the network, so sources are fetched first, and the second build catches anything odd. |
| Windows or macOS | Later, through QEMU with the platform's hypervisor (WHPX or HVF) | Same as Linux, once tested |

### The pool server

The pool server plays the part of a mining pool's coordinator. It watches RenkuOSPorts `main`, turns each change into jobs with the logic `update-packages.sh` already uses (HaikuPorter's `--ports-for-files`), and hands them out:

- **Two copies of each job** go to two volunteers, on machines belonging to different people where possible.
- **Dependencies:** a job is handed out only once its build packages have been accepted. It carries the sha256 of the snapshot to build against, so both copies start from the same inputs.
- **Priority:** packages that hold up many others (gcc, glib, openssl) go first, along with security fixes.
- **Deadlines:** each copy has a deadline, scaled to how long the port took last time. The client sends a heartbeat. A copy that misses its deadline is handed to someone else.
- **Size matching:** jobs go only to clients whose limits fit.

The server holds no signing key and compiles nothing. Anyone can rebuild its state from RenkuOSPorts and the results archive, so a lost or compromised server is replaced rather than trusted.

### A job and a result

The protocol is plain HTTPS and JSON:

```
POST /enroll        invitation code, client public key, machine limits
POST /work          "give me work": architectures, cores, memory, max hours
  → { job: "qt6_base-6.8.2-1", arch: "x86_64",
      recipe_rev: "9f3c1e2…", inputs: "sha256:4b1d…",
      guest: "sha256:77ae…", sources: [{url, sha256}], deadline }
POST /work/:id/beat progress and the last lines of the log
POST /work/:id/done result record signed with the client key, then the hpkg uploads
```

The signed result names the recipe commit, the input snapshot, the guest image and each output package's sha256. Results are kept in a public archive.

### Accepting a package

A package is accepted when two volunteers' results for the same job and inputs have the same sha256. A mismatch goes to a maintainer with both results and logs attached. It means either a nondeterministic recipe or a misbehaving machine.

> **Unknown:** nobody has yet measured whether HaikuPorter builds are bit-for-bit reproducible. Timestamps inside hpkg files, build paths and parallel-make ordering are the usual causes when they aren't. Phase 0 measures this first. Until a port is fixed, it goes on a `single-build.list`: one build is accepted only if a maintainer checks the package's contents and co-signs it. New-architecture bootstraps start on that list too.

### Signing the repository

The pool assembles a candidate repository from the accepted packages with `package_repo`. The same packages always give the same `repo` file. A release key holder, of whom there might be two or three named by the council, re-runs the assembly on their own machine, checks they get the same `repo.sha256`, and signs it. The key never touches the pool server or a volunteer's machine.

pkgman can't check signatures yet. At first, the download server and mirrors refuse an unsigned or wrongly signed repository. After that, a package-kit change makes pkgman check `repo.sha256.sig` against keys in `repository_infos`. That's a high-risk path under REVIEW.md §4, so it needs two approvals, and it settles the signing half of D2.

### Credit

The client shows build-hours and accepted builds, and the pool's web page has a leaderboard. Credit counts only accepted builds. It's recognition only: there's no token or money, and none is planned.

## What it protects against

| If this happens | What stops it |
|---|---|
| A volunteer's machine is compromised and builds a backdoored package | The other copy doesn't match, so the package isn't accepted, and the mismatch points at the machine. |
| A client's key is stolen | The key is revoked. Every package it helped accept drops back to one build and is rebuilt automatically. |
| The pool server is compromised | It can't produce a second matching signed result by itself, and it can't sign the repository. |
| Someone changes a package on a mirror | Its hash no longer matches `repo`, and `repo` is signed. |
| Two colluding volunteers get both copies of one job | The server never gives both copies to one person and picks pairs at random. Core packages can require three matching builds. |
| A malicious recipe gets merged | Out of scope. Recipe review in RenkuOSPorts is the defence, as now. |

## Who can join

Joining is by invitation to start with. A maintainer issues an invitation code, the client generates its key when it enrolls, and the volunteer appears on a public list of builders. Because every package needs two matching builds, the pool could later open to anyone without changing the design.

## Rollout

1. **Phase 0: measure.** Build 50 ports twice on two machines and compare hashes. Fix the common causes, such as honouring `SOURCE_DATE_EPOCH` for timestamps in packages.
2. **Phase 1: a minimal pool with arm64.** The pool server, plus a headless Linux client built from the current `bootstrap/arm64` scripts. The current arm64 runner becomes the first client. The repository is signed by hand.
3. **Phase 2: x86_64 and the app.** Add x86_64 jobs, so Renku images no longer need HaikuPorts' build packages. Release the Linux client with settings and progress, and invite more volunteers.
4. **Phase 3: native and signed.** The native Renku client with its Deskbar replicant, and the package-kit change for signature checks.

## Decisions for the project

1. **Build our own pool or run BOINC.** BOINC already handles redundancy, credit and VM jobs. But it assumes independent work units. Ours depend on each other and can run for days, and its VM support is built around VirtualBox. The recommendation is a small pool of our own that borrows BOINC's ideas, but it's worth trying BOINC first.
2. **An owner for D2**, and who holds the release key.
3. **Where the pool server runs** and who runs it.
4. **How many matching builds:** two to start, perhaps three for the toolchain.
5. **Who issues invitations**, until `MAINTAINERS` exists.
