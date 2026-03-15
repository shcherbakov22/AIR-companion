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
- service lifecycle adapter

Linux support should implement the same interface set without changing the core loop contract.
