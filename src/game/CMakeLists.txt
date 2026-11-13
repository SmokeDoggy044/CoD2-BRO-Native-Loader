# The full native build's cod_bro. With the loader's code built (COD_BRO_BUILD_LOADER), cod_bro is our own engine
# (src/runtime/loader/host_main.cpp) instead; this placeholder only checks the portable layers elsewhere.
if(NOT COD_BRO_BUILD_LOADER)
    add_executable(cod_bro
        main.cpp
    )
    target_link_libraries(cod_bro PRIVATE platform)
endif()
