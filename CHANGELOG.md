# Changelog

All notable changes to **Chiron** are documented here.

## [0.2.0] — 2026-11-22

### Added

- Linux x86_64 AppImage packaging (`scripts/build-appimage.sh`) and GitHub Actions release workflow
- Product README with real Chiron UI screenshots
- Heroic / Dark1HGL GOG discovery via `gog_store/installed.json` (any install path / drive)
- Headless launch through `heroic://launch/gog/<appName>` (PATH, `.desktop`, AppImage dirs, or Flatpak)
- `providers.gog.launch_helper` for a custom Dark1HGL / Heroic binary or AppImage

### Fixed

Linux GOG import / launch (bug hunt):

- Shallow `goggame-*.info` scan — no longer crawls Proton/Wine `pfx` / `drive_c` trees
- Strip AppImage library overrides (`LD_LIBRARY_PATH`, `APPDIR`, Qt plugin vars) when launching Heroic/Proton
- Resolve Dark1HGL/Heroic AppImages outside PATH; skip Heroic import with a clear warning if none found
- Escape `launch_helper` paths that contain spaces
- Prefer `goggame-<appName>.info` and root-level infos over nested DLC metadata
- One library entry per GOG ID (Heroic/Proton launch preferred over native `start.sh` when both exist)
- Keep entries when an install path is missing (unmounted drive) instead of dropping them silently
- Prefer native/AppImage helpers before Flatpak
- Harden JSON edge cases (`is_dlc` strings, backslash paths, array-shaped `installed.json`)

### Notes

- Supported artifact: **Linux x86_64 AppImage only**
- Config/cache use the `chiron` app name (for example `~/.config/chiron`, `~/.cache/chiron`)

## [0.1.0] — 2026-11-22

### Added

- Initial Chiron fork: rebrand (app id `chiron`, desktop id `org.dark1ltg.Chiron`), Linux AppImage focus, and first-pass Heroic/Dark1HGL GOG import
