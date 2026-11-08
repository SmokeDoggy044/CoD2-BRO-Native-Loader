"""Where the rewritten engine code lives: src/engine/<module>/, one folder per module.

The sources include each other's headers by file name (every module folder is on the
include path), so a file name is unique across the folders. Tools find a file by name
with path(), list files with files(), and put a new file in the folder its name says
(the same name rules as the existing files: bro_scr_ -> levelscript, cg_ -> cgame, ...).
"""
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parent.parent
ENGINE = ROOT / "src" / "engine"
FUNCTIONS_CPP = ENGINE / "shared" / "functions.cpp"
CMAKE = ENGINE / "CMakeLists.txt"

# A new file's module folder by its name; first match wins.
FOLDERS = [
    ("levelscript", r"bro_scr_"),
    ("levels", r"levels_"),
    ("script", r"(g_scr_builtins|scr_|script_string)"),
    ("cgame", r"(cg_|cgame)"),
    ("client", r"(cl[._]|fe[._]|movie|ui_)"),
    ("collision", r"cm[._]"),
    ("renderer", r"(dobj|fx[._]|ngl|r_|shaders|tr_)"),
    ("xanim", r"(anim_script|nal_|xanim)"),
    ("qcommon", r"(qcommon|param_set)"),
    ("sound", r"snd"),
    ("streaming", r"stream"),
    ("server", r"sv[._]"),
    ("zlib", r"(zlib|unzip)"),
    ("game", r"(actor|bg_|g_|phys|pmove|ragdoll)"),
    ("shared", r""),
]


def engine(root=None):
    """src/engine of this checkout, or of another worktree (root)."""
    return pathlib.Path(root) / "src" / "engine" if root else ENGINE


def files(pattern="*", root=None):
    """Every engine source file matching pattern (a glob on the file name), sorted by name."""
    return sorted((p for p in engine(root).rglob(pattern) if p.is_file() and p.name != "CMakeLists.txt"),
                  key=lambda p: p.name)


def folder_for(name):
    return next(folder for folder, rx in FOLDERS if re.match(rx, name))


def path(name, root=None):
    """The file called name: where it is, or where a new one goes (its folder is created)."""
    found = [p for p in engine(root).rglob(name) if p.is_file()]
    if found:
        return found[0]
    new = engine(root) / folder_for(name) / name
    new.parent.mkdir(exist_ok=True)
    return new


def cmake_entry(name, root=None):
    """How a source file is listed in src/engine/CMakeLists.txt (relative to it)."""
    return path(name, root).relative_to(engine(root)).as_posix()
