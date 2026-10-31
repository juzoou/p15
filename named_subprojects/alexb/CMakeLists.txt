cmake_minimum_required(VERSION 3.13)
project(p15 CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
  add_compile_options(/O2 /utf-8 /EHsc /W3)
else()
  add_compile_options(-O2 -Wall -Wextra)
endif()

add_library(p15core STATIC
  src/common.cpp src/io.cpp src/route.cpp src/exact.cpp src/reduce.cpp
  src/pdb.cpp src/ida.cpp src/greedy.cpp src/beam.cpp src/best.cpp)

foreach(alg exact ida reduce greedy beam best)
  add_executable(p15_${alg} src/main_${alg}.cpp)
  target_link_libraries(p15_${alg} p15core)
  # глубокая рекурсия в ida: стек 256 МБ (в Windows по умолчанию всего 1 МБ)
  if(MSVC)
    target_link_options(p15_${alg} PRIVATE /STACK:268435456)
  elseif(WIN32)
    target_link_options(p15_${alg} PRIVATE -Wl,--stack,268435456)
  endif()
  set_target_properties(p15_${alg} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
  foreach(cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(p15_${alg} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${cfg} ${CMAKE_SOURCE_DIR}/bin)
  endforeach()
endforeach()
