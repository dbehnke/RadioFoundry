<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 The RadioFoundry Authors -->
<!-- Copyright (c) 2026 David Behnke -->

# KF8S Fork Changelog

Changes added to `kf8sfork` from its first feature commit on November 23, 2026, through November 24, 2026.
This lists fork-specific changes, not the features inherited from upstream RadioFoundry.
See the [usage and testing guide](README.md) for menu paths and test instructions.

## Current test build

| Item | Value |
|---|---|
| Firmware source | `eb02cb21` |
| Upstream main included | `dd67ecd6` |
| Release tag | [`kf8s-dd67ecd6-eb02cb21-261124-6`](https://github.com/dbehnke/RadioFoundry/tree/kf8s-dd67ecd6-eb02cb21-261124-6) |
| ZIP | `radiofoundry-kf8s-dd67ecd6-eb02cb21-261124-6.zip` |
| Images | RT3S / MD-UV380 standard 5 W, MD9600, DM1701, DM32 |
| Optional calibration | Off in this package |
| DMR AES | Enabled in this package |

The tag identifies the firmware source. Later documentation commits do not change that firmware.
The local ZIP is not automatically uploaded to GitHub by pushing this tag.

## November 24, 2026

### Upstream synchronization after the tagged test build

- Merged upstream through `bac4b209`, including the FM Home receive-frequency/offset display change (#1043).
- Reviewed upstream's microphone/P25 limiter follow-up (#1054). Kept the fork's existing shared DSP limiter,
  computed DC removal, manual P25 gain handling and -20 dBFS microphone AGC target in overlapping code.
  The AMBE board correction is not added to P25. Existing audio regression tests remain authoritative.
- These changes are newer than the firmware tag listed above.

### Receive display rollback after the tagged test build

- Removed the digital RF signal-display enhancement because the added information crowded the display.
- Restored the earlier caller/dual layouts and removed the added RX details preference and measurement metadata.
- Preserved the earlier caller lookup/display improvements, RX/TX AGC, power controls and microphone keys.
- This source rollback is newer than the firmware tag above. The existing ZIP still has the meter enhancement;
  no replacement firmware was built for this change. The original feature branch is retained.


### Microphone input and P25 calibration

- Added streaming microphone DC-offset removal before P25 gain and level measurement. The estimate is computed
  from the incoming samples, not a fixed correction measured on one radio. Live DMR/AMBE, FM and receive audio
  retain their existing paths.
- Expanded the temporary codec diagnostic with microphone DC, AC/speech energy, rail counts and comparable
  encoder/decoder measurements. An intermediate diagnostic also supported AMBE; that diagnostic was later removed.
- Replaced the diagnostic with a P25-only calibration that records two seconds of conditioned speech once, tests
  five input targets, and verifies a proposed target against approximately -25 dBFS decoded active speech.
  The recommendation does not save settings. The earlier ten-second parrot is not the current calibration workflow.
- Set the shared live P25 microphone AGC input target to **-20 dBFS** after speech calibration on DM32 and DM1701
  consistently recommended it. Decoded level remains dependent on the speech and decoder; -25 dBFS is not guaranteed.
- Retained calibration behind `CONFIG_P25_PARROT`, default **off**. It needs a compact memory profile when enabled.

Key commits: [`6121f678`](https://github.com/dbehnke/RadioFoundry/commit/6121f678),
[`d199960a`](https://github.com/dbehnke/RadioFoundry/commit/d199960a),
[`db5fa879`](https://github.com/dbehnke/RadioFoundry/commit/db5fa879),
[`97a826cf`](https://github.com/dbehnke/RadioFoundry/commit/97a826cf).

### Transmit power controls

- Added a persistent **Default TX power** setting, with Factory default and all board-supported levels.
- Changed Quick Settings Power from a three-level cycle to a picker containing all supported levels.
- Made inherited channel power explicit with **Radio's (level)** and corrected the temporary-power indicator.
- Added connected-radio power choices to the web codeplug editor, including band-specific names where provided.
  Existing codeplug encoding and calibrated level sets remain unchanged.
- Added a bounded power-metadata diagnostic view and updated the feature-cost documentation.

Key commits: [`897aea14`](https://github.com/dbehnke/RadioFoundry/commit/897aea14),
[`dc8b2fe2`](https://github.com/dbehnke/RadioFoundry/commit/dc8b2fe2).

### Digital RF signal display (later reverted)

- Standardized the existing **-120 to -60 dBm bar** and actual numeric dBm reading across digital caller,
  compact, large-text, dual and receive-audio layouts where space permits. Numbers can extend beyond the bar range.
- Kept caller identity stable instead of rotating it with signal information.
- Added a saved **RX details** choice between Caller details and Signal details for secondary information.
- Added measurement validity, age and source tracking. Held readings carry `~`; unavailable or expired readings
  show dashes. Dual-watch history is not presented as a simultaneous live measurement.
- Preserved DMR per-burst RSSI preference and its existing fallback hold. Limited ordinary meter presentation
  updates to four per second while keeping essential state changes immediate.
- Captured UI view data and change counters together to avoid delaying essential events.
- Isolated UI tick scratch storage from synchronous key paths so the firmware stack check passes without
  increasing the UI task's allocation or reducing its required margins.

Key commits: [`a78929cb`](https://github.com/dbehnke/RadioFoundry/commit/a78929cb),
[`6bb8278c`](https://github.com/dbehnke/RadioFoundry/commit/6bb8278c).

### MD9600 microphone keys and navigation

- Exposed **Mic A-D**, with separate press and hold assignments, in the existing **Settings > Keys** menu.
  Existing panel-key assignments and microphone defaults are preserved.
- Added assignable **Back**, using normal navigation and unsaved-edit confirmation.
- Changed microphone **A/B short press to OK**. A menu hold follows normal OK-hold behavior; a Home hold selects
  the other dual pane or standalone VFO memory. It does not implicitly start dual watch.
- Preserved digit entry, frequency entry, key lock, PTT and microphone A-D DTMF symbols during FM transmission.
- Protected pending editor text when navigating Home, including at maximum menu depth. Expanded real-board
  assignment-row tests and fixed simulator label caching when changing simulated radios.

Key commits: [`4336b41d`](https://github.com/dbehnke/RadioFoundry/commit/4336b41d),
[`e446e6a4`](https://github.com/dbehnke/RadioFoundry/commit/e446e6a4).

## November 23, 2026

### Digital receive audio AGC

- Added one shared receive-speech controller for DMR, P25 and NXDN, with independent per-mode targets, default Off.
- Added optional averaged input/output audio levels in dBFS, before speaker volume.
- Added fast initial acquisition, slower speech tracking, bounded gain, peak/startup protection and a ten-caller
  RAM history. Caller history clears at power-off. FM and local UI sounds are outside this normalization.
- Integrated enabled AGC levels with caller displays and removed redundant AGC stand-in text.

Key commits: [`29ce516e`](https://github.com/dbehnke/RadioFoundry/commit/29ce516e),
[`57ceec26`](https://github.com/dbehnke/RadioFoundry/commit/57ceec26),
[`4b7bf742`](https://github.com/dbehnke/RadioFoundry/commit/4b7bf742).

### Caller identification

- Added optional P25 caller labels from private contacts and the installed DMR ID database, default Off.
  Labels are local lookups; P25 and DMR numeric IDs do not necessarily identify the same person.
- Improved caller identity on P25 and dual Home views, and guarded lookup tests for feature-off configurations.

Key commits: [`9bb6caac`](https://github.com/dbehnke/RadioFoundry/commit/9bb6caac),
[`fa578c7b`](https://github.com/dbehnke/RadioFoundry/commit/fa578c7b),
[`162b7a1d`](https://github.com/dbehnke/RadioFoundry/commit/162b7a1d).

### Transmit audio, diagnostics and upgrade handling

- Added shared transmit leveling and peak protection, P25 microphone PCM AGC, and an optional local P25
  encode/decode/playback diagnostic. The initial P25 target was -25 dBFS; November 24 changed it to -20 dBFS.
- Added microphone DC and speech-level measurements to help distinguish bias from useful microphone audio.
- Protected AES counter state when loading older AGC/settings layouts. Ambiguous legacy counters block AES
  transmission until recovery; factory reset preserves counter/recovery state. Added translated recovery screens.

Key commits: [`9af5cf47`](https://github.com/dbehnke/RadioFoundry/commit/9af5cf47),
[`d541ac0e`](https://github.com/dbehnke/RadioFoundry/commit/d541ac0e),
[`9e83b326`](https://github.com/dbehnke/RadioFoundry/commit/9e83b326),
[`26835ae5`](https://github.com/dbehnke/RadioFoundry/commit/26835ae5).

### Build and package support

- Added private `kf8sfork` build labels with upstream hash, fork hash, date and unique daily sequence.
- Added clean-tree/provenance checks, board/profile validation, warning and stack checks, flasher manifests,
  checksums, and ZIP packaging without embedding stock AMBE codecs.
- Added diagnostic build-profile validation, radio-independent packaging guidance, feature-matrix entries and
  compiled-component/runtime license notices in the packages.

Key commits: [`fef6efbd`](https://github.com/dbehnke/RadioFoundry/commit/fef6efbd),
[`299144ae`](https://github.com/dbehnke/RadioFoundry/commit/299144ae),
[`fc71a4d5`](https://github.com/dbehnke/RadioFoundry/commit/fc71a4d5),
[`f4c1f4e5`](https://github.com/dbehnke/RadioFoundry/commit/f4c1f4e5),
[`1b1ddc06`](https://github.com/dbehnke/RadioFoundry/commit/1b1ddc06).

## Validation and remaining checks

Independent implementation/integration reviews accepted the power, RF display and microphone-key changes.
Host/UI, feature-off, translation, documentation and layout checks passed. The current four-image package passed
firmware size, stack, provenance and checksum checks with warning-free final build logs. C-SKY build-tool crashes
required retries; they did not require source changes.

These checks do not establish hardware RF-power accuracy, RSSI calibration, microphone-key behavior, or on-air
speech quality. Owner testing is the purpose of this build. Earlier on-device calibration informed the P25 target;
it is not a validation of every feature on every radio.
