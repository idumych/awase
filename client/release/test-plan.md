# Test plan: Awase

Awase is a network client: it claims build jobs from a RenkuOSPorts coordinator, runs a
build command, uploads the packages and reports back. Without a coordinator it shows a
simulated demo pool.

## Fixtures

**The coordinator:** `tests/portal_stub.py`, a stand-in that follows the portal's
build-client API (the same routes, JSON and rules). Start it on the host, listening where
the tested system can reach it, with its log as evidence:

    python3 tests/portal_stub.py 8099 0.0.0.0 2> dist/V/evidence/{p}-portal.log &

The address is the host's LAN IP for VMs (`ip route get 1.1.1.1 | grep -oP 'src \K\S+'`)
and `127.0.0.1` under Xvfb. Make a build client for the platform being tested and queue a
job for it (platform `x86_64` for Haiku amd64 and the x86_64 AppImage, `arm64` for Renku
arm64, `amd64`/`arm64` builds use `x86_64`/`arm64`):

    curl -s -XPOST -d '{"platform":"x86_64"}' http://127.0.0.1:8099/stub/clients
    #  -> {"clientId": N, "secret": "..."}
    curl -s -XPOST -d '{"port":"zlib","version":"1.3.1","platform":"x86_64"}' \
        http://127.0.0.1:8099/stub/jobs

**The build command:** `scripts/awase-mock-build.sh`, copied to the tested system (e.g.
`~/awase-mock-build.sh`, executable). It writes a small fake package for the port.

## Fresh state

- Haiku / Renku: `rm -rf ~/config/settings/Awase`
- Linux (Cosmoe): `rm -rf ~/.config/cosmoe/Awase` (the AppImage's throwaway HOME under
  `xtest.sh` is fresh already)
- VitruvianOS: `rm -rf ~/config/settings/Awase`

## Feature checks

### demo

Steps: start Awase with no settings. It opens on the demo pool. Wait 30 seconds, then take
a screenshot of the window and OCR it into `dist/V/evidence/{p}-demo.txt`.

PASS when: the OCR text has "sealed" (the pool's "N of 23 sealed" count) and at least one
seal has landed (the count is above 0), and the screenshot shows the pool drawn: seals,
threads and the "you" marker, not an empty window.

### coordinator

Steps: choose **Pool › Coordinator…**. Type the stub's address (`http://<address>:8099`) in
"Coordinator:", the client ID in "Client ID:", the secret in "Secret:", and the mock build
command's path in "Build command:". Click **Connect**. Wait until the activity list says the
build was uploaded (about 30 seconds).

PASS when: the stub's log, `dist/V/evidence/{p}-portal.log`, has
`POST /api/client/jobs/<id>/complete` answered 200, and the window shows the job's port in
"Now building" or the repository wall. The stub's `/stub/state` lists the build with its
package.
