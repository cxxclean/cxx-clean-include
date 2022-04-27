set(LLVM_LINK_COMPONENTS
  Core
  Option
  Support
)

add_clang_tool(cxxclean
	cxx_clean.cpp
	parser.cpp
	project.cpp
	history.cpp
	tool.cpp
	vs.cpp
	html_log.cpp
	main.cpp
)

set(CXXCLEAN_DEPS_LIB_DEPS
  clangAST
  clangBasic
  clangDriver
  clangFrontend
  clangRewriteFrontend
  clangTooling
  clangCodeGen
  clangFrontendTool
  clangLex
  clangParse
  clangSerialization
  clangTooling
  clangDependencyScanning
  )

clang_target_link_libraries(cxxclean
  PRIVATE
  ${CXXCLEAN_DEPS_LIB_DEPS}
  )
