cmake_minimum_required(VERSION 3.1.0)
project(assign1)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_COMPILER "clang")
set(CMAKE_CXX_COMPILER "clang++")
# set(LLVM_DIR "../llvm10/build")
set(LLVM_ENABLE_RTTI ON)
set(LLVM_ENABLE_EH ON)

find_package(Clang REQUIRED CONFIG HINTS ${LLVM_DIR} ${LLVM_DIR}/lib/cmake/clang NO_DEFAULT_PATH)

include_directories(${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS} SYSTEM)
link_directories(${LLVM_LIBRARY_DIRS})

file(GLOB SOURCE "./*.cpp")

add_executable(ast-interpreter ${SOURCE})

set( LLVM_LINK_COMPONENTS
  ${LLVM_TARGETS_TO_BUILD}
  Option
  Support
  )


target_link_libraries(ast-interpreter
  clangAST
  clangBasic
  clangFrontend
  clangTooling
  )

install(TARGETS ast-interpreter
  RUNTIME DESTINATION bin)
