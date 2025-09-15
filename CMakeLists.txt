cmake_minimum_required(VERSION 3.15)
project(DynamicQueriesCC)
include (FetchContent)

# Force IPO is enabled
cmake_policy(SET CMP0069 NEW) 
set(CMAKE_POLICY_DEFAULT_CMP0069 NEW)

set(CMAKE_CXX_STANDARD 17)
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

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
  message("Adding GNU compiler flags")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -W -Wall")
elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
  message("Adding MSVC compiler flags")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Wall")
else()
  message("${CMAKE_CXX_COMPILER_ID} not recognized, no flags added")
endif()

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
  GIT_TAG             db06e662aa7563716e49e3f5036e773a97a7dd64 #main
)
FetchContent_MakeAvailable(GraphZeppelinVerifyCC)

FetchContent_Declare(parlaylib
  GIT_REPOSITORY  https://github.com/cmuparlay/parlaylib.git
  GIT_TAG         master
)
FetchContent_GetProperties(parlaylib)
if(NOT parlaylib_POPULATED)
  FetchContent_Populate(parlaylib)
  add_subdirectory(${parlaylib_SOURCE_DIR} EXCLUDE_FROM_ALL)
endif()

FetchContent_Declare(parallel-ett
    GIT_REPOSITORY  git@github.com:quintendeman/parallel-euler-tour-tree.git
    GIT_TAG         master
)
FetchContent_MakeAvailable(parallel-ett)

#add_compile_options(-fsanitize=address)
#add_link_options(-fsanitize=address)
#add_compile_options(-fsanitize=undefined)
#add_link_options(-fsanitize=undefined)

add_executable(dynamicCC_tests
  test/test_runner.cpp
  test/skiplist_test.cpp
  test/euler_tour_tree_test.cpp
  test/link_cut_tree_test.cpp
  test/graph_tiers_test.cpp

  src/skiplist.cpp
  src/euler_tour_tree.cpp
  src/link_cut_tree.cpp
  src/graph_tiers.cpp
)

target_include_directories(dynamicCC_tests PUBLIC include ${MPI_C_INCLUDE_PATH})
add_dependencies(dynamicCC_tests GraphZeppelinVerifyCC)
target_link_libraries(dynamicCC_tests PRIVATE GraphZeppelinVerifyCC ${MPI_LIBRARIES})

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

target_include_directories(mpi_dynamicCC_tests PUBLIC include ${MPI_C_INCLUDE_PATH})
add_dependencies(mpi_dynamicCC_tests GraphZeppelinVerifyCC)
target_link_libraries(mpi_dynamicCC_tests PRIVATE GraphZeppelinVerifyCC ${MPI_LIBRARIES})

add_executable(par_ett_test
  test/par_ett_test.cpp
)
target_link_libraries(par_ett_test parlay par-ett)

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
