# Xbox Live / network replacement (portable).
add_library(online STATIC
    lan.cpp
    xnet.cpp
    xonline.cpp
)
target_include_directories(online PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(online PUBLIC xbox platform)
