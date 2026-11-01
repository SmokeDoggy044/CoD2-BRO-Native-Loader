# Route-B only: deleted once the game no longer needs the original XBE.
set(loader_sources
    compare_common.cpp
    crash.cpp
    diag_summary.cpp
    hooks.cpp
    host_image.cpp
    kernel_stubs.cpp
    kernel_thunks.cpp
    options.cpp
    title_setup.cpp
    title_session.cpp
    title_threads.cpp
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
    # The program proper is a DLL; cod_bro_loader.exe is a stub linked at the XBE's
    # base address that keeps the Xbox address ranges free and loads it (win32_stub.c).
    # The stub is a GUI program, so starting it from the launcher opens no console;
    # from a console, the loader attaches to it (os_win32.cpp).
    # Two DLLs share the runtime library: cod_bro_loader_core.dll (main.cpp + the kernel) and
    # cod_bro_core.dll (our own engine, host_main.cpp, xbox_host only - no kernel).
    add_library(cod_bro_runtime STATIC ${loader_sources} compare_win32.cpp crash_win32.cpp text_trap_win32.cpp os_win32.cpp profiler_win32.cpp thread_env_win32.cpp)
    target_link_libraries(cod_bro_runtime PUBLIC game_code audio input online render text xbox_host platform settings_ui shell32 user32 winmm)
    set(loader_core cod_bro_runtime)

    add_library(cod_bro_loader_core SHARED main.cpp entry_loader_win32.cpp)
    set_target_properties(cod_bro_loader_core PROPERTIES PREFIX "")
    cod_bro_windows_resources(cod_bro_loader_core "Call of Duty 2: Big Red One native loader - core" LIBRARY)
    target_link_libraries(cod_bro_loader_core PRIVATE cod_bro_runtime xbox)

    add_library(cod_bro_core SHARED host_main.cpp entry_host_win32.cpp)
    set_target_properties(cod_bro_core PROPERTIES PREFIX "")
    cod_bro_windows_resources(cod_bro_core "Call of Duty 2: Big Red One - own engine core" LIBRARY)
    target_link_libraries(cod_bro_core PRIVATE cod_bro_runtime)  # no kernel (xbox_host only)

    add_executable(cod_bro_loader win32_stub.c)
    target_compile_options(cod_bro_loader PRIVATE -O2 -ffreestanding -fno-stack-protector
                           -fno-asynchronous-unwind-tables)
    target_link_options(cod_bro_loader PRIVATE -nostdlib -nostartfiles -Wl,-e,_stub_entry
                        -Wl,--image-base=0x10000 -Wl,--disable-dynamicbase -Wl,--disable-reloc-section
                        -Wl,--large-address-aware -Wl,--disable-nxcompat -mwindows)
    target_link_libraries(cod_bro_loader PRIVATE kernel32)
    # Its .rsrc lands after .idata, above the Xbox ranges the XBE is mapped into: the manifest is
    # read at process creation and the version information from the file.
    cod_bro_windows_resources(cod_bro_loader "Call of Duty 2: Big Red One native loader" PROGRAM)
    add_dependencies(cod_bro_loader cod_bro_loader_core)

    # cod_bro.exe: our own engine, the same kind of stub with cod_bro_core.dll's entry.
    add_executable(cod_bro win32_stub.c)
    target_compile_definitions(cod_bro PRIVATE COD_BRO_LOADER_CORE_DLL="cod_bro_core.dll"
                               COD_BRO_LOADER_CORE_ENTRY="cod_bro_host_main")
    target_compile_options(cod_bro PRIVATE -O2 -ffreestanding -fno-stack-protector -fno-asynchronous-unwind-tables)
    target_link_options(cod_bro PRIVATE -nostdlib -nostartfiles -Wl,-e,_stub_entry
                        -Wl,--image-base=0x10000 -Wl,--disable-dynamicbase -Wl,--disable-reloc-section
                        -Wl,--large-address-aware -Wl,--disable-nxcompat -mwindows)
    target_link_libraries(cod_bro PRIVATE kernel32)
    cod_bro_windows_resources(cod_bro "Call of Duty 2: Big Red One - own engine" PROGRAM)
    add_dependencies(cod_bro cod_bro_core)
else()
    # The loader's code as a library: cod_bro_loader (main.cpp) and our own engine cod_bro (host_main.cpp) share it.
    add_library(cod_bro_runtime STATIC ${loader_sources} compare_linux.cpp crash_posix.cpp text_trap_linux.cpp os_linux.cpp profiler_linux.cpp thread_env_linux.cpp)
    target_link_libraries(cod_bro_runtime PUBLIC game_code audio input online render text xbox_host platform settings_ui)
    add_executable(cod_bro_loader main.cpp entry_loader_linux.cpp)
    target_link_libraries(cod_bro_loader PRIVATE cod_bro_runtime xbox)  # cod_bro: no kernel (xbox_host only)
    add_executable(cod_bro host_main.cpp entry_host_linux.cpp)
    target_link_libraries(cod_bro PRIVATE cod_bro_runtime)
    set(loader_core cod_bro_runtime)
    # Code that runs in the harness's test processes must not call anything the
    # compiler adds (stack checks, memcpy for loops).
    set_source_files_properties(compare_linux.cpp PROPERTIES COMPILE_OPTIONS
        "-fno-stack-protector;$<$<CXX_COMPILER_ID:GNU>:-fno-tree-loop-distribute-patterns>")
endif()
target_compile_definitions(${loader_core} PUBLIC
    COD_BRO_SYMBOLS_DIR="${PROJECT_SOURCE_DIR}/tools/data")
