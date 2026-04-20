# Linux Screenshot Companion

This Linux port is intentionally screenshot-only. It does not run enforcement,
camera capture, remote desktop, push-up counter, app control, or updater logic.

Build:

```sh
cmake -S . -B build/linux-screenshot -DCMAKE_BUILD_TYPE=Release
cmake --build build/linux-screenshot --target air_companion_linux_screenshot
```

Install for the logged-in desktop user:

```sh
install -Dm755 build/linux-screenshot/air_companion_linux_screenshot "$HOME/.local/bin/air_companion_linux_screenshot"
install -Dm644 packaging/linux/air-companion-linux-screenshot.service "$HOME/.config/systemd/user/air-companion-linux-screenshot.service"
mkdir -p "$HOME/.config/AIRCompanion"
cat > "$HOME/.config/AIRCompanion/linux-screenshot.env" <<'EOF'
AIR_COMPANION_BASE_URL=https://192.168.11.228
AIR_COMPANION_DEVICE_TOKEN=replace-with-device-token
AIR_COMPANION_SCREEN_INTERVAL_SECONDS=30
EOF
systemctl --user daemon-reload
systemctl --user enable --now air-companion-linux-screenshot.service
```

The process must run inside the graphical user session so screenshot backends can
see `DISPLAY`, `WAYLAND_DISPLAY`, and the session DBus address.

Supported screenshot backends are tried in order:

```text
gnome-screenshot, grim, spectacle, scrot, maim, import
```

If the server uses a private TLS certificate, the Linux HTTP client accepts it by
default. Set `AIR_COMPANION_STRICT_TLS=1` in the env file to require normal TLS
validation.
