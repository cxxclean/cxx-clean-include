set(LLVM_LINK_COMPONENTS support)

add_clang_executable(loop-convert
  LoopConvert.cpp
)
target_link_libraries(loop-convert
  clangTooling
  clangBasic
  clangASTMatchers
)