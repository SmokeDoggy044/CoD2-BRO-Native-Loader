#include <cstring>

#include "kernel.h"

namespace xbox {

namespace {

#define KIND_STDCALL ExportKind::Stdcall
#define KIND_FASTCALL ExportKind::Fastcall
#define KIND_CDECL ExportKind::Cdecl
#define KIND_DATA ExportKind::Data
#define KIND_UNKNOWN ExportKind::Unknown
#define KEXPORT(ordinal, name, kind, stack_bytes) {ordinal, #name, KIND_##kind, stack_bytes},

const KernelExport kExports[] = {
#include "kernel_exports.inc"
};

#undef KEXPORT

}  // namespace

const KernelExport* find_kernel_export(uint32_t ordinal) {
    for (const auto& e : kExports)
        if (e.ordinal == ordinal) return &e;
    return nullptr;
}

const KernelExport* find_kernel_export(const char* name) {
    for (const auto& e : kExports)
        if (std::strcmp(e.name, name) == 0) return &e;
    return nullptr;
}

const char* kernel_export_name(uint32_t ordinal) {
    const KernelExport* e = find_kernel_export(ordinal);
    return e ? e->name : "?";
}

}  // namespace xbox
