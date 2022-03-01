cmake_minimum_required(VERSION 3.10)

set (CMAKE_CXX_STANDARD 11)
set(CMAKE_BUILD_TYPE Debug)

# set the project name
project(cutvols)

# insert Boost
SET(Boost_USE_STATIC_LIBS ON)
find_package( Boost COMPONENTS log program_options REQUIRED )
list(APPEND PROJ_INCLUDE_DIRS ${Boost_INCLUDE_DIR})
list(APPEND PROJ_LIBRARY_DIRS ${Boost_LIBRARIES})
link_directories(${Boost_LIBRARIES})
include_directories(${Boost_INCLUDE_DIR})

# add the executable
add_executable(cutvols main.cpp vols_cutter.hpp vols_cutter.cpp)

# link dependencies to the executable
target_link_libraries(cutvols ${Boost_LIBRARIES})

# install targets
install(TARGETS cutvols RUNTIME DESTINATION "${CMAKE_INSTALL_PREFIX}/bin")
