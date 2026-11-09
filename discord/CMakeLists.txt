# Discord Rich Presence over Discord's local IPC (no external library).
add_library(discord_rpc STATIC rich_presence.cpp)
target_include_directories(discord_rpc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(discord_rpc PUBLIC cod_bro_options)
find_package(Threads REQUIRED)
target_link_libraries(discord_rpc PUBLIC Threads::Threads)
