add_library(far_memory_container INTERFACE)
target_include_directories(far_memory_container INTERFACE include/public)

target_link_libraries(far_memory_container INTERFACE
  farmalloc_abst
  util
)
