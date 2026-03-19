# AIR Companion

Windows-first native companion app for student machines.

This app is split into two binaries:

- `air_companion_service`
  - background agent / service host
  - handles enrollment, token persistence, policy polling, heartbeats, activity uploads, capture scheduling, and command execution
- `air_companion_tray`
  - small tray-facing UI shell
  - surfaces current student identity, connection state, policy state, and diagnostics

## Current status

This directory contains the initial implementation scaffold for the new companion architecture:

- core agent loop and config model
- platform adapter interfaces
- Windows adapter stubs
- companion API client contract matching the Laravel backend
- heartbeat and activity uplink wiring for the AIR companion API
- network identity collection and Windows gateway/DNS adapter scaffolding
- persisted local config under `%APPDATA%\\AIRCompanion\\config.json`
- optional root CA download URL persisted with the local config
- hidden internal capture settings under `%PROGRAMDATA%\\AIRCompanion\\Internal\\capture-settings.json`
- first-run bootstrap through a native enrollment window in the tray app
- Windows trusted-root bootstrap for the AIR platform certificate
- automatic boot-start registration through a Windows `ONSTART` scheduled task
- service host entry point
- tray app entry point
- Visual Studio-friendly CMake build files

## Build

Recommended on Windows with Visual Studio 2022:

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug
```

Or directly:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022"
cmake --build build --config Debug
```

## Test

Run the native companion test suite with:

```powershell
ctest -C Debug --output-on-failure --test-dir build\windows-debug
```

## Folder layout

- `docs/`
  - architecture notes and implementation guidance
- `include/companion/`
  - public headers for models, networking, adapters, core logic, service, and tray
- `src/core/`
  - agent orchestration, policy sync, command polling, capture scheduling, and enforcement coordination
- `src/networking/`
  - AIR platform API client surface
- `src/adapters/windows/`
  - Windows-first adapter stubs
- `src/service/`
  - Windows service/agent host
- `src/tray/`
  - tray process and small diagnostics shell

## Backend contract

This app targets only the current AIR platform. It expects the companion API group:

- `POST /api/companion/enroll`
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

## Notes

- V1 is Windows-first and structured for Linux adapters later.
- Internet policy is currently paused; the companion reports network identity but does not rewrite gateway or DNS.
- First enrollment now happens through a small native window when the tray starts without a saved config.
- Enrollment/settings can also carry an optional root CA URL when the AIR platform serves its trusted root certificate from a custom endpoint.
- Successful tray/service startup now re-registers automatic boot start by default for `air_companion_service.exe`.
- You can reopen enrollment settings on an installed machine with:
  - `air_companion_tray --settings`
- Headless bootstrap also accepts `AIR_COMPANION_ROOT_CA_URL` for the same override.
- Screen/camera capture behavior is also driven by a separate hidden machine-level settings file for:
  - local capture enablement
  - minimum capture intervals
  - output directories
  - content types
- No legacy compatibility is included here.
- This scaffold is intentionally stub-heavy right now: it defines the native app shape and contracts, while the Laravel platform side already exposes the first companion API surface.
