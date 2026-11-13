// cod_bro's entry on Windows: cod_bro.exe (win32_stub.c) calls it in cod_bro_core.dll, which has no kernel.
#include "os.h"

extern "C" __declspec(dllexport) void cod_bro_host_main() { loader::os::run_main(host_main); }
