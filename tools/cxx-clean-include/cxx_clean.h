///<------------------------------------------------------------------------------
//< @file:   cxx_clean.h
//< @author: ������
//< @brief:  ʵ��clang����������﷨���йصĸ��ֻ�����
///<------------------------------------------------------------------------------

#pragma once

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Lex/Preprocessor.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Path.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Tooling/Tooling.h"
#include <clang/Tooling/CompilationDatabase.h>

using namespace std;
using namespace clang;
using namespace clang::driver;
using namespace clang::tooling;
using namespace llvm;
using namespace llvm::sys;
using namespace llvm::sys::path;
using namespace llvm::sys::fs;

class ParsingFile;
class VsProject;

// Ԥ����������#define��#if��#else��Ԥ����ؼ��ֱ�Ԥ����ʱʹ�ñ�Ԥ������
class CxxCleanPreprocessor : public PPCallbacks
{
public:
	explicit CxxCleanPreprocessor(ParsingFile *mainFile);

public:
	// �ļ��л�
	void FileChanged(SourceLocation loc, FileChangeReason reason, SrcMgr::CharacteristicKind FileType, FileID prevFileID = FileID()) override;

	// �ļ�������
	void FileSkippedWithFileID(FileID);

	// ����꣬��#if defined DEBUG
	void Defined(const Token &macroName, const MacroDefinition &definition, SourceRange range) override;

	// ����#define
	void MacroDefined(const Token &macroName, const MacroDirective *direct) override;

	// �걻#undef
	void MacroUndefined(const Token &macroName, const MacroDefinition &definition, const MacroDirective *Undef) override;

	// ����չ
	void MacroExpands(const Token &macroName, const MacroDefinition &definition, SourceRange range, const MacroArgs *args) override;

	// #ifdef
	void Ifdef(SourceLocation loc, const Token &macroName, const MacroDefinition &definition) override;

	// #ifndef
	void Ifndef(SourceLocation loc, const Token &macroName, const MacroDefinition &definition) override;

private:
	// ��ǰ���ڽ�����cpp�ļ���Ϣ
	ParsingFile *m_root;
};

// ͨ��ʵ��RecursiveASTVisitor���࣬�Զ������c++�����﷨��ʱ�Ĳ���
class CxxCleanASTVisitor : public RecursiveASTVisitor<CxxCleanASTVisitor>
{
public:
	explicit CxxCleanASTVisitor(ParsingFile *rootFile);

	// ���ʵ������
	bool VisitStmt(Stmt *s);

	// ���ʺ�������
	bool VisitFunctionDecl(FunctionDecl *f);

	// ����class��struct��union��enumʱ
	bool VisitCXXRecordDecl(CXXRecordDecl *r);

	// �����ֱ�������ʱ�ýӿڱ�����
	bool VisitVarDecl(VarDecl *var);

	// ���磺typedef int A;
	bool VisitTypedefDecl(clang::TypedefDecl *d);

	// ���磺namespace A{}
	bool VisitNamespaceDecl(clang::NamespaceDecl *d);

	// ���磺namespace s = std;
	bool VisitNamespaceAliasDecl(clang::NamespaceAliasDecl *d);

	// ���磺using namespace std;
	bool VisitUsingDirectiveDecl(clang::UsingDirectiveDecl *d);

	// ���磺using std::string;
	bool VisitUsingDecl(clang::UsingDecl *d);

	// ���ʳ�Ա����
	bool VisitFieldDecl(FieldDecl *decl);

	// ��������
	bool VisitCXXConstructorDecl(CXXConstructorDecl *decl);

	// �������
	bool VisitCXXConstructExpr(CXXConstructExpr *expr);

private:
	// ��ǰ���ڽ�����cpp�ļ���Ϣ
	ParsingFile*	m_root;
};

// ����������ʵ��ASTConsumer�ӿ����ڶ�ȡclang���������ɵ�ast�﷨��
class CxxCleanASTConsumer : public ASTConsumer
{
public:
	explicit CxxCleanASTConsumer(ParsingFile *rootFile);

	// ���ǣ�������������ķ���
	bool HandleTopLevelDecl(DeclGroupRef declgroup) override;

	// �����������ÿ��Դ�ļ���������һ�Σ����磬����һ��hello.cpp��#include�����ͷ�ļ���Ҳֻ�����һ�α�����
	void HandleTranslationUnit(ASTContext& context) override;

public:
	// ��ǰ���ڽ�����cpp�ļ���Ϣ
	ParsingFile*		m_root;

	// �����﷨��������
	CxxCleanASTVisitor	m_visitor;
};

// `TextDiagnosticPrinter`���Խ�������Ϣ��ӡ�ڿ���̨�ϣ�Ϊ�˵��Է��㽫����Ϊ����
class CxxcleanDiagnosticConsumer : public TextDiagnosticPrinter
{
public:
	explicit CxxcleanDiagnosticConsumer(DiagnosticOptions *diags);

	void Clear();

	void BeginSourceFile(const LangOptions &LO, const Preprocessor *PP) override;

	virtual void EndSourceFile() override;

	// ��һ��������ʱ������ô˺�����������������¼�������ʹ����
	virtual void HandleDiagnostic(DiagnosticsEngine::Level diagLevel, const Diagnostic &info) override;

	std::string			m_errorTip;
	raw_string_ostream	m_log;
};

// ����ClangTool���յ���ÿ��cppԴ�ļ�������newһ��CxxCleanAction
class CxxCleanAction : public ASTFrontendAction
{
public:
	CxxCleanAction()
		: m_root(nullptr)
	{}

	// ��ʼ�ļ�����
	bool BeginSourceFileAction(CompilerInstance &compiler) override;

	// �����ļ�����
	void EndSourceFileAction() override;

	// ���������﷨��������
	std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &compiler, StringRef file) override;

private:
	// ��ǰ���ڽ�����cpp�ļ���Ϣ
	ParsingFile	*m_root;
};

// �����ߵ������в���������������clang���CommonOptionParser��ʵ�ֶ���
class CxxCleanOptionsParser
{
public:
	CxxCleanOptionsParser() {}

	// ����ѡ����������������Ӧ�Ķ�����Ӧ��;�˳��򷵻�true�����򷵻�false
	bool ParseOptions(int &argc, const char **argv);

	// ��ִ���������в�����"--"�ָ���ǰ��������в������������߽���������������в�������clang�����
	// ע�⣺argc��������Ϊ"--"�ָ���ǰ�Ĳ�������
	// ���磺
	//		����ʹ��cxxclean -clean ./hello/ -- -include log.h
	//		��-clean ./hello/���������߽�����-include log.h����clang�����
	static FixedCompilationDatabase *CxxCleanOptionsParser::SplitCommandLine(int &argc, const char *const *argv, Twine directory = ".");

	// ���clang����
	void AddClangArgument(ClangTool &tool, const char *arg) const;

	// �������������clang����
	void AddClangArgumentByOption(ClangTool &tool) const;

	// ���ϵͳͷ�ļ�����·��
	void AddSystemHeaderSearchPath(ClangTool &tool, const char *path) const;

	// ����û�ͷ�ļ�����·��
	void AddHeaderSearchPath(ClangTool &tool, const char *path) const;

	// ����vs�����ļ�����clang�Ĳ���
	bool AddVsArgument(const VsProject &vs, ClangTool &tool) const;

	// ��ȡvisual studio�İ�װ·��
	std::string GetVsInstallDir() const;

	// ���visual studio�Ķ���İ���·������Ϊclang����©��һ������·���ᵼ��#include <atlcomcli.h>ʱ�Ҳ���ͷ�ļ�
	void AddVsSearchDir(ClangTool &tool) const;

	// ��������Ŀ��-cleanѡ��
	bool ParseCleanOption();

	// ������־��ӡ����-vѡ��
	bool ParseLogOption();

	CompilationDatabase &getCompilations() const {return *m_compilation;}

private:
	std::unique_ptr<CompilationDatabase> m_compilation;
};