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
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -W -Wall -Werror=return-type")
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



# Install GraphZeppelin Project
FetchContent_Declare(
  GraphZeppelinVerifyCC

  GIT_REPOSITORY      https://github.com/GraphStreamingProject/GraphZeppelin
  # GIT_TAG             2c633f5814f7edc79632cceb629280f6221b0281 #ksparse_recovery
  # GIT_TAG             892b5b71b139100c309c79bb6ab94a5596fc606d
  GIT_TAG            047b89f5577043b3ec69b91c092c1f1ec0b04816
)
add_compile_definitions(GLOG_USE_GLOG_EXPORT)


# NOTE - not sure if this is gonna work
# Install DynamicConnectivity Package
# it doesn't. the

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


find_package(TBB REQUIRED)

add_executable(ufo_tests
  test/test_runner.cpp
  test/ufo_tree_test.cpp
)
target_include_directories(ufo_tests PUBLIC include)
target_link_libraries(ufo_tests PRIVATE GraphZeppelinVerifyCC parlay absl::flat_hash_set)

add_executable(dynamicCC_tests
  test/test_runner.cpp
  test/skiplist_test.cpp
  test/euler_tour_tree_test.cpp
  test/link_cut_tree_test.cpp
  test/graph_tiers_test.cpp
  test/ufo_tree_test.cpp

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
target_link_libraries(dynamicCC_tests PRIVATE GraphZeppelinVerifyCC dycon ${MPI_LIBRARIES} TBB::tbb parlay)

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
target_link_libraries(hybrid_shmem_dynamicCC_tests PRIVATE GraphZeppelinVerifyCC dycon ${MPI_LIBRARIES} TBB::tbb)

target_compile_options(hybrid_shmem_dynamicCC_tests PUBLIC -fopenmp)
target_link_options(hybrid_shmem_dynamicCC_tests PUBLIC -fopenmp)

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
