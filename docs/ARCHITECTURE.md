# AIR Companion Architecture

## Processes

### Service / agent

Owns:

- enrollment
- token storage
- policy polling
- heartbeats
- activity uploads
- scheduled and on-demand captures
- remote command execution
- local enforcement

### Tray UI

Owns:

- showing connection state
- showing active student/device identity
- showing the latest policy summary
- exposing local diagnostics and logs

The tray UI is optional at runtime. Closing it must not stop enforcement.

## Core modules

- `core/AgentConfig`
  - persistent config and runtime settings
- `core/AgentLoop`
  - main orchestration loop
- `network/CompanionApiClient`
  - AIR platform API surface
- `platform/PlatformAdapters`
  - interfaces for OS-specific behavior

## Windows-first adapter set

- screen capture adapter
- camera capture adapter
- focused/open app adapter
- browser domain adapter
- app blocker adapter
- network configuration adapter
- service lifecycle adapter

Linux support should implement the same interface set without changing the core loop contract.

## Current uplink behavior

The native agent now has a dedicated uplink coordinator that:

- fetches policy from AIR
- sends heartbeat payloads with:
  - hostname
  - label
  - app version
  - ipv4
  - mac address
  - gateway ipv4
  - adapter name
- sends focused-app and open-app activity events
- derives the AIR gateway target from the configured API base URL host
- asks the network adapter to bind gateway and DNS to that AIR host

The Windows network adapter is still stubbed, but the core flow is now aligned with the Laravel companion API contract and the gateway-policy design on `192.168.11.228`.
