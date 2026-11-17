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
- Characters must exist in Windows-1252 (the game's character set): accented Latin letters are fine.
- A release reads the files from its `lang` folder, so you can try a change there before sending it.

`template.pot` lists every text. It is written by the program: `cod_bro --lang-template lang/template.pot`.
After a new text is added, the test `ui_text_test` lists the files that miss it.
