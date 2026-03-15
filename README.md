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
- first-run bootstrap through `AIR_COMPANION_*` environment variables
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
- Internet policy is derived from task templates and schedule state.
- The companion is structured to route student traffic through the AIR host by setting gateway and DNS to the AIR server IP when policy sync runs.
- The current Windows network implementation is still a stub adapter, but the core loop now carries the real gateway/DNS intent and reports network identity upstream.
- First enrollment currently happens by launching the binary with:
  - `AIR_COMPANION_BASE_URL`
  - `AIR_COMPANION_USERNAME`
  - `AIR_COMPANION_PASSWORD`
  - optional `AIR_COMPANION_DEVICE_LABEL`
- No legacy compatibility is included here.
- This scaffold is intentionally stub-heavy right now: it defines the native app shape and contracts, while the Laravel platform side already exposes the first companion API surface.
