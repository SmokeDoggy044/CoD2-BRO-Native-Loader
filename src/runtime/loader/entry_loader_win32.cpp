// cod_bro_loader's entry on Windows: cod_bro_loader.exe (win32_stub.c) calls it in cod_bro_loader_core.dll.
#include "os.h"

extern "C" __declspec(dllexport) void cod_bro_loader_main() { loader::os::run_main(loader_main); }
