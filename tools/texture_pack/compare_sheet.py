#!/usr/bin/env python3
"""Model bake-off sheet (UI textures, 2026-11-14): the original and each model's result side by side; the user picks
a model per texture (or skip), then `merge` builds one mod from the picks.

usage:
  compare_sheet.py sheet OUT --variant NAME=WORK [--variant ...] [--size 320] [--serve PORT]
  compare_sheet.py merge OUT PICKS.csv --variant NAME=WORK [...] [--name N] [--version V] [--zip]

sheet: OUT/index.html + OUT/img (WebP previews: original nearest-enlarged, each variant's pack picture, and a 1:1
  centre crop per variant for the zoom). Every WORK is a build.py work dir built with the same --source/--classes and
  a different --model. Picks stay in the browser (localStorage); "Export picks.csv" saves content_sha1,choice
  (choice = a variant name or 'skip'). Serve it (--serve) for Flatpak browsers (see sheet.py).
merge: OUT/mod = mod.ini + textures/<key>.dds copied from the picked variant's WORK/mod/textures; unpicked textures
  are left out (the original shows in game).
"""
import argparse
import csv
import functools
import json
import os
import shutil
import sys
import zipfile
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[3]


def variants(specs):
    out = []
    for spec in specs:
        name, _, work = spec.partition("=")
        out.append((name, Path(work)))
    return out


def rows_of(work):
    with open(work / "select.csv", newline="") as f:
        return [s for s in csv.DictReader(f) if not s["skip"]]


def previews(item):
    sha1, original, packs, out, size = item
    o = Image.open(original).convert("RGBA")
    f = size / max(o.size)
    box = (max(1, round(o.width * f)), max(1, round(o.height * f)))
    o.resize(box, Image.NEAREST).save(out / f"{sha1}_o.webp", quality=88)
    for name, pack in packs:
        u = Image.open(pack).convert("RGBA")
        u.resize(box, Image.LANCZOS).save(out / f"{sha1}_{name}.webp", quality=88)
        cw, ch = min(u.width, 2 * size), min(u.height, 2 * size)
        x, y = (u.width - cw) // 2, (u.height - ch) // 2
        u.crop((x, y, x + cw, y + ch)).save(out / f"{sha1}_{name}_z.webp", quality=90)
    return sha1


PAGE = """<!doctype html><html><head><meta charset="utf-8"><title>UI model bake-off</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>
:root{--bg:#141414;--fg:#e8e2dc;--mut:#9a918a;--card:#1f1d1c;--line:#38332f;--acc:#c0392b}
@media (prefers-color-scheme: light){:root:not([data-theme="dark"]){--bg:#f6f3f0;--fg:#1d1a18;--mut:#6b625b;
--card:#fff;--line:#ddd5ce}}
body{margin:0;background:var(--bg);color:var(--fg);font:14px/1.4 system-ui,sans-serif}
header{position:sticky;top:0;z-index:2;background:var(--bg);border-bottom:1px solid var(--line);padding:10px 16px;
display:flex;flex-wrap:wrap;gap:8px;align-items:center}
header h1{font-size:16px;margin:0 12px 0 0}
input,select,button{font:inherit;color:var(--fg);background:var(--card);border:1px solid var(--line);border-radius:6px;padding:5px 8px}
button{cursor:pointer}button.primary{background:var(--acc);border-color:var(--acc);color:#fff}
#count,#tally{color:var(--mut)}
main{padding:16px;display:flex;flex-direction:column;gap:12px}
.card{background:var(--card);border:1px solid var(--line);border-radius:8px;padding:10px}
.name{font-weight:600;word-break:break-all}.meta{color:var(--mut);font-size:12px;margin-bottom:6px}
.pics{display:grid;grid-template-columns:repeat(auto-fill,minmax(min(100%,200px),1fr));gap:8px}
.pics figure{margin:0;border:2px solid transparent;border-radius:6px;padding:3px}
.pics figure.picked{border-color:var(--acc)}
.pics img{width:100%;height:auto;display:block;background:repeating-conic-gradient(#8884 0 25%,#0000 0 50%) 0 0/16px 16px;cursor:zoom-in}
.pics figcaption{display:flex;justify-content:space-between;align-items:center;gap:4px;font-size:12px;margin-top:4px}
.pics figcaption button{padding:2px 8px;font-size:12px}
.pics figcaption button.on{background:var(--acc);border-color:var(--acc);color:#fff}
#zoom{position:fixed;inset:0;background:#000e;display:none;flex-direction:column;align-items:center;justify-content:center;z-index:5}
#zoom img{max-width:96vw;max-height:82vh}#zoom p{color:#ddd;margin:8px}
#zoom nav{display:flex;gap:6px;flex-wrap:wrap;justify-content:center}
</style></head><body>
<header><h1>UI model bake-off</h1>
<input id="q" placeholder="filter name" size="16">
<select id="state"><option value="">all</option><option value="open">not picked yet</option></select>
<span id="count"></span><span id="tally"></span><span style="flex:1"></span>
<button class="primary" id="export">Export picks.csv</button></header>
<main id="grid"></main>
<div id="zoom"><img alt=""><p></p><nav></nav></div>
<script>
const ROWS = __ROWS__, VARIANTS = __VARIANTS__, KEY = "ui-bakeoff-" + __TAG__;
let picks = {};
try { picks = JSON.parse(localStorage.getItem(KEY) || "{}"); } catch (e) {}
const save = () => { try { localStorage.setItem(KEY, JSON.stringify(picks)); } catch (e) {} };
const zoom = document.getElementById("zoom");
zoom.querySelector("img").onclick = () => zoom.style.display = "none";
function show(r, v) {
  const views = [["original", `img/${r.sha1}_o.webp`], ...VARIANTS.map(n => [n, `img/${r.sha1}_${n}_z.webp`])];
  const nav = zoom.querySelector("nav"); nav.replaceChildren();
  const go = ([n, src]) => { zoom.querySelector("img").src = src; zoom.querySelector("p").textContent =
    `${r.name}: ${n}${n === "original" ? " (enlarged)" : " (1:1 centre)"} - click the picture to close`; };
  for (const view of views) { const b = document.createElement("button"); b.textContent = view[0]; b.onclick = () => go(view); nav.append(b); }
  go(views.find(x => x[0] === v) || views[0]); zoom.style.display = "flex";
}
function card(r) {
  const c = document.createElement("div"); c.className = "card";
  c.innerHTML = `<div class="name"></div><div class="meta"></div><div class="pics"></div>`;
  c.querySelector(".name").textContent = r.name;
  c.querySelector(".meta").textContent = `${r.folder} · ${r.w}x${r.h} · alpha ${r.alpha}`;
  const pics = c.querySelector(".pics");
  for (const v of ["original", ...VARIANTS]) {
    const f = document.createElement("figure");
    const choice = v === "original" ? "skip" : v;
    if (picks[r.sha1] === choice) f.className = "picked";
    f.innerHTML = `<img loading="lazy" alt=""><figcaption><span></span><button></button></figcaption>`;
    f.querySelector("img").src = v === "original" ? `img/${r.sha1}_o.webp` : `img/${r.sha1}_${v}.webp`;
    f.querySelector("img").onclick = () => show(r, v);
    f.querySelector("span").textContent = v;
    const b = f.querySelector("button");
    b.textContent = v === "original" ? "keep original" : "pick";
    if (picks[r.sha1] === choice) b.className = "on";
    b.onclick = () => { picks[r.sha1] = choice; save(); c.replaceWith(card(r)); tally(); };
    pics.append(f);
  }
  return c;
}
function tally() {
  const t = {}; for (const v of Object.values(picks)) t[v] = (t[v] || 0) + 1;
  document.getElementById("tally").textContent = "picked: " + (Object.entries(t).map(([k, n]) => `${k} ${n}`).join(", ") || "none");
}
function render() {
  const q = document.getElementById("q").value.toLowerCase(), st = document.getElementById("state").value;
  const rows = ROWS.filter(r => (!q || r.name.toLowerCase().includes(q)) && (st !== "open" || !picks[r.sha1]));
  document.getElementById("count").textContent = `${rows.length} of ${ROWS.length}`;
  document.getElementById("grid").replaceChildren(...rows.map(card)); tally();
}
document.getElementById("q").oninput = render; document.getElementById("state").oninput = render;
document.getElementById("export").onclick = () => {
  const lines = ["content_sha1,choice", ...Object.entries(picks).map(([k, v]) => `${k},${v}`)];
  const a = document.createElement("a");
  a.href = URL.createObjectURL(new Blob([lines.join("\\n") + "\\n"], {type: "text/csv"})); a.download = "picks.csv"; a.click();
};
render();
</script></body></html>
"""


def sheet(args, vs):
    by_sha1 = {}
    with open(args.manifest, newline="") as f:
        for r in csv.DictReader(f):
            by_sha1.setdefault(r["content_sha1"], r)
    base = {s["content_sha1"]: s for s in rows_of(vs[0][1])}
    img = args.out / "img"
    img.mkdir(parents=True, exist_ok=True)
    items, data = [], []
    for sha1, s in sorted(base.items(), key=lambda kv: kv[1]["asset_path"].lower()):
        packs = [(n, w / "mod" / "textures" / f"{s['key']}.dds") for n, w in vs]
        if not all(p.exists() for _, p in packs):
            continue
        items.append((sha1, args.manifest.parent / (by_sha1[sha1]["output"] + ".png"), packs, img, args.size))
        path = s["asset_path"].replace("\\", "/")
        data.append({"sha1": sha1, "name": path.rsplit("/", 1)[-1], "folder": path.rsplit("/", 1)[0].split("assets/")[-1],
                     "w": int(s["width"]), "h": int(s["height"]), "alpha": s["alpha"]})
    with ProcessPoolExecutor(args.jobs) as pool:
        list(pool.map(previews, items, chunksize=2))
    page = PAGE.replace("__ROWS__", json.dumps(data)).replace("__VARIANTS__", json.dumps([n for n, _ in vs]))
    page = page.replace("__TAG__", json.dumps(args.out.resolve().name))
    (args.out / "index.html").write_text(page)
    print(f"sheet: {args.out / 'index.html'} ({len(data)} textures x {len(vs)} variants)", flush=True)
    if args.serve:
        from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
        handler = functools.partial(SimpleHTTPRequestHandler, directory=str(args.out))
        handler.log_message = lambda *a: None
        print(f"serving http://127.0.0.1:{args.serve}/ (Ctrl+C stops)", flush=True)
        ThreadingHTTPServer(("127.0.0.1", args.serve), handler).serve_forever()


def merge(args, vs):
    works = dict(vs)
    keys = {}
    for _, w in vs:
        keys.update({s["content_sha1"]: s["key"] for s in rows_of(w)})
    mod = args.out / "mod"
    if mod.exists():
        shutil.rmtree(mod)
    (mod / "textures").mkdir(parents=True)
    used, kept = {}, 0
    with open(args.picks, newline="") as f:
        for r in csv.DictReader(f):
            if r["choice"] == "skip":
                kept += 1
                continue
            if r["choice"] not in works:
                sys.exit(f"unknown variant {r['choice']} in {args.picks}")
            key = keys[r["content_sha1"]]
            shutil.copy2(works[r["choice"]] / "mod" / "textures" / f"{key}.dds", mod / "textures" / f"{key}.dds")
            used[r["choice"]] = used.get(r["choice"], 0) + 1
    (mod / "mod.ini").write_text(f"name={args.name}\nversion={args.version}\nauthor=cod_bro texture pack builder\n"
                                 f"kind=textures\nsource=UI textures, per-texture model picks: "
                                 f"{', '.join(f'{k} {n}' for k, n in sorted(used.items()))}\n")
    print(f"merge: {mod} ({sum(used.values())} textures: {used}; {kept} kept original)")
    if args.zip:
        target = args.out / f"{args.name.replace(' ', '_')}-{args.version}.zip"
        with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED) as z:
            for p in sorted(mod.rglob("*")):
                if p.is_file():
                    z.write(p, Path(args.name.replace(" ", "_")) / p.relative_to(mod))
        print(f"zip: {target}")


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("mode", choices=["sheet", "merge"])
    p.add_argument("out", type=Path)
    p.add_argument("picks", type=Path, nargs="?")
    p.add_argument("--variant", action="append", required=True, help="NAME=WORK")
    p.add_argument("--manifest", type=Path, default=ROOT / "textures/original/manifest.csv")
    p.add_argument("--size", type=int, default=320)
    p.add_argument("--jobs", type=int, default=os.cpu_count())
    p.add_argument("--serve", type=int, default=0)
    p.add_argument("--name", default="UI textures")
    p.add_argument("--version", default="0.1")
    p.add_argument("--zip", action="store_true")
    args = p.parse_args()
    vs = variants(args.variant)
    if args.mode == "merge" and not args.picks:
        sys.exit("merge needs PICKS.csv")
    (sheet if args.mode == "sheet" else merge)(args, vs)
    return 0


if __name__ == "__main__":
    sys.exit(main())
