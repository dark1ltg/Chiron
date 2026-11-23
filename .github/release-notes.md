## Chiron 0.2.3

Chiron now prefers your **Dark1HGL AppImage** over a system-installed Heroic package, so GOG library and launch stay on the same helper.

### What’s new

- Looks for Dark1HGL AppImages first (including names like `Heroic-2.22.5-gog.7-linux-x86_64.AppImage`)
- Prefers Dark1HGL config folders when reading installed GOG games
- Falls back to system Heroic / Flatpak only if no Dark1HGL AppImage is found

Put the AppImage in a common spot (`~/Applications`, `~/AppImages`, `~/Downloads`, …) or set:

```
providers.gog.launch_helper: /path/to/Heroic-2.22.5-gog.7-linux-x86_64.AppImage
```

### Download

`Chiron-0.2.3-x86_64.AppImage`

```sh
chmod +x Chiron-0.2.3-x86_64.AppImage
./Chiron-0.2.3-x86_64.AppImage
```
