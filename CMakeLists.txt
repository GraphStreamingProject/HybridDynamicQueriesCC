cmake_minimum_required(VERSION 3.15)
project(DynamicQueriesCC)
include (FetchContent)

set(Boost_USE_STATIC_LIBS ON)
find_package(Boost REQUIRED COMPONENTS regex context)
# Force IPO is enabled
cmake_policy(SET CMP0069 NEW) 
set(CMAKE_POLICY_DEFAULT_CMP0069 NEW)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
#set(INTERPROCEDURAL_OPTIMIZATION TRUE)

option(USE_RESIZEABLE_SKETCH "Use ResizeableSketchColumn for test targets" OFF)
option(USE_PARALLEL_HYBRID "Use ParallelConnectivityManager instead of SerialConnectivityManager for hybrid benchmarks" OFF)

# Make the default build type Release. If user or another
# project sets a different value than use that
if(NOT CMAKE_BUILD_TYPE)
  message(STATUS "Setting build type to default -- Release")
  set(CMAKE_BUILD_TYPE "Release" CACHE
      STRING "Choose the type of build." FORCE)
endif()
message(STATUS "DynamicQueries Build Type: ${CMAKE_BUILD_TYPE}")

# controversial choice: return-type warnings should be promoted to errors
if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
  message("Adding GNU compiler flags")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -W -Wall -Werror=return-type -Wno-error=stringop-overflow -Wno-stringop-overflow")
elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
  message("Adding MSVC compiler flags")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Wall")
else()
  message("${CMAKE_CXX_COMPILER_ID} not recognized, no flags added")
endif()


# add_compile_options(-fsanitize=address)
# add_link_options(-fsanitize=address)
# add_compile_options(-fsanitize=undefined)
# add_link_options(-fsanitize=undefined)

######
# Get MPI for distributed communication
######
find_package(MPI REQUIRED)
include_directories(${MPI_INCLUDE_PATH})
add_definitions(-DOMPI_SKIP_MPICXX)
if(MPI_CXX_FOUND)
       message("Found MPI_CXX")
endif()



set(TBB_TEST OFF CACHE BOOL "" FORCE)
set(TBB_STRICT OFF CACHE BOOL "Disable strict warnings as errors for TBB" FORCE)
FetchContent_Declare(
  onetbb
  GIT_REPOSITORY https://github.com/oneapi-src/oneTBB.git
  GIT_TAG        v2022.3.0
)
FetchContent_MakeAvailable(onetbb)

# Create dummy TBBConfig.cmake so Parlay's find_package(TBB) succeeds.
if(NOT TARGET TBB::tbb)
  add_library(TBB::tbb ALIAS tbb)
endif()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/TBBConfig.cmake" "
if(NOT TARGET TBB::tbb)
  add_library(TBB::tbb ALIAS tbb)
endif()
")
set(TBB_DIR "${CMAKE_CURRENT_BINARY_DIR}" CACHE PATH "Path to dummy TBBConfig" FORCE)

# Install GraphZeppelin Project
FetchContent_Declare(
  GraphZeppelinVerifyCC
  GIT_REPOSITORY      https://github.com/GraphStreamingProject/GraphZeppelin
  #GIT_TAG            047b89f5577043b3ec69b91c092c1f1ec0b04816
  GIT_TAG             6c11903714edf1993e920d6d5ed8bde3ecc0963d
)
add_compile_definitions(GLOG_USE_GLOG_EXPORT)

# Install DynamicConnectivity Package
FetchContent_Declare(
  dycon
  GIT_REPOSITORY  https://github.com/GraphStreamingProject/DynamicConnectivity
  GIT_TAG         733d99e4a6985ef656df716ffe23719a1f29786b
)

# TODO - THERE should be a better way to do this that doesn't rely on populate and add_subdirectory
FetchContent_Populate(dycon)
add_subdirectory(${dycon_SOURCE_DIR} ${dycon_BINARY_DIR} EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(GraphZeppelinVerifyCC)

FetchContent_Declare(abseil-cpp
  GIT_REPOSITORY  git@github.com:abseil/abseil-cpp.git
  GIT_TAG         master
)
FetchContent_MakeAvailable(abseil-cpp)


# Fetch mimalloc
set(MI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MI_BUILD_SHARED ON CACHE BOOL "" FORCE)
set(MI_BUILD_STATIC OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  mimalloc
  GIT_REPOSITORY https://github.com/microsoft/mimalloc.git
  GIT_TAG        v2.1.2
)
FetchContent_MakeAvailable(mimalloc)

# add_executable(ufo_tests
#   test/test_runner.cpp
#   test/ufo_tree_test.cpp
# )
# target_include_directories(ufo_tests PUBLIC include)
# target_link_libraries(ufo_tests PRIVATE GraphZeppelinVerifyCC parlay absl::flat_hash_set)

add_executable(dynamicCC_tests
  test/test_runner.cpp
  test/skiplist_test.cpp
  test/euler_tour_tree_test.cpp
  test/link_cut_tree_test.cpp
  test/graph_tiers_test.cpp
  test/ufo_tree_test.cpp
  test/lct_cutset_test.cpp
  test/cutset_benchmarks.cpp

  src/skiplist.cpp
  src/euler_tour_tree.cpp
  src/sketchless_skiplist.cpp
  src/sketchless_euler_tour_tree.cpp
  src/link_cut_tree.cpp
  src/graph_tiers.cpp
  src/batch_tiers.cpp
)
target_include_directories(dynamicCC_tests PUBLIC include ${MPI_C_INCLUDE_PATH})
add_dependencies(dynamicCC_tests GraphZeppelinVerifyCC dycon)
# force mimalloc linking on tests, TODO - we should probably just leave this as a user option? not sure / TBD.
# TODO - evaluate performance impact of mimalloc.
target_link_libraries(dynamicCC_tests PRIVATE GraphZeppelinVerifyCC dycon ${MPI_LIBRARIES} tbb parlay -Wl,--no-as-needed mimalloc -Wl,--as-needed)

target_compile_options(dynamicCC_tests PUBLIC -fopenmp)
target_link_options(dynamicCC_tests PUBLIC -fopenmp)

add_executable(mpi_dynamicCC_tests
  test/mpi_test_runner.cpp
  test/mpi_graph_tiers_test.cpp

  src/skiplist.cpp
  src/sketchless_skiplist.cpp
  src/euler_tour_tree.cpp
  src/sketchless_euler_tour_tree.cpp
  src/link_cut_tree.cpp
  src/input_node.cpp
  src/tier_node.cpp
)
target_compile_definitions(mpi_dynamicCC_tests PUBLIC SKETCH_BUFFER_SIZE=${SKETCH_BUFFER_SIZE})
target_include_directories(mpi_dynamicCC_tests PUBLIC include ${MPI_C_INCLUDE_PATH})
add_dependencies(mpi_dynamicCC_tests GraphZeppelinVerifyCC dycon)
target_link_libraries(mpi_dynamicCC_tests PRIVATE GraphZeppelinVerifyCC dycon ${MPI_LIBRARIES})

target_compile_options(mpi_dynamicCC_tests PUBLIC -fopenmp)
target_link_options(mpi_dynamicCC_tests PUBLIC -fopenmp)


add_executable(hybrid_mpi_dynamicCC_tests
  test/hybrid_test_runner.cpp
  test/hybrid_tests.cpp

  src/skiplist.cpp
  src/sketchless_skiplist.cpp
  src/euler_tour_tree.cpp
  src/sketchless_euler_tour_tree.cpp
  src/link_cut_tree.cpp
  src/input_node.cpp
  src/tier_node.cpp
)
target_compile_definitions(hybrid_mpi_dynamicCC_tests PUBLIC SKETCH_BUFFER_SIZE=${SKETCH_BUFFER_SIZE})
target_include_directories(hybrid_mpi_dynamicCC_tests PUBLIC include ${MPI_C_INCLUDE_PATH})
add_dependencies(hybrid_mpi_dynamicCC_tests GraphZeppelinVerifyCC dycon)
target_link_libraries(hybrid_mpi_dynamicCC_tests PRIVATE GraphZeppelinVerifyCC dycon ${MPI_LIBRARIES})

target_compile_options(hybrid_mpi_dynamicCC_tests PUBLIC -fopenmp)
target_link_options(hybrid_mpi_dynamicCC_tests PUBLIC -fopenmp)

add_executable(hybrid_shmem_dynamicCC_tests
  test/hybrid_shmem_test_runner.cpp
  test/hybrid_shmem_tests.cpp

  src/skiplist.cpp
  src/graph_tiers.cpp
  src/batch_tiers.cpp
  src/sketchless_skiplist.cpp
  src/euler_tour_tree.cpp
  src/sketchless_euler_tour_tree.cpp
  src/link_cut_tree.cpp
  # src/input_node.cpp
  # src/tier_node.cpp
)
target_compile_definitions(hybrid_shmem_dynamicCC_tests PUBLIC SKETCH_BUFFER_SIZE=${SKETCH_BUFFER_SIZE})
target_include_directories(hybrid_shmem_dynamicCC_tests PUBLIC include ${MPI_C_INCLUDE_PATH})
add_dependencies(hybrid_shmem_dynamicCC_tests GraphZeppelinVerifyCC dycon)
target_link_libraries(hybrid_shmem_dynamicCC_tests PRIVATE GraphZeppelinVerifyCC dycon ${MPI_LIBRARIES} tbb -Wl,--no-as-needed mimalloc -Wl,--as-needed)

target_compile_options(hybrid_shmem_dynamicCC_tests PUBLIC -fopenmp)
target_link_options(hybrid_shmem_dynamicCC_tests PUBLIC -fopenmp)

if(USE_RESIZEABLE_SKETCH)
  target_compile_definitions(dynamicCC_tests PUBLIC USE_RESIZEABLE_SKETCH=1)
  target_compile_definitions(mpi_dynamicCC_tests PUBLIC USE_RESIZEABLE_SKETCH=1)
  target_compile_definitions(hybrid_mpi_dynamicCC_tests PUBLIC USE_RESIZEABLE_SKETCH=1)
  target_compile_definitions(hybrid_shmem_dynamicCC_tests PUBLIC USE_RESIZEABLE_SKETCH=1)
endif()

#######
# TODO: Is MPI INCLUDE PATH necessary?
if(MPI_COMPILE_FLAGS)
  set_target_properties(mpi_dynamicCC_tests PROPERTIES
    COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
endif()
if(MPI_LINK_FLAGS)
  set_target_properties(mpi_dynamicCC_tests PROPERTIES
    LINK_FLAGS "${MPI_LINK_FLAGS}")
endif()

#######

#####################################################################
# Benchmark targets
#####################################################################
# Common source files for each backend
set(SHMEM_BENCH_SOURCES
  src/skiplist.cpp
  src/sketchless_skiplist.cpp
  src/sketchless_euler_tour_tree.cpp
)

set(MPI_BENCH_SOURCES
  src/skiplist.cpp
  src/sketchless_skiplist.cpp
  src/sketchless_euler_tour_tree.cpp
)

# Function: add_bench_targets(NAME CUPCAKE_ALGO CUTSET_DS USE_HYBRID USE_RESIZEABLE)
# Creates bench_speed_<NAME> and bench_profile_<NAME>
function(add_bench_targets NAME CUPCAKE_ALGO CUTSET_DS USE_HYBRID USE_RESIZEABLE)
  # Determine source files and link libraries based on tier algo
  if(CUPCAKE_ALGO STREQUAL "mpi_tiers")
    set(_SOURCES ${MPI_BENCH_SOURCES})
    set(_EXTRA_LIBS ${MPI_LIBRARIES} -Wl,--no-as-needed mimalloc -Wl,--as-needed)
  else()
    set(_SOURCES ${SHMEM_BENCH_SOURCES})
    set(_EXTRA_LIBS tbb -Wl,--no-as-needed mimalloc -Wl,--as-needed)
  endif()

  foreach(BENCH_TYPE speed profile)
    set(TARGET_NAME "bench_${BENCH_TYPE}_${NAME}")
    add_executable(${TARGET_NAME}
      test/bench_${BENCH_TYPE}.cpp
      ${_SOURCES}
    )

    # Compile definitions for config selection
    target_compile_definitions(${TARGET_NAME} PRIVATE
      CUPCAKE_ALGO_${CUPCAKE_ALGO}=1
      CUTSET_DS_${CUTSET_DS}=1
      USE_HYBRID=${USE_HYBRID}
      SKETCH_BUFFER_SIZE=${SKETCH_BUFFER_SIZE}
    )
    if(USE_RESIZEABLE)
      target_compile_definitions(${TARGET_NAME} PRIVATE USE_RESIZEABLE_SKETCH=1)
    endif()
    if(USE_PARALLEL_HYBRID AND USE_HYBRID)
      target_compile_definitions(${TARGET_NAME} PRIVATE USE_PARALLEL_HYBRID=1)
    endif()

    target_include_directories(${TARGET_NAME} PUBLIC include ${MPI_C_INCLUDE_PATH})
    add_dependencies(${TARGET_NAME} GraphZeppelinVerifyCC dycon)
    target_link_libraries(${TARGET_NAME} PRIVATE
      GraphZeppelinVerifyCC dycon ${MPI_LIBRARIES} ${_EXTRA_LIBS}
    )
    target_compile_options(${TARGET_NAME} PUBLIC -fopenmp)
    target_link_options(${TARGET_NAME} PUBLIC -fopenmp)
  endforeach()
endfunction()

# ---- Generate benchmark targets ----
option(BENCH_ALL "Build ALL algo×cutset×sketch×hybrid benchmark configs" OFF)

if(BENCH_ALL)
  # Full matrix: 3 algos × 3 cutsets × 2 sketches × 2 hybrid = 36 configs
  foreach(ALGO_PAIR "batch;BATCH_TIERS" "graph;GRAPH_TIERS" "mpi;MPI_TIERS")
    list(GET ALGO_PAIR 0 ALGO_LABEL)
    list(GET ALGO_PAIR 1 ALGO_FLAG)
    foreach(CUTSET_PAIR "ufo;UFO" "lct;LCT" "ett;ETT")
      list(GET CUTSET_PAIR 0 CUTSET_LABEL)
      list(GET CUTSET_PAIR 1 CUTSET_FLAG)
      foreach(SKETCH_PAIR "fixed;0" "resizeable;1")
        list(GET SKETCH_PAIR 0 SKETCH_LABEL)
        list(GET SKETCH_PAIR 1 SKETCH_FLAG)
        add_bench_targets(${ALGO_LABEL}_${CUTSET_LABEL}_${SKETCH_LABEL}
          ${ALGO_FLAG} ${CUTSET_FLAG} 0 ${SKETCH_FLAG})
        add_bench_targets(${ALGO_LABEL}_${CUTSET_LABEL}_${SKETCH_LABEL}_hybrid
          ${ALGO_FLAG} ${CUTSET_FLAG} 1 ${SKETCH_FLAG})
      endforeach()
    endforeach()
  endforeach()
else()
  # Default: MPI × {UFO, LCT, ETT} × {fixed, resizeable}, pure cupcake + hybrid
  add_bench_targets(mpi_ufo_fixed              MPI_TIERS UFO 0 0)
  add_bench_targets(mpi_lct_fixed              MPI_TIERS LCT 0 0)
  add_bench_targets(mpi_ett_fixed              MPI_TIERS ETT 0 0)
  add_bench_targets(mpi_ufo_fixed_hybrid       MPI_TIERS UFO 1 0)
  add_bench_targets(mpi_lct_fixed_hybrid       MPI_TIERS LCT 1 0)
  add_bench_targets(mpi_ett_fixed_hybrid       MPI_TIERS ETT 1 0)

  add_bench_targets(mpi_ufo_resizeable          MPI_TIERS UFO 0 1)
  add_bench_targets(mpi_lct_resizeable          MPI_TIERS LCT 0 1)
  add_bench_targets(mpi_ett_resizeable          MPI_TIERS ETT 0 1)
  add_bench_targets(mpi_ufo_resizeable_hybrid   MPI_TIERS UFO 1 1)
  add_bench_targets(mpi_lct_resizeable_hybrid   MPI_TIERS LCT 1 1)
  add_bench_targets(mpi_ett_resizeable_hybrid   MPI_TIERS ETT 1 1)

  # Add more one-liners here as needed, e.g.:
  # add_bench_targets(batch_ufo_fixed  BATCH_TIERS UFO 0 0)
  add_bench_targets(cf CF NONE 0 0)
endif()

