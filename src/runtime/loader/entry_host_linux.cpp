// cod_bro's process entry on Linux (Windows: the stub calls the core DLL's cod_bro_host_main).
#include "os.h"

int main(int argc, char** argv) { return host_main(argc, argv); }
