## Chiron 0.2.1

This update makes GOG games more reliable when you run Chiron as an AppImage on Linux—especially with Heroic or Dark1HGL.

### What’s better

- Games from your normal GOG folder start more cleanly (the AppImage no longer leaks its own libraries into the game)
- Titles on an unmounted drive still show up as separate games instead of collapsing into one entry
- Flatpak Heroic and native / Dark1HGL each use the right launcher for their installs
- Missing game folders no longer break launch with a bad working directory
- If the same game exists both as a native Linux install and in Heroic, Chiron keeps the one that actually works (and prefers the native Linux launcher when both are fine)
- A custom launcher path you set is checked before Chiron tries to use it
- When several Heroic-like AppImages are found, Dark1HGL / newer ones are preferred
- Game info and launch links behave better for unusual GOG IDs and names

### Download

`Chiron-0.2.1-x86_64.AppImage`

Make it executable, then run it:

```sh
chmod +x Chiron-0.2.1-x86_64.AppImage
./Chiron-0.2.1-x86_64.AppImage
```
