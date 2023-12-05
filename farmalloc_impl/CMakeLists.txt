add_subdirectory(umap)

add_library(farmalloc_impl SHARED)
add_subdirectory(src)
target_include_directories(farmalloc_impl PUBLIC include/public)

target_link_libraries(farmalloc_impl PRIVATE farmalloc_compile_ops)
target_link_libraries(farmalloc_impl PUBLIC
  util
  umap
)
