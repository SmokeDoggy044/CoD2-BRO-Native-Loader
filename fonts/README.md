# Fonts for sharper text

Free stand-ins for the game's bitmap fonts, used by the "Sharper text" option
(`--sharp-text`, `text/sharp_text.h`): each of the game's font atlases is drawn again
from one of these at the render resolution, into the game's own glyph boxes. All are
under the SIL Open Font License 1.1 (texts in `licenses/`); none is modified.

| Game font | Stand-in | Source |
|---|---|---|
| `garamond` (Garamond Bold) | EB Garamond Bold (`EBGaramond-Bold.ttf`) | github.com/octaviopardo/EBGaramond12 at 106a4a6, `fonts/ttf/` |
| `GemFontOne` (Futura-like) | Jost Medium (`Jost-500-Medium.ttf`) | github.com/indestructible-type/Jost at 35f141c, `fonts/ttf/` |
| `arialblack_14` (Times-like bold) | Liberation Serif Bold 2.1.5 (`LiberationSerif-Bold.ttf`) | github.com/liberationfonts/liberation-fonts (Fedora's liberation-serif-fonts 2.1.5) |
| `impact` (Impact) | Anton (`Anton-Regular.ttf`) | github.com/google/fonts at 47d54ca, `ofl/anton/` |

The game's button icon fonts (`i_button_icons_xb_*`) stay as they are.
