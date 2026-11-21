# Translations of our own menu texts

The game's own text comes from your disc, in the language the game picked. The texts our engine adds (the
settings pages under Options, Settings in the pause menu, Quit Game, Press ENTER or START) follow that same
language, using the file here for it:

| Game language | File    |
|---------------|---------|
| German        | `de.po` |
| French        | `fr.po` |
| Spanish       | `es.po` |
| Italian       | `it.po` |
| Polish (text language) | `pl.po` |
| Russian (text language) | `ru.po` |
| Ukrainian (text language) | `uk.po` |
| Portuguese, Brazil (text language) | `pt_BR.po` |

A text without a translation stays English. The first versions of these files were drafted by an AI (Claude);
corrections from native speakers are welcome as pull requests.

## Correcting or adding a translation

The files are gettext `.po` files (UTF-8), which any text editor or [Poedit](https://poedit.net/) can open. Each
entry pairs the English text with its translation:

```
msgid "Field of view"
msgstr "Campo de visión"
```

- Change only the `msgstr` lines. An empty `msgstr ""` (or an entry marked `#, fuzzy`) shows the English text.
- Keep page titles in capitals (`"GRAPHICS"`), as the game writes its own.
- Keep translations short: a row's name has room for 43 characters, a value for 35, and the menus are narrow.
- Characters must exist in Windows-1252 (the game's character set): accented Latin letters are fine. Polish files
  may use the Polish letters, Russian and Ukrainian files Cyrillic (Windows-1251: no other scripts or symbols).
- A release reads the files from its `lang` folder, so you can try a change there before sending it.

`template.pot` lists every text. It is written by the program: `cod_bro --lang-template lang/template.pot`.
After a new text is added, the test `ui_text_test` lists the files that miss it.

## Polish, Russian, Ukrainian and Portuguese: the game's own texts too

The Text language setting (launcher, or Graphics > Screen) set to Polish, Russian, Ukrainian or Portuguese (Brazil) shows every text on
screen in that language - menus, hints, objectives and subtitles - while the game itself (its voices) stays in the
disc's language. `game/pl.po`, `game/ru.po`, `game/uk.po` and `game/pt_BR.po` hold the game's texts (2628 each), `pl.po`,
`ru.po`, `uk.po` and `pt_BR.po` ours. Portuguese needs no new letters; for the others the game's fonts get them drawn in (Sharper text is switched on with it); Cyrillic
letters the replacement fonts lack come from Oswald and Montserrat (`src/text/fonts/README.md`).

The first versions of all of them were drafted by an AI (Claude) and have not been checked by native speakers yet:
corrections are very welcome. In `game/*.po` each entry is keyed by `msgctxt` (the game's string key); `msgid` is
the English text for reference. Keep `[TOKENS]`, `~tokens`, `%s`, the subtitle timings at the start ("12.34|15.60|"
or "5|") and the `|` line breaks; a `|` may be added to split a long subtitle line in two. Russian mission objectives
use the infinitive ("Уничтожить ..."), Ukrainian ones the imperative ("Знищіть ...").

To fix a line for yourself without editing the release's file, copy the entry into `text_overrides.po` in your
data folder (`%APPDATA%\cod_bro` or `~/.local/share/cod_bro`) and change its `msgstr` there; it wins over
`game/<language>.po` (this works in any language). `cod_bro --game-text-template FILE` writes every text a session
loads with its key, to find one.
