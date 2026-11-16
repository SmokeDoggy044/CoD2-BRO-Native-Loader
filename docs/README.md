# Documentation

## For developers

- [How it works](developers/how-it-works.md): how the original game code runs on a PC,
  and how the rewritten engine takes over from it.
- [Development](developers/development.md): building on Linux and Windows, the build
  presets, release packaging and the program's options.
- [The comparison harness](developers/comparison-harness.md): how a rewritten function
  is checked against the original inside the running game.
- [Tools we use](developers/tools.md): the analysis, decompiling and testing tools.
- [Playtesting](developers/playtesting.md): diagnostic runs and how problems get marked.
- [Rendering](developers/rendering.md) and [our own renderer](developers/own-renderer.md):
  the graphics path, from the Xbox's Direct3D 8 to our graphics interface.
- [Multiplayer](developers/multiplayer.md): System Link on a virtual network.
- [The engine host](developers/own-engine-host.md): our own executable, frame pacing, the Vulkan renderer, the
  lighting and sound-propagation features and the in-game menus, with how each was checked.
- [Texture packs](developers/texture-packs.md): the pack format, the keys from the player's own textures and the
  launcher's install.
- [Game files and disc images](developers/game-files.md): the edition check and the disc image import.

## File formats and interfaces

- [The level boundary](formats/level_boundary.md): everything the original mission
  scripts need from the engine ([data](formats/level_boundary.csv)).
- [NAL animation](formats/nal_animation.md): the animation codec's channel classes and
  stream formats.

## Research

- [Naming](research/naming.md): how functions get their names, and what has been found.
- [Renderer inventory](research/renderer-inventory.md): what the decompiled renderer
  asked of the Xbox graphics libraries.
- [Component status](research/status.md): what each part does today and how it was checked.
- Class lists per module (`research/*_classes.csv`): how each function was decompiled
  and tested.
