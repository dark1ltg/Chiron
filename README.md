# Chiron

**Chiron is a Linux game library frontend.** One AppImage to browse your collection and launch emulators, native games, and GOG titles—including Windows/Proton installs managed by Heroic or Dark1HGL.

It is a focused fork of [Pegasus Frontend](https://github.com/mmatyas/pegasus-frontend): same theming model and providers, shipped only as a **Linux x86_64 AppImage**, with Linux GOG discovery fixed for Heroic-style installs.

## What you get

- Browse and launch your game library from a couch-friendly UI (Pegasus themes work)
- Emulator and multi-source library support inherited from Pegasus
- **GOG on Linux that actually finds Heroic / Dark1HGL games** (any install path / drive) and launches them through Heroic so Proton works
- Native `~/GOG Games` + `start.sh` installs still work
- Single downloadable AppImage — no Flatpak/deb/rpm/Windows/macOS release

## Who it’s for

Linux players who already use Heroic or Dark1HGL for GOG (especially Proton/Windows builds) and want a Pegasus-style frontend without fighting missing GOG imports.

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

## Releases (Linux x86_64 AppImage)

The supported release artifact is a single AppImage:

```sh
chmod +x Chiron-*-x86_64.AppImage
./Chiron-*-x86_64.AppImage
```

Build one locally (downloads upstream’s static Qt 5.15.12 + SDL2 toolchains into `/opt`):

```sh
./scripts/build-appimage.sh
# → dist/Chiron-<version>-x86_64.AppImage
```

Optional: `VERSION=0.1.0 ./scripts/build-appimage.sh`

GitHub Actions workflow [`.github/workflows/release-appimage.yml`](.github/workflows/release-appimage.yml) builds the AppImage on tag push (`v*` / `chiron-*`) or `workflow_dispatch`, and attaches it to a GitHub Release for tags.

## Building from source

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
