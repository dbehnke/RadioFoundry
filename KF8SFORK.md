<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 The RadioFoundry Authors -->
<!-- Copyright (c) 2026 David Behnke -->

# KF8S combined test builds

`kf8sfork` combines unmerged changes for listening tests. Feature branches stay separate for upstream review.
This is a private test build based on RadioFoundry. Contact: dbehnke (KF8S).

Documentation for testers:

- [Usage and testing guide](docs/kf8sfork/README.md): settings, expected behavior, and a report template.
- [Fork changelog](docs/kf8sfork/CHANGELOG.md): changes from the first fork feature through November 24, 2026.

Current additions include digital RX AGC, P25 microphone AGC and DC removal, optional P25 caller lookup,
caller/RF display improvements, full TX power controls, and configurable microphone keys. The temporary P25
calibration is optional and off in the standard package. The guide identifies the current tagged firmware.

## Build and package

Merge current upstream `origin/main` into `kf8sfork` and commit reviewed changes first. Initialize the submodules
and install both toolchains as described in [building](docs/dev/building.md). Then run:

```
python3 tools/kf8s_build.py --self-test
python3 tools/kf8s_build.py --jobs 8
```

The wrapper refuses other branches and dirty trees. It fetches upstream main and requires that commit to be an
ancestor of the build. It builds every board and band/power option in the board registry, runs each image's stack
check, rejects build warnings, and packages codec-free images with flasher manifests and license notices.

Every image, manifest and ZIP in one batch uses:

```
kf8s-<upstream-main-short-hash>-<kf8sfork-short-hash>-<yymmdd>-<sequence>
```

Hashes have eight characters. Dates use America/Detroit. The sequence starts at 1 each day and increments once
per build attempt, including failed attempts, so a label is never reused. The counter is local Git metadata shared
by worktrees. Keep it when moving the build setup. The actual Git commit, build-source metadata and crash ID keep
their normal meaning. The full private label is the release field; the old CPS Git field stays a short commit.
Ordinary builds and other branches retain upstream version behavior.

Output is under `build/kf8s/<label>/`. The ZIP includes every image and manifest, `SHA256SUMS`, build metadata,
`LICENSE`, `AUTHORS`, `CONTRIBUTORS` and third-party licenses. Keep those notices when sharing it, as
[FORKING.md](FORKING.md) requires. No AMBE codec is included.

Host tests and firmware checks do not replace the owner's listening tests. No build command flashes or keys a radio.
