# zstd 1.5.7's decoder (third_party/zstd: lib/common + lib/decompress, BSD license): the texture packs' stored files
# (render::texture_pack). Plain C, no assembly, no legacy formats.
add_library(zstd_decompress STATIC
    ${PROJECT_SOURCE_DIR}/third_party/zstd/common/debug.c
    ${PROJECT_SOURCE_DIR}/third_party/zstd/common/entropy_common.c
    ${PROJECT_SOURCE_DIR}/third_party/zstd/common/error_private.c
    ${PROJECT_SOURCE_DIR}/third_party/zstd/common/fse_decompress.c
    ${PROJECT_SOURCE_DIR}/third_party/zstd/common/xxhash.c
    ${PROJECT_SOURCE_DIR}/third_party/zstd/common/zstd_common.c
    ${PROJECT_SOURCE_DIR}/third_party/zstd/decompress/huf_decompress.c
    ${PROJECT_SOURCE_DIR}/third_party/zstd/decompress/zstd_ddict.c
    ${PROJECT_SOURCE_DIR}/third_party/zstd/decompress/zstd_decompress.c
    ${PROJECT_SOURCE_DIR}/third_party/zstd/decompress/zstd_decompress_block.c
)
target_include_directories(zstd_decompress PUBLIC ${PROJECT_SOURCE_DIR}/third_party/zstd)
target_compile_definitions(zstd_decompress PRIVATE ZSTD_DISABLE_ASM=1 ZSTD_LEGACY_SUPPORT=0 ZSTD_TRACE=0)
set_target_properties(zstd_decompress PROPERTIES POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden)
if(NOT MSVC)
    target_compile_options(zstd_decompress PRIVATE -w)
endif()
