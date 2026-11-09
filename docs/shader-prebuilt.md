# Prebuilt title and post shaders

`render/shaders/prebuilt/*.d3dbc` and `render/post/prebuilt/*.d3dbc` are the
compiled Shader Model 3 bytecode for the HLSL sources next to them. They were
produced with **vkd3d-shader's HLSL compiler** (`vkd3d-compiler -x hlsl -p
ps_3_0|vs_3_0 -b d3dbc`, vkd3d 1.15 for the title shaders and 1.17 for the post
passes, whose `tex2Dlod` needs the newer release).

Shipping the bytecode means a build does **not** need an HLSL compiler at all:
Debian 13's `vkd3d-compiler` 1.2 only understands DXBC input, while the project's
shaders are HLSL text. `render/CMakeLists.txt` copies the prebuilt `.d3dbc` files
into the build directory and embeds them with `cmake/embed_shaders.cmake` and
`cmake/embed_named_shaders.cmake`. If a prebuilt file is missing, CMake falls back
to compiling that HLSL with a `vkd3d-compiler` that supports `-x hlsl`.

To regenerate (needs vkd3d >= 1.15 with the HLSL front end):

```sh
cd render/shaders
for f in *.hlsl; do
    n=${f%.hlsl}
    case $n in vs_*) p=vs_3_0;; *) p=ps_3_0;; esac
    vkd3d-compiler -x hlsl -p "$p" -b d3dbc -o "prebuilt/$n.d3dbc" "$f"
done
cd ../post
for f in *.hlsl; do
    n=${f%.hlsl}
    case $n in post_vs) p=vs_3_0;; *) p=ps_3_0;; esac
    vkd3d-compiler -x hlsl -p "$p" -b d3dbc -o "prebuilt/$n.d3dbc" "$f"
done
```

The `.d3dbc` files are raw SM1-3 token streams (the first four bytes are the
version token, e.g. `00 03 ff ff` for `ps_3_0`); `IDirect3DDevice9::CreatePixelShader`
and `CreateVertexShader` consume them directly. They are data generated from this
repository's own HLSL and are covered by the project's GPL-2.0-or-later license.
