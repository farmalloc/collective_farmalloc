include(FetchContent)
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        3f0283de7a9c43200033da996ff9093be3ac84dc # v3.3.2
)
FetchContent_MakeAvailable(Catch2)


list(APPEND TEST_SOURCES
  collective_allocator_aware_b_tree.cpp
  page_aware_b_tree.cpp
  vEB_layout_b_tree.cpp
  hinted_b_tree.cpp

  collective_allocator_aware_skiplist.cpp
  page_aware_skiplist.cpp
  hinted_skiplist.cpp
)

add_executable(collective_farmalloc_test "${TEST_SOURCES}")
target_link_libraries(collective_farmalloc_test PRIVATE
  farmalloc_compile_ops
  farmalloc_impl
  far_memory_container
  Catch2::Catch2WithMain
)


# check if TEST_SOURCES is complete
list(APPEND COPYED_TEST_SOURCES)
foreach(SOURCE IN LISTS TEST_SOURCES)
  file(REAL_PATH "${SOURCE}" REALPATH_SOURCE)
  list(APPEND COPYED_TEST_SOURCES "${REALPATH_SOURCE}")
endforeach(SOURCE IN LISTS TEST_SOURCES)

file(GLOB_RECURSE GLOBBED_TEST_SOURCES "*.cpp")
foreach(GLOBBED IN LISTS GLOBBED_TEST_SOURCES)
  file(REAL_PATH "${GLOBBED}" REALPATH_GLOBBED)
  if(NOT (REALPATH_GLOBBED IN_LIST COPYED_TEST_SOURCES))
    file(RELATIVE_PATH RELPATH_GLOBBED "${CMAKE_CURRENT_SOURCE_DIR}" "${REALPATH_GLOBBED}")
    message(FATAL_ERROR "${RELPATH_GLOBBED} should be included in TEST_SOURCES")
  endif(NOT (REALPATH_GLOBBED IN_LIST COPYED_TEST_SOURCES))
endforeach(GLOBBED IN LISTS GLOBBED_TEST_SOURCES)
