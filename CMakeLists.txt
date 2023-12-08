cmake_minimum_required(VERSION 3.13)

project(collective_farmalloc)


add_library(farmalloc_compile_ops INTERFACE)
target_compile_options(farmalloc_compile_ops INTERFACE
  -Werror
  -Wno-error=use-after-free
  -Wall
  -Wextra
  -Wconversion
  -Wswitch-default
  -Wdisabled-optimization
  -Wformat
  -Winit-self
  -Woverloaded-virtual
  -Wfloat-equal
  -Wreturn-type
)
target_compile_features(farmalloc_compile_ops INTERFACE cxx_std_20)


add_subdirectory(util)
add_subdirectory(farmalloc_abst)
add_subdirectory(farmalloc_impl)
add_subdirectory(far_memory_container)
