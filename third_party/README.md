# Third-party reference headers

`xbox-includes/` is the git submodule declared in `.gitmodules`
(github.com/mborgerson/xbox-includes). It is **not compiled into any build**;
only `tools/gen_kernel_exports.py` reads its `xboxkrnl.h` to regenerate
`xbox/kernel_exports.inc`. If you cloned without `--recursive`, run:

    git submodule update --init third_party/xbox-includes
