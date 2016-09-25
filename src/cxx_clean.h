///<------------------------------------------------------------------------------
//< @file:   cxx_clean.h
//< @author: 洪坤安
//< @brief:  实现clang库中与抽象语法树有关的各种基础类
//< Copyright (c) 2016 game. All rights reserved.
///<------------------------------------------------------------------------------

#ifndef _cxx_clean_h_
#define _cxx_clean_h_

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Lex/Preprocessor.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Path.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/Tooling.h"

using namespace clang;
using namespace clang::driver;
using namespace clang::tooling;
using namespace llvm;

using namespace std;
using namespace llvm::sys;
using namespace llvm::sys::path;
using namespace llvm::sys::fs;

namespace cxxclean
{
	class ParsingFile;
	class VsProject;
}

namespace cxxclean
{
	// 预处理器，当#define、#if、#else等预处理关键字被预处理时使用本预处理器
	class CxxCleanPreprocessor : public PPCallbacks
	{
	public:
		explicit CxxCleanPreprocessor(ParsingFile *mainFile);

	public:
		// 文件切换
		void FileChanged(SourceLocation loc, FileChangeReason reason,
		                 SrcMgr::CharacteristicKind FileType,
		                 FileID prevFileID = FileID()) override;

		// 文件被跳过
		void FileSkipped(const FileEntry &SkippedFile,
		                 const Token &FilenameTok,
		                 SrcMgr::CharacteristicKind FileType) override;

		// 处理#include
		void InclusionDirective(SourceLocation HashLoc /*#的位置*/, const Token &includeTok,
		                        StringRef fileName, bool isAngled/*是否被<>包含，否则是被""包围*/, CharSourceRange filenameRange,
		                        const FileEntry *file, StringRef searchPath, StringRef relativePath, const clang::Module *Imported) override;

		// 定义宏，如#if defined DEBUG
		void Defined(const Token &macroName, const MacroDefinition &definition, SourceRange range) override;

		// 处理#define
		void MacroDefined(const Token &macroName, const MacroDirective *direct) override;

		// 宏被#undef
		void MacroUndefined(const Token &macroName, const MacroDefinition &definition) override;

		// 宏扩展
		void MacroExpands(const Token &macroName,
		                  const MacroDefinition &definition,
		                  SourceRange range,
		                  const MacroArgs *args) override;

		// #ifdef
		void Ifdef(SourceLocation loc, const Token &macroName, const MacroDefinition &definition) override;

		// #ifndef
		void Ifndef(SourceLocation loc, const Token &macroName, const MacroDefinition &definition) override;

		// 当前正在解析的cpp文件信息
		ParsingFile *m_root;
	};

	// 通过实现RecursiveASTVisitor基类，自定义访问c++抽象语法树时的操作
	class CxxCleanASTVisitor : public RecursiveASTVisitor<CxxCleanASTVisitor>
	{
	public:
		explicit CxxCleanASTVisitor(ParsingFile *rootFile);

		// 用于调试：打印语句的信息
		void PrintStmt(Stmt *s);

		// 访问单条语句
		bool VisitStmt(Stmt *s);

		// 访问函数声明
		bool VisitFunctionDecl(FunctionDecl *f);

		// 访问class、struct、union、enum时
		bool VisitCXXRecordDecl(CXXRecordDecl *r);

		// 当发现变量定义时该接口被调用
		bool VisitVarDecl(VarDecl *var);

		// 比如：typedef int A;
		bool VisitTypedefDecl(clang::TypedefDecl *d);

		// 比如：namespace A{}
		bool VisitNamespaceDecl(clang::NamespaceDecl *d);

		// 比如：using namespace std;
		bool VisitUsingDirectiveDecl(clang::UsingDirectiveDecl *d);

		// 比如：using std::string;
		bool VisitUsingDecl(clang::UsingDecl *d);

		// 访问成员变量
		bool VisitFieldDecl(FieldDecl *decl);

		// 构造声明
		bool VisitCXXConstructorDecl(CXXConstructorDecl *decl);

		// 构造语句
		bool VisitCXXConstructExpr(CXXConstructExpr *expr);

	private:
		// 当前正在解析的cpp文件信息
		ParsingFile*	m_root;
	};

	// 遍历声明，实现ASTConsumer接口用于读取clang解析器生成的ast语法树
	class CxxCleanASTConsumer : public ASTConsumer
	{
	public:
		explicit CxxCleanASTConsumer(ParsingFile *rootFile);

		// 覆盖：遍历最顶层声明的方法
		bool HandleTopLevelDecl(DeclGroupRef declgroup) override;

		// 这个函数对于每个源文件仅被调用一次，比如，若有一个hello.cpp中#include了许多头文件，也只会调用一次本函数
		void HandleTranslationUnit(ASTContext& context) override;

	public:
		// 当前正在解析的cpp文件信息
		ParsingFile*		m_root;

		// 抽象语法树访问器
		CxxCleanASTVisitor	m_visitor;
	};

	// `TextDiagnosticPrinter`可以将错误信息打印在控制台上，为了调试方便将其作为基类
	class CxxcleanDiagnosticConsumer : public TextDiagnosticPrinter
	{
	public:
		explicit CxxcleanDiagnosticConsumer(DiagnosticOptions *diags);

		void Clear();

		void BeginSourceFile(const LangOptions &LO, const Preprocessor *PP) override;

		virtual void EndSourceFile() override;

		// 是否是严重编译错误（暂时用不到）
		bool IsFatalError(int errid);

		// 当一个错误发生时，会调用此函数，在这个函数里记录编译错误和错误号
		virtual void HandleDiagnostic(DiagnosticsEngine::Level diagLevel, const Diagnostic &info) override;

		std::string			m_errorTip;
		raw_string_ostream	m_log;
	};

	// 对于ClangTool接收到的每个cpp源文件，都将new一个CxxCleanAction
	class CxxCleanAction : public ASTFrontendAction
	{
	public:
		CxxCleanAction()
			: m_root(nullptr)
		{
		}

		// 开始文件处理
		bool BeginSourceFileAction(CompilerInstance &compiler, StringRef filename) override;

		// 结束文件处理
		void EndSourceFileAction() override;

		// 创建抽象语法树解析器
		std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &compiler, StringRef file) override;

	private:
		// 文件重写类，用来修改c++源码内容
		Rewriter	m_rewriter;

		// 当前正在解析的cpp文件信息
		ParsingFile	*m_root;
	};

	// 本工具的命令行参数解析器，参照clang库的CommonOptionParser类实现而成
	class CxxCleanOptionsParser
	{
	public:
		CxxCleanOptionsParser() {}

		// 解析选项并将解析结果存入相应的对象，若应中途退出则返回true，否则返回false
		bool ParseOptions(int &argc, const char **argv);

		// 拆分传入的命令行参数，"--"分隔符前面的命令行参数将被本工具解析，后面的命令行参数将被clang库解析
		// 注意：argc将被更改为"--"分隔符前的参数个数
		// 例如：
		//		假设使用cxxclean -clean ./hello/ -- -include log.h
		//		则-clean ./hello/将被本工具解析，-include log.h将被clang库解析
		static FixedCompilationDatabase *CxxCleanOptionsParser::SplitCommandLine(int &argc, const char *const *argv, Twine directory = ".");

		// 根据vs工程文件里调整clang的参数
		bool AddCleanVsArgument(const VsProject &vs, ClangTool &tool) const;

		// 获取visual studio的安装路径
		std::string GetVsInstallDir() const;

		// 添加visual studio的额外的包含路径，因为clang库遗漏了一个包含路径会导致#include <atlcomcli.h>时找不到头文件
		void AddVsSearchDir(ClangTool &tool) const;

		// 解析单个的c++文件目标-src选项
		bool ParseSrcOption();

		// 解析清理目标-clean选项
		bool ParseCleanOption();

		// 解析日志打印级别-v选项
		bool ParseVerboseOption();

		// 解析清理模式-mode选项
		bool ParseCleanModeOption();

		CompilationDatabase &getCompilations() const {return *m_compilation;}

		std::vector<std::string>& getSourcePathList() {return m_sourceList;}

	private:
		std::unique_ptr<CompilationDatabase>	m_compilation;
		std::vector<std::string>				m_sourceList;
	};
}

#endif // _cxx_clean_h_