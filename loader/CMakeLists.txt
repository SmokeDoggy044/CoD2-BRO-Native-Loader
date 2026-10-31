# Route-B only: deleted once the game no longer needs the original XBE.
set(loader_sources
    compare_common.cpp
    crash.cpp
    diag_summary.cpp
    hooks.cpp
    kernel_thunks.cpp
    main.cpp
    patches.cpp
    pc_menu.cpp
    profiler.cpp
    seh.cpp
    shader_dump.cpp
    release_map.cpp
    symbols.cpp
    thread_env.cpp
    xbe.cpp
)

if(WIN32)
    # The loader proper is a DLL; cod_bro_loader.exe is a stub linked at the XBE's
    # base address that keeps the Xbox address ranges free and loads it (win32_stub.c).
    # The stub is a GUI program, so starting it from the launcher opens no console;
    # from a console, the loader attaches to it (os_win32.cpp).
    add_library(cod_bro_loader_core SHARED ${loader_sources} compare_win32.cpp crash_win32.cpp os_win32.cpp profiler_win32.cpp thread_env_win32.cpp)
    set_target_properties(cod_bro_loader_core PROPERTIES PREFIX "")
    cod_bro_windows_resources(cod_bro_loader_core "Call of Duty 2: Big Red One native loader - core" LIBRARY)
    target_link_libraries(cod_bro_loader_core PRIVATE game_code audio input online render text xbox platform shell32 user32 winmm)
    set(loader_core cod_bro_loader_core)

    add_executable(cod_bro_loader win32_stub.c)
    target_compile_options(cod_bro_loader PRIVATE -O2 -ffreestanding -fno-stack-protector
                           -fno-asynchronous-unwind-tables)
    target_link_options(cod_bro_loader PRIVATE -nostdlib -nostartfiles -Wl,-e,_stub_entry
                        -Wl,--image-base=0x10000 -Wl,--disable-dynamicbase -Wl,--disable-reloc-section
                        -Wl,--large-address-aware -Wl,--disable-nxcompat -mwindows)
    target_link_libraries(cod_bro_loader PRIVATE kernel32 user32)
    # Its .rsrc lands after .idata, above the Xbox ranges the XBE is mapped into: the manifest is
    # read at process creation and the version information from the file.
    cod_bro_windows_resources(cod_bro_loader "Call of Duty 2: Big Red One native loader" PROGRAM)
    add_dependencies(cod_bro_loader cod_bro_loader_core)
else()
    add_executable(cod_bro_loader ${loader_sources} compare_linux.cpp crash_posix.cpp os_linux.cpp profiler_linux.cpp thread_env_linux.cpp)
    target_link_libraries(cod_bro_loader PRIVATE game_code audio input online render text xbox platform)
    set(loader_core cod_bro_loader)
    # Code that runs in the harness's test processes must not call anything the
    # compiler adds (stack checks, memcpy for loops).
    set_source_files_properties(compare_linux.cpp PROPERTIES COMPILE_OPTIONS
        "-fno-stack-protector;$<$<CXX_COMPILER_ID:GNU>:-fno-tree-loop-distribute-patterns>")
endif()
target_compile_definitions(${loader_core} PRIVATE
    COD_BRO_SYMBOLS_DIR="${PROJECT_SOURCE_DIR}/tools/data")
