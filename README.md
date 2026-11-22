# Chiron

**Chiron is a Linux game library frontend.** Browse your collection and launch emulators, native games, and GOG titles—including Windows/Proton installs from Heroic or Dark1HGL—from one AppImage.

[Download](https://github.com/dark1ltg/Chiron/releases/latest) · [Changelog](CHANGELOG.md)

A focused GPLv3 fork of [Pegasus Frontend](https://github.com/mmatyas/pegasus-frontend). Themes stay compatible; Chiron ships **Linux x86_64 AppImage only**, with GOG discovery fixed for Heroic-style installs.

## Screenshots

![Library grid](etc/promo/screenshot-library.png)

![Game details](etc/promo/screenshot-details.png)

![Collection bar](etc/promo/screenshot-collections.png)

## Highlights

- Couch-friendly library UI (Pegasus-compatible themes)
- Emulators and multi-source libraries
- GOG via Heroic / Dark1HGL (any install path, including Proton) plus native `~/GOG Games`
- Config under `~/.config/chiron`

## Install

```sh
chmod +x Chiron-*-x86_64.AppImage
./Chiron-*-x86_64.AppImage
```

Grab the AppImage from [Releases](https://github.com/dark1ltg/Chiron/releases/latest).

## GOG

Reads `~/GOG Games` and Heroic/Dark1HGL `gog_store/installed.json`, then launches with `heroic://launch/gog/<appName>` (PATH, desktop entry, common AppImage dirs, or Flatpak).

```
providers.gog.directories: /path/to/extra/gog/root
providers.gog.launch_helper: /path/to/Dark1HGL.AppImage
```

## Build

```sh
VERSION=0.2.0 ./scripts/build-appimage.sh   # → dist/Chiron-*-x86_64.AppImage
```

From source: `git submodule update --init && cmake -B build && cmake --build build` (Qt 5.15+, SDL2). CI builds on tag push / `workflow_dispatch` via [`.github/workflows/release-appimage.yml`](.github/workflows/release-appimage.yml).

## License

GPLv3 — see [LICENSE.md](LICENSE.md). Not affiliated with Pegasus Frontend or GOG.
