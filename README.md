# AIR Companion

AIR Companion is the Windows-first native agent for student machines.

## Repository

- repo root:
  - [C:\Users\user\codex\air-companion](C:\Users\user\codex\air-companion)
- platform repo:
  - [C:\Users\user\codex\school-system-redo](C:\Users\user\codex\school-system-redo)
- consolidated handoff:
  - [C:\Users\user\codex\AIR_HANDOFF_2026-03-31.md](C:\Users\user\codex\AIR_HANDOFF_2026-03-31.md)

## What it does now

- runs as a Windows service:
  - `AIRCompanion`
- enrolls with the AIR platform using a one-time enrollment token
- persists device config locally
- renews device auth tokens
- uploads heartbeats and activity
- uploads screenshots and camera captures
- reports focused/open apps and installed apps
- enforces blocked-process policy
- supports remote-control helper startup
- supports in-place update downloads and updater launch

## Important binaries

- `air_companion_service`
  - service host and main agent loop
- `air_companion_tray`
  - utility/helper entry point
  - writes enrollment request files
  - runs one-shot helper operations used by the service

## Current local paths on installed machines

- install directory:
  - `C:\Program Files\AIR Companion`
- service config:
  - `%APPDATA%\AIRCompanion\config.json`
- backup config:
  - `%APPDATA%\AIRCompanion\config.backup.json`
- service-owned fallback config:
  - `C:\ProgramData\AIRCompanion\Service\config.json`
- pending enrollment request:
  - `C:\ProgramData\AIRCompanion\Internal\enrollment-request.json`
- machine capture settings:
  - `C:\ProgramData\AIRCompanion\Internal\capture-settings.json`
- service log:
  - `C:\Windows\System32\config\systemprofile\AppData\Roaming\AIRCompanion\debug.log`
- installer/enrollment logs:
  - `C:\ProgramData\AIRCompanion\Logs`

## Build

Recommended:

```powershell
cmake --preset windows-release
cmake --build --preset windows-release
```

Debug build:

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug
```

## Test

Release tests:

```powershell
ctest -C Release --output-on-failure --test-dir build\windows-release
```

Debug tests:

```powershell
ctest -C Debug --output-on-failure --test-dir build\windows-debug
```

## Installer and update packaging

- installer script:
  - [C:\Users\user\codex\air-companion\installer\install-companion.ps1](C:\Users\user\codex\air-companion\installer\install-companion.ps1)
- bundle builder:
  - [C:\Users\user\codex\air-companion\installer\build-installer-bundle.ps1](C:\Users\user\codex\air-companion\installer\build-installer-bundle.ps1)

Current expectations:

- package from `windows-release`, not debug output
- installer stages itself before elevation
- installer self-elevates
- installer maintains the Windows service and firewall rules
- enrollment script writes the machine-local enrollment request consumed by the service

## Backend contract in use

The companion targets the AIR platform companion API, including:

- `POST /api/companion/enroll/claim`
- `POST /api/companion/token/renew`
- `POST /api/companion/revoke`
- `POST /api/companion/heartbeat`
- `GET /api/companion/policy`
- `POST /api/companion/activity`
- `POST /api/companion/captures/screen`
- `POST /api/companion/captures/camera`
- `GET /api/companion/commands/next`
- `POST /api/companion/commands/{id}/acknowledge`
- `POST /api/companion/commands/{id}/result`
- update manifest and update package endpoints served by the platform

## Notes that matter operationally

- internet control was removed from the live runtime
- enrollment is token-based, not student-password-based
- screenshot capture uses an interactive helper staging through:
  - `C:\Users\Public\AIRCompanion\InteractiveCapture`
- remote control requires the platform-side start flow and the companion-side firewall rule path
- config loss on transient token renew failure was fixed; only explicit auth rejection should clear config now
- auto-update previously got stuck if an updater launch stalled; that failure mode was patched

## Related files

- core agent:
  - [C:\Users\user\codex\air-companion\src\core](C:\Users\user\codex\air-companion\src\core)
- Windows adapters:
  - [C:\Users\user\codex\air-companion\src\adapters\windows](C:\Users\user\codex\air-companion\src\adapters\windows)
- service host:
  - [C:\Users\user\codex\air-companion\src\service](C:\Users\user\codex\air-companion\src\service)
- tray/helper host:
  - [C:\Users\user\codex\air-companion\src\tray](C:\Users\user\codex\air-companion\src\tray)
