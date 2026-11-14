#!/usr/bin/env python3
"""Phase 1.0c curation sheet: original vs upscaled per texture as a local HTML page for the user.

usage: tools/texture_pack/sheet.py WORK [--size 384] [--source REGEX] [--jobs N]

Writes WORK/sheet/index.html plus WebP previews (WORK/sheet/img/<sha1>_o.webp original enlarged with nearest
neighbour, _u.webp the pack picture as encoded, both --size pixels on the long side, and _uz.webp a 1:1 crop of the
pack picture's centre for zooming). Per texture: upscale / conservative / skip buttons and an optional mask note; the
choices stay in the browser (localStorage) and "Export curation.csv" saves a file for build.py --curation (masks are
typed as 'x0 y0 x1 y1;...' in original texels). Filter by folder, alpha kind or text; sort by size or change.

The page is for the user's own browser (file://) - it holds the game's textures, so it is never published, and the
assistant does not open the pictures.
"""
import argparse
import csv
import html
import json
import os
import re
import sys
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[3]


def previews(item):
    s, original, pack, out, size = item
    o = Image.open(original).convert("RGBA")
    u = Image.open(pack).convert("RGBA")
    f = size / max(o.size)
    box = (max(1, round(o.width * f)), max(1, round(o.height * f)))
    o.resize(box, Image.NEAREST).save(out / f"{s['content_sha1']}_o.webp", quality=88)
    u.resize(box, Image.LANCZOS).save(out / f"{s['content_sha1']}_u.webp", quality=88)
    cw, ch = min(u.width, size), min(u.height, size)
    x, y = (u.width - cw) // 2, (u.height - ch) // 2
    u.crop((x, y, x + cw, y + ch)).save(out / f"{s['content_sha1']}_uz.webp", quality=90)
    # how much the picture changed: shrunk back vs original (alpha-weighted PSNR)
    a = np.stack([np.asarray(c.resize(o.size, Image.BOX), np.float32) for c in u.split()], 2)
    b = np.asarray(o, np.float32)
    w = b[..., 3:4] / 255 + 1e-6
    mse = ((a[..., :3] - b[..., :3]) ** 2 * w).sum() / w.sum() / 3
    return s["content_sha1"], round(float(10 * np.log10(255 ** 2 / max(mse, 1e-9))), 1)


PAGE = """<!doctype html><html><head><meta charset="utf-8"><title>Texture curation</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>
:root{--bg:#141414;--fg:#e8e2dc;--mut:#9a918a;--card:#1f1d1c;--line:#38332f;--acc:#c0392b;--ok:#3d8b5a;--warn:#c98a2b}
@media (prefers-color-scheme: light){:root:not([data-theme="dark"]){--bg:#f6f3f0;--fg:#1d1a18;--mut:#6b625b;
--card:#fff;--line:#ddd5ce}}
body{margin:0;background:var(--bg);color:var(--fg);font:14px/1.4 system-ui,sans-serif}
header{position:sticky;top:0;z-index:2;background:var(--bg);border-bottom:1px solid var(--line);padding:10px 16px;
display:flex;flex-wrap:wrap;gap:8px;align-items:center}
header h1{font-size:16px;margin:0 12px 0 0}
input,select,button{font:inherit;color:var(--fg);background:var(--card);border:1px solid var(--line);border-radius:6px;
padding:5px 8px}
button{cursor:pointer}button.primary{background:var(--acc);border-color:var(--acc);color:#fff}
#count{color:var(--mut)}
main{display:grid;grid-template-columns:repeat(auto-fill,minmax(min(100%,420px),1fr));gap:12px;padding:16px}
.card{background:var(--card);border:1px solid var(--line);border-radius:8px;padding:10px;display:flex;flex-direction:column;gap:8px}
.card.skip{opacity:.55}.card.conservative{border-color:var(--warn)}.card.skip{border-color:var(--acc)}
.name{font-weight:600;word-break:break-all}.meta{color:var(--mut);font-size:12px}
.pics{display:grid;grid-template-columns:1fr 1fr;gap:6px}
.pics figure{margin:0}.pics img{width:100%;height:auto;display:block;background:
repeating-conic-gradient(#8884 0 25%,#0000 0 50%) 0 0/16px 16px;cursor:zoom-in}
.pics figcaption{font-size:11px;color:var(--mut)}
.acts{display:flex;gap:6px;flex-wrap:wrap}.acts button.on{background:var(--fg);color:var(--bg)}
.mask{width:100%;box-sizing:border-box}
#zoom{position:fixed;inset:0;background:#000d;display:none;align-items:center;justify-content:center;z-index:5;cursor:zoom-out}
#zoom img{max-width:96vw;max-height:92vh;image-rendering:auto}
#zoom p{position:absolute;bottom:8px;left:0;right:0;text-align:center;color:#ddd;margin:0}
</style></head><body>
<header><h1>Texture curation</h1>
<input id="q" placeholder="filter name" size="16">
<select id="folder"><option value="">all folders</option></select>
<select id="alpha"><option value="">any alpha</option><option>none</option><option>cutout</option><option>blend</option></select>
<select id="state"><option value="">any choice</option><option>upscale</option><option>conservative</option><option>skip</option>
<option value="changed">changed from default</option></select>
<select id="sort"><option value="path">sort: path</option><option value="psnr">sort: most changed</option>
<option value="size">sort: largest</option></select>
<span id="count"></span><span style="flex:1"></span>
<button class="primary" id="export">Export curation.csv</button></header>
<main id="grid"></main>
<div id="zoom"><img alt=""><p></p></div>
<script>
const ROWS = __ROWS__;
const KEY = "texture-curation-" + __TAG__;
let choices = {};
try { choices = JSON.parse(localStorage.getItem(KEY) || "{}"); } catch (e) {}
const save = () => { try { localStorage.setItem(KEY, JSON.stringify(choices)); } catch (e) {} };
const folders = [...new Set(ROWS.map(r => r.folder))].sort();
const fsel = document.getElementById("folder");
for (const f of folders) fsel.add(new Option(f, f));
const grid = document.getElementById("grid");
const zoom = document.getElementById("zoom");
zoom.onclick = () => zoom.style.display = "none";
function choice(r) { return (choices[r.sha1] || {}).action || r.action; }
function card(r) {
  const c = document.createElement("div");
  const act = choice(r);
  c.className = "card " + act;
  c.innerHTML = `<div class="name"></div><div class="meta"></div><div class="pics">
    <figure><img loading="lazy" src="img/${r.sha1}_o.webp" alt="original"><figcaption>original ${r.w}x${r.h}</figcaption></figure>
    <figure><img loading="lazy" src="img/${r.sha1}_u.webp" alt="upscaled"><figcaption>pack ${r.w*4}x${r.h*4} (click: 1:1 centre)</figcaption></figure>
    </div><div class="acts"></div><input class="mask" placeholder="masks: x0 y0 x1 y1;... (original texels)">`;
  c.querySelector(".name").textContent = r.name;
  c.querySelector(".meta").textContent = `${r.folder} · ${r.format} · alpha ${r.alpha} · PSNR ${r.psnr} dB`;
  const imgs = c.querySelectorAll("img");
  imgs[0].onclick = () => show(`img/${r.sha1}_o.webp`, r.name + " (original, enlarged)");
  imgs[1].onclick = () => show(`img/${r.sha1}_uz.webp`, r.name + " (pack, 1:1 centre)");
  const acts = c.querySelector(".acts");
  for (const a of ["upscale", "conservative", "skip"]) {
    const b = document.createElement("button");
    b.textContent = a; if (a === act) b.className = "on";
    b.onclick = () => { choices[r.sha1] = {...(choices[r.sha1] || {}), action: a}; save(); c.replaceWith(card(r)); };
    acts.append(b);
  }
  const m = c.querySelector(".mask");
  m.value = (choices[r.sha1] || {}).masks ?? r.masks;
  m.onchange = () => { choices[r.sha1] = {...(choices[r.sha1] || {}), masks: m.value.trim()}; save(); };
  return c;
}
function show(src, label) { zoom.querySelector("img").src = src; zoom.querySelector("p").textContent = label; zoom.style.display = "flex"; }
function render() {
  const q = document.getElementById("q").value.toLowerCase(), f = fsel.value;
  const al = document.getElementById("alpha").value, st = document.getElementById("state").value;
  let rows = ROWS.filter(r => (!q || r.name.toLowerCase().includes(q)) && (!f || r.folder === f) &&
    (!al || r.alpha === al) && (!st || (st === "changed" ? choice(r) !== r.action || ((choices[r.sha1]||{}).masks||"") !== r.masks : choice(r) === st)));
  const s = document.getElementById("sort").value;
  if (s === "psnr") rows.sort((a, b) => a.psnr - b.psnr);
  if (s === "size") rows.sort((a, b) => b.w * b.h - a.w * a.h);
  document.getElementById("count").textContent = `${rows.length} of ${ROWS.length}`;
  grid.replaceChildren(...rows.slice(0, 400).map(card));
  if (rows.length > 400) { const p = document.createElement("p"); p.textContent = "First 400 shown - narrow the filter."; grid.append(p); }
}
for (const id of ["q", "folder", "alpha", "state", "sort"]) document.getElementById(id).oninput = render;
document.getElementById("export").onclick = () => {
  const lines = ["asset_path,content_sha1,action,masks"];
  for (const r of ROWS) {
    const ch = choices[r.sha1] || {}, a = ch.action || r.action, m = ch.masks ?? r.masks;
    if (a !== "upscale" || m) lines.push([r.path, r.sha1, a, m].map(v => '"' + String(v).replace(/"/g, '""') + '"').join(","));
  }
  const blob = new Blob([lines.join("\\n") + "\\n"], {type: "text/csv"});
  const link = document.createElement("a"); link.href = URL.createObjectURL(blob); link.download = "curation.csv"; link.click();
};
render();
</script></body></html>
"""


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("work", type=Path)
    p.add_argument("--manifest", type=Path, default=ROOT / "textures/original/manifest.csv")
    p.add_argument("--size", type=int, default=384)
    p.add_argument("--source", default="")
    p.add_argument("--jobs", type=int, default=os.cpu_count())
    args = p.parse_args()
    by_sha1 = {}
    with open(args.manifest, newline="") as f:
        for r in csv.DictReader(f):
            by_sha1.setdefault(r["content_sha1"], r)
    with open(args.work / "select.csv", newline="") as f:
        rows = [s for s in csv.DictReader(f) if not s["skip"] and (not args.source or re.search(args.source, s["source"]))]
    out = args.work / "sheet" / "img"
    out.mkdir(parents=True, exist_ok=True)
    items = []
    for s in rows:
        pack = args.work / "mod" / "textures" / f"{s['key']}.dds"
        if pack.exists():
            items.append((s, args.manifest.parent / (by_sha1[s["content_sha1"]]["output"] + ".png"), pack, out,
                          args.size))
    with ProcessPoolExecutor(args.jobs) as pool:
        psnr = dict(pool.map(previews, items, chunksize=4))
    data = []
    for s, *_ in items:
        path = s["asset_path"].replace("\\", "/")
        name = path.rsplit("/", 1)[-1]
        folder = re.sub(r"^[a-z]:/asset_temp/", "", path.rsplit("/", 1)[0], flags=re.I)
        data.append({"sha1": s["content_sha1"], "path": s["asset_path"], "name": name, "folder": folder,
                     "format": s["format"], "alpha": s["alpha"], "w": int(s["width"]), "h": int(s["height"]),
                     "action": s["action"], "masks": s["masks"], "psnr": psnr[s["content_sha1"]]})
    page = PAGE.replace("__ROWS__", json.dumps(data)).replace("__TAG__", json.dumps(args.work.resolve().name))
    (args.work / "sheet" / "index.html").write_text(page)
    print(f"sheet: {args.work / 'sheet' / 'index.html'} ({len(data)} textures)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
