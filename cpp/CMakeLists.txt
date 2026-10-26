cmake_minimum_required(VERSION 3.16)
project(pyatnashki_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(pyatnashki_cpp
    src/main.cpp
    src/parser.cpp
    src/validation.cpp
    src/algorithms_exact.cpp src/algorithms_advanced.cpp
    src/solver_rowbyrow.cpp
    src/solver_columnbycolumn.cpp
)
target_include_directories(pyatnashki_cpp PRIVATE include)
if(WIN32)
  target_link_libraries(pyatnashki_cpp PRIVATE shell32)
endif()
if(MSVC)
  target_compile_options(pyatnashki_cpp PRIVATE /W4)
else()
  target_compile_options(pyatnashki_cpp PRIVATE -Wall -Wextra -Wpedantic)
endif()
