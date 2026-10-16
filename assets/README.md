# Game files go here

Copy your game dump into this folder: the complete contents of your Call of Duty 2:
Big Red One (Xbox) disc. `default.xbe` and the `media` folder must be directly in
this folder, not in a subfolder:

    assets/
        default.xbe
        media/
        movies/
        sp/
        textures/
        ... (the other files and folders from the disc)

Then start `cod_bro_launcher`. It shows "Game files found." when everything is in
place.

Your game files can also stay somewhere else: in the launcher, choose "Choose
another folder..." and pick the folder that contains `default.xbe`.

The game only reads these files and never changes them. Your saves and settings go
to your user folder instead:

- Linux: `~/.local/share/cod_bro`
- Windows: `%APPDATA%\cod_bro`
