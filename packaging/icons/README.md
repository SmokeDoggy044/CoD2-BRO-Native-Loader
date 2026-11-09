# Loader icon

`cod_bro.png` and `cod_bro.ico` are the application icon supplied for this
fork, derived from the *Call of Duty 2: Big Red One* cover art. They are used
only to identify the launcher and loader; all game artwork, logos and
trademarks remain the property of their respective owners. The image is not
game data and is not loaded by the game itself.

- `cod_bro.png`: 256x256 RGBA PNG, installed with the Linux release and used by
  `cod_bro.desktop` / `install-linux-icon.sh`.
- `cod_bro.ico`: multi-resolution Windows icon (16, 24, 32, 48, 64, 128 and
  256 px), embedded into `cod_bro_launcher.exe`, `cod_bro_loader.exe` and
  `cod_bro_loader_core.dll` by `cmake/windows_resources.cmake`.
- `platform/window_icon.cpp` contains a generated 64x64 RGBA copy of the PNG
  so SDL can set the window/taskbar icon on both platforms at run time.

To regenerate them from the original 500x500 PNG:

```sh
convert image-1.png -filter Lanczos -resize 256x256 -strip PNG32:cod_bro.png
convert image-1.png -filter Lanczos \
    -define icon:auto-resize=256,128,64,48,32,24,16 -strip cod_bro.ico
convert cod_bro.png -filter Lanczos -resize 64x64 -alpha on -depth 8 RGBA:icon.rgba
```

The last command produces the raw pixels embedded in `platform/window_icon.cpp`.
