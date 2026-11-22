# Chiron

Chiron is a **Linux x86_64 AppImage** fork of [Pegasus Frontend](https://github.com/mmatyas/pegasus-frontend).

It stays close to upstream. The intentional delta is:

1. **Chiron branding** (name, config paths, desktop id, logo) for GPL §7
2. **Linux GOG import** that also finds Heroic / Dark1HGL installs (including Proton / Windows titles) and launches them through Heroic so Proton works

Native `~/GOG Games` + `start.sh` scanning is unchanged. Windows registry GOG support remains in-tree but is **not** a supported Chiron release target.

## Supported release

| Platform | Status |
|---|---|
| Linux x86_64 AppImage | **Supported** |
| Windows / macOS / Android / Flatpak / deb / rpm | Not supported / not shipped |

Upstream multi-platform code may still exist in the tree; do not expect builds or docs for those targets from this fork.

## GOG on Linux

Chiron’s GOG provider will:

- Keep scanning `~/GOG Games` (and `providers.gog.directories`) for native `start.sh` games
- Also read Heroic / Dark1HGL `gog_store/installed.json` (any `install_path`, including other drives)
- Launch Heroic-managed titles with  
  `heroic --no-gui heroic://launch/gog/<appName>`  
  (or Flatpak / a custom `providers.gog.launch_helper`)

Optional settings (same style as other Pegasus providers):

```
providers.gog.directories: /path/to/extra/gog/root
providers.gog.launch_helper: /path/to/Dark1HGL.AppImage
```

GOG store metadata (art, description, genres) still uses the public GOG product APIs, keyed by GOG ID — same as upstream.

## Building

Build dependencies match upstream Pegasus (Qt 5.15+, QML, Multimedia, SVG, SQL/SQLite, SDL2 or Qt Gamepad). See the [upstream build guide](https://pegasus-frontend.org/docs/dev/build) for details.

```sh
git submodule update --init
mkdir build && cd build
cmake ..   # or: qmake ..
cmake --build .
```

Config/cache directories use the `chiron` application name (for example `~/.config/chiron`).

## License

GPLv3, same as Pegasus Frontend. See [LICENSE.md](LICENSE.md).

This project is not affiliated with Pegasus Frontend or GOG. “Pegasus” and related marks belong to their respective owners; this fork is renamed under GPLv3 §7.
