## Chiron 0.2.4

Games that show up in both Dark1HGL and Chiron should launch again. Pressing Launch now hands off to Dark1HGL (no GUI) with your existing Proton prefixes — Chiron is just the frontend.

### What’s fixed

- Nested AppImage launch: Dark1HGL AppImages start with `--appimage-extract-and-run` so they don’t silently fail under Chiron’s AppImage
- Headless handoff: launch URL includes `?gui=false` and `--no-gui` so Dark1HGL stays in the background
- Working directory is your home folder (not the game install tree), so Dark1HGL/Proton find their usual config and prefixes
- Child processes no longer inherit Chiron’s AppImage library paths (`LD_LIBRARY_PATH`, `APPDIR`, …)

### Download

`Chiron-0.2.4-x86_64.AppImage`

```sh
chmod +x Chiron-0.2.4-x86_64.AppImage
./Chiron-0.2.4-x86_64.AppImage
```
