// cod_bro_loader's process entry on Linux (Windows: the stub calls the core DLL's cod_bro_loader_main).
#include "os.h"

int main(int argc, char** argv) { return loader_main(argc, argv); }
