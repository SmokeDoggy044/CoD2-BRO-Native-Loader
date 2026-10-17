add_subdirectory(code)

add_executable(cod_bro
    main.cpp
)
target_link_libraries(cod_bro PRIVATE platform)
