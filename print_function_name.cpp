//------------------------------------------------------------------------------
// Tooling sample. Demonstrates:
//
// * How to write a simple source tool using libTooling.
// * How to use RecursiveASTVisitor to find interesting AST nodes.
// * How to use the Rewriter API to rewrite the source code.
//
// Eli Bendersky (eliben@gmail.com)
// This code is in the public domain
//------------------------------------------------------------------------------
#include <sstream>
#include <string>

#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/ASTConsumers.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;
using namespace clang::driver;
using namespace clang::tooling;

namespace tutorial
{
	static llvm::cl::OptionCategory ToolingSampleCategory("Tooling Sample");

	// By implementing RecursiveASTVisitor, we can specify which AST nodes
	// we're interested in by overriding relevant methods.
	class MyASTVisitor : public RecursiveASTVisitor<MyASTVisitor>
	{
	public:
		MyASTVisitor(Rewriter &R) : TheRewriter(R) {}

		bool VisitStmt(Stmt *s)
		{
			// Only care about If statements.
			if (isa<IfStmt>(s)) {
				IfStmt *IfStatement = cast<IfStmt>(s);
				Stmt *Then = IfStatement->getThen();

				TheRewriter.InsertText(Then->getLocStart(), "// the 'if' part\n", true,
				                       true);

				Stmt *Else = IfStatement->getElse();
				if (Else)
					TheRewriter.InsertText(Else->getLocStart(), "// the 'else' part\n",
					                       true, true);
			}

			return true;
		}

		bool VisitFunctionDecl(FunctionDecl *f)
		{
			// Only function definitions (with bodies), not declarations.
			if (f->hasBody()) {
				Stmt *FuncBody = f->getBody();

				// Type name as string
				QualType QT = f->getReturnType();
				std::string TypeStr = QT.getAsString();

				// Function name
				DeclarationName DeclName = f->getNameInfo().getName();
				std::string FuncName = DeclName.getAsString();

				// Add comment before
				std::stringstream SSBefore;
				SSBefore << "// Begin function " << FuncName << " returning " << TypeStr
				         << "\n";
				SourceLocation ST = f->getSourceRange().getBegin();
				TheRewriter.InsertText(ST, SSBefore.str(), true, true);

				// And after
				std::stringstream SSAfter;
				SSAfter << "\n// End function " << FuncName;
				ST = FuncBody->getLocEnd().getLocWithOffset(1);
				TheRewriter.InsertText(ST, SSAfter.str(), true, true);
			}

			return true;
		}

	private:
		Rewriter &TheRewriter;
	};

	// Implementation of the ASTConsumer interface for reading an AST produced
	// by the Clang parser.
	class MyASTConsumer : public ASTConsumer
	{
	public:
		MyASTConsumer(Rewriter &R) : Visitor(R) {}

		// Override the method that gets called for each parsed top-level
		// declaration.
		bool HandleTopLevelDecl(DeclGroupRef DR) override
		{
			for (DeclGroupRef::iterator b = DR.begin(), e = DR.end(); b != e; ++b) {
				// Traverse the declaration using our AST visitor.
				Visitor.TraverseDecl(*b);
				(*b)->dump();
			}
			return true;
		}

	private:
		MyASTVisitor Visitor;
	};

	// For each source file provided to the tool, a new FrontendAction is created.
	class MyFrontendAction : public ASTFrontendAction
	{
	public:
		MyFrontendAction() {}
		void EndSourceFileAction() override
		{
			SourceManager &SM = TheRewriter.getSourceMgr();
			llvm::errs() << "** EndSourceFileAction for: "
			             << SM.getFileEntryForID(SM.getMainFileID())->getName() << "\n";

			// Now emit the rewritten buffer.
			TheRewriter.getEditBuffer(SM.getMainFileID()).write(llvm::outs());
		}

		std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
		        StringRef file) override
		{
			llvm::errs() << "** Creating AST consumer for: " << file << "\n";
			TheRewriter.setSourceMgr(CI.getSourceManager(), CI.getLangOpts());
			return llvm::make_unique<MyASTConsumer>(TheRewriter);
		}

	private:
		Rewriter TheRewriter;
	};
}

namespace tool
{
	static llvm::cl::OptionCategory g_optionCategory("List Decl");

	// 通过实现RecursiveASTVisitor，对感兴趣的访问函数进行自定义
	class DeclASTVisitor : public RecursiveASTVisitor<DeclASTVisitor>
	{
	public:
		DeclASTVisitor(Rewriter &rewriter) : m_rewriter(rewriter) {}

		// 访问单条语句
		bool VisitStmt(Stmt *s)
		{
			// Only care about If statements.
			if (isa<IfStmt>(s)) {
				IfStmt *IfStatement = cast<IfStmt>(s);
				Stmt *Then = IfStatement->getThen();

				m_rewriter.InsertText(Then->getLocStart(), "// the 'if' part\n", true,
				                      true);

				Stmt *Else = IfStatement->getElse();
				if (Else)
					m_rewriter.InsertText(Else->getLocStart(), "// the 'else' part\n",
					                      true, true);
			} else if (isa<DeclStmt>(s)) {
				DeclStmt *decl = cast<DeclStmt>(s);

				m_rewriter.InsertText(decl->getLocStart(), "// DeclStmt begin\n", true, true);
				m_rewriter.InsertText(decl->getLocEnd(), "// DeclStmt end\n", true, true);
			}

			return true;
		}

		bool VisitFunctionDecl(FunctionDecl *f)
		{
			// 函数实现（含函数体），不仅仅是声明
			if (f->hasBody()) {
				Stmt *funcBody = f->getBody();

				// 函数的返回值
				QualType returnType = f->getReturnType();
				std::string typeStr = returnType.getAsString();

				// 函数名
				DeclarationName declName = f->getNameInfo().getName();
				std::string funcName = declName.getAsString();

				// 在前面加上注释
				std::stringstream before;
				before << "// Begin function " << funcName << " returning " << typeStr << "\n";
				SourceLocation location = f->getSourceRange().getBegin();
				m_rewriter.InsertText(location, before.str(), true, true);

				// 在后面加上注释
				std::stringstream after;
				after << "\n// End function " << funcName;
				location = funcBody->getLocEnd().getLocWithOffset(1);
				m_rewriter.InsertText(location, after.str(), true, true);
			}
			// 函数声明，不含实现
			else {
				Stmt *funcBody = f->getBody();

				// 函数名
				DeclarationName declName = f->getNameInfo().getName();
				std::string funcName = declName.getAsString();

				// 在前面加上注释
				std::stringstream before;
				before << "// begin function <" << funcName << "> only declaration\n";
				SourceLocation location = f->getSourceRange().getBegin();
				m_rewriter.InsertText(location, before.str(), true, true);

				// 在后面加上注释
				std::stringstream after;
				after << "\n// end function <" << funcName << "> only declaration";
				location = f->getLocEnd().getLocWithOffset(1);
				m_rewriter.InsertText(location, after.str(), true, true);
			}

			return true;
		}

	private:
		Rewriter &m_rewriter;
	};

	// 遍历声明，实现ASTConsumer接口用于读取clang解析器生成的ast语法树
	class ListDeclASTConsumer : public ASTConsumer
	{
	public:
		ListDeclASTConsumer(Rewriter &r) : m_visitor(r) {}

		// 覆盖：遍历最顶层声明的方法
		bool HandleTopLevelDecl(DeclGroupRef declgroup) override
		{
			for (DeclGroupRef::iterator itr = declgroup.begin(), end = declgroup.end(); itr != end; ++itr) {
				Decl *decl = *itr;

				// 使用ast访问器遍历声明
				m_visitor.TraverseDecl(decl);
				decl->dump();
			}

			return true;
		}

	private:
		DeclASTVisitor m_visitor;
	};

	// 对于tool接收到的每个源文件，都将new一个ListDeclAction
	class ListDeclAction : public ASTFrontendAction
	{
	public:
		ListDeclAction() {}

		void EndSourceFileAction() override
		{
			SourceManager &SM = m_rewriter.getSourceMgr();
			llvm::errs() << "** EndSourceFileAction for: "
			             << SM.getFileEntryForID(SM.getMainFileID())->getName()
			             << "\n";

			// Now emit the rewritten buffer.
			m_rewriter.getEditBuffer(SM.getMainFileID()).write(llvm::outs());
		}

		std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &compiler, StringRef file) override
		{
			llvm::errs() << "** Creating AST consumer for: " << file << "\n";
			m_rewriter.setSourceMgr(compiler.getSourceManager(), compiler.getLangOpts());

			return llvm::make_unique<ListDeclASTConsumer>(m_rewriter);
		}

	private:
		Rewriter m_rewriter;
	};
}