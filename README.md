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

For Dark1HGL/Heroic AppImages off PATH, set `providers.gog.launch_helper` to the binary. Details and release notes: [CHANGELOG.md](CHANGELOG.md).

## License

GPLv3 — see [LICENSE.md](LICENSE.md). Not affiliated with Pegasus Frontend or GOG.
