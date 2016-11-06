set( LLVM_LINK_COMPONENTS
  ${LLVM_TARGETS_TO_BUILD}
  Option
  Support
)

add_clang_executable(cxxclean
	cxx_clean.cpp
	parser.cpp	
	project.cpp
	history.cpp
	tool.cpp
	vs.cpp
	html_log.cpp
	main.cpp
)

include_directories(
	${CLANG_SOURCE_DIR}
)

target_link_libraries(cxxclean
  clangAST
  clangBasic
  clangDriver
  clangFrontend
  clangRewriteFrontend
  clangTooling
)