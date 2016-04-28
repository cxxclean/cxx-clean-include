///<------------------------------------------------------------------------------
//< @file  : cxx_clean.cpp
//< @author: 洪坤安
//< @date  : 2016年1月2日
//< @brief :
//< Copyright (c) 2016. All rights reserved.
///<------------------------------------------------------------------------------

#include <sstream>

#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Tooling/Tooling.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/Signals.h"
#include "llvm/Option/ArgList.h"
#include "clang/Basic/Version.h"
#include "clang/Driver/ToolChain.h"
#include "clang/Driver/Driver.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "lib/Driver/ToolChains.h"

#include "tool.h"
#include "vs_project.h"
#include "parsing_cpp.h"
#include "project.h"
#include "html_log.h"

using namespace clang;
using namespace clang::driver;
using namespace clang::tooling;
using namespace llvm;


using namespace std;
using namespace llvm::sys;
using namespace llvm::sys::path;
using namespace llvm::sys::fs;

namespace cxxcleantool
{
	static llvm::cl::OptionCategory g_optionCategory("cxx-clean-include category");

	// 预处理器，当#define、#if、#else等预处理关键字被预处理时使用本预处理器
	class CxxCleanPreprocessor : public PPCallbacks
	{
	public:
		CxxCleanPreprocessor(ParsingFile *mainFile)
			: m_main(mainFile)
		{
		}

	public:
		void FileChanged(SourceLocation loc, FileChangeReason reason,
		                 SrcMgr::CharacteristicKind FileType,
		                 FileID prevFileID = FileID()) override
		{
			// 注意：当为PPCallbacks::EnterFile时，prevFileID是无效的
			if (reason != PPCallbacks::EnterFile)
			{				
				return;
			}

			SourceManager &srcMgr	= m_main->GetSrcMgr();
			FileID curFileID		= srcMgr.getFileID(loc);

			if (curFileID.isInvalid())
			{
				return;
			}

			// 这里要注意，有的文件是会被FileChanged遗漏掉的，除非把HeaderSearch::ShouldEnterIncludeFile方法改为每次都返回true
			m_main->AddFile(curFileID);

			FileID parentID = srcMgr.getFileID(srcMgr.getIncludeLoc(curFileID));
			if (parentID.isInvalid())
			{
				return;
			}

			if (m_main->IsForceIncluded(curFileID))
			{
				parentID = srcMgr.getMainFileID();
			}

			if (curFileID == parentID)
			{
				return;
			}

			m_main->AddParent(curFileID, parentID);
			m_main->AddInclude(parentID, curFileID);
		}

		void FileSkipped(const FileEntry &SkippedFile,
		                 const Token &FilenameTok,
		                 SrcMgr::CharacteristicKind FileType) override
		{
			SourceManager &srcMgr		= m_main->GetSrcMgr();
			FileID curFileID = srcMgr.getFileID(FilenameTok.getLocation());

			m_main->AddFile(curFileID);
		}

		// 处理#include
		void InclusionDirective(SourceLocation HashLoc /*#的位置*/, const Token &includeTok,
		                        StringRef fileName, bool isAngled/*是否被<>包含，否则是被""包围*/, CharSourceRange filenameRange,
		                        const FileEntry *file, StringRef searchPath, StringRef relativePath, const clang::Module *Imported) override
		{
			// 注：当编译时采用-include<c++头文件>选项，但却又找不到该头文件时，将导致file无效，但这里不影响
			if (nullptr == file)
			{
				return;
			}

			FileID curFileID = m_main->GetSrcMgr().getFileID(HashLoc);
			if (curFileID.isInvalid())
			{
				return;
			}

			SourceRange range(HashLoc, filenameRange.getEnd());

			if (filenameRange.getBegin() == filenameRange.getEnd())
			{
				llvm::outs() << "InclusionDirective filenameRange.getBegin() == filenameRange.getEnd()\n";
			}

			m_main->AddIncludeLoc(filenameRange.getBegin(), range);
		}

		// 定义宏，如#if defined DEBUG
		void Defined(const Token &macroName, const MacroDefinition &definition, SourceRange range) override
		{
			m_main->UseMacro(macroName.getLocation(), definition, macroName);
		}

		// 处理#define
		void MacroDefined(const Token &macroName, const MacroDirective *direct) override
		{
		}

		// 宏被#undef
		void MacroUndefined(const Token &macroName, const MacroDefinition &definition) override
		{
			m_main->UseMacro(macroName.getLocation(), definition, macroName);
		}

		/// \brief Called by Preprocessor::HandleMacroExpandedIdentifier when a
		/// macro invocation is found.
		void MacroExpands(const Token &macroName,
		                  const MacroDefinition &definition,
		                  SourceRange range,
		                  const MacroArgs *args) override
		{
			m_main->UseMacro(range.getBegin(), definition, macroName, args);

			/*
			SourceManager &srcMgr = m_main->GetSrcMgr();
			if (srcMgr.isInMainFile(range.getBegin()))
			{
				SourceRange spellingRange(srcMgr.getSpellingLoc(range.getBegin()), srcMgr.getSpellingLoc(range.getEnd()));

				llvm::outs() << "<pre>text = " << m_main->GetSourceOfRange(range) << "</pre>\n";
				llvm::outs() << "<pre>macroName = " << macroName.getIdentifierInfo()->getName().str() << "</pre>\n";
			}
			*/
		}

		// #ifdef
		void Ifdef(SourceLocation loc, const Token &macroName, const MacroDefinition &definition) override
		{
			m_main->UseMacro(loc, definition, macroName);
		}

		// #ifndef
		void Ifndef(SourceLocation loc, const Token &macroName, const MacroDefinition &definition) override
		{
			m_main->UseMacro(loc, definition, macroName);
		}

		ParsingFile *m_main;
	};

	// 通过实现RecursiveASTVisitor基类，自定义访问c++抽象语法树时的操作
	class DeclASTVisitor : public RecursiveASTVisitor<DeclASTVisitor>
	{
	public:
		DeclASTVisitor(Rewriter &rewriter, ParsingFile *mainFile)
			: m_rewriter(rewriter), m_main(mainFile)
		{
		}

		void PrintStmt(Stmt *s)
		{
			SourceLocation loc = s->getLocStart();

			llvm::outs() << "<pre>source = " << m_main->DebugRangeText(s->getSourceRange()) << "</pre>\n";
			llvm::outs() << "<pre>";
			s->dump(llvm::outs());
			llvm::outs() << "</pre>";
		}

		// 访问单条语句
		bool VisitStmt(Stmt *s)
		{
			SourceLocation loc = s->getLocStart();

			/*
			if (m_rewriter.getSourceMgr().isInMainFile(loc))
			{
				llvm::outs() << "<pre>------------ VisitStmt ------------:</pre>\n";
				PrintStmt(s);
			}
			*/

			// 参见：http://clang.llvm.org/doxygen/classStmt.html
			if (isa<CastExpr>(s))
			{
				CastExpr *castExpr = cast<CastExpr>(s);

				QualType castType = castExpr->getType();
				m_main->UseQualType(loc, castType);

				/*
				if (m_rewriter.getSourceMgr().isInMainFile(loc))
				{
					llvm::outs() << "<pre>------------ CastExpr ------------:</pre>\n";
					llvm::outs() << "<pre>";
					llvm::outs() << castExpr->getCastKindName();
					llvm::outs() << "</pre>";
				}
				*/

				// 这里注意，CastExpr有一个getSubExpr()函数，表示子表达式，但此处不需要继续处理，因为子表达式将作为Stmt仍然会被VisitStmt访问到
			}
			else if (isa<CallExpr>(s))
			{
				CallExpr *callExpr = cast<CallExpr>(s);

				Decl *calleeDecl = callExpr->getCalleeDecl();
				if (NULL == calleeDecl)
				{
					return true;
				}

				if (isa<ValueDecl>(calleeDecl))
				{
					ValueDecl *namedDecl = cast<ValueDecl>(calleeDecl);
					m_main->UseValueDecl(loc, namedDecl);

					{
						//llvm::outs() << "<pre>------------ CallExpr: NamedDecl ------------:</pre>\n";
						//PrintStmt(s);
					}
				}
			}
			else if (isa<DeclRefExpr>(s))
			{
				DeclRefExpr *declRefExpr = cast<DeclRefExpr>(s);
				m_main->UseValueDecl(loc, declRefExpr->getDecl());
			}
			// 依赖当前范围取成员语句，例如：this->print();
			else if (isa<CXXDependentScopeMemberExpr>(s))
			{
				CXXDependentScopeMemberExpr *expr = cast<CXXDependentScopeMemberExpr>(s);
				m_main->UseQualType(loc, expr->getBaseType());
			}
			// this
			else if (isa<CXXThisExpr>(s))
			{
				CXXThisExpr *expr = cast<CXXThisExpr>(s);
				m_main->UseQualType(loc, expr->getType());
			}
			/// 结构体或union的成员，例如：X->F、X.F.
			else if (isa<MemberExpr>(s))
			{
				MemberExpr *memberExpr = cast<MemberExpr>(s);
				m_main->UseValueDecl(loc, memberExpr->getMemberDecl());
			}
			// delete语句
			else if (isa<CXXDeleteExpr>(s))
			{
				CXXDeleteExpr *expr = cast<CXXDeleteExpr>(s);
				m_main->UseQualType(loc, expr->getDestroyedType());
			}
			// 数组取元素语句，例如：a[0]或4[a]
			else if (isa<ArraySubscriptExpr>(s))
			{
				ArraySubscriptExpr *expr = cast<ArraySubscriptExpr>(s);
				m_main->UseQualType(loc, expr->getType());
			}
			// typeid语句，例如：typeid(int) or typeid(*obj)
			else if (isa<CXXTypeidExpr>(s))
			{
				CXXTypeidExpr *expr = cast<CXXTypeidExpr>(s);
				m_main->UseQualType(loc, expr->getType());
			}
			// 构造符
			else if (isa<CXXConstructExpr>(s))
			{
				CXXConstructExpr *cxxConstructExpr = cast<CXXConstructExpr>(s);
				CXXConstructorDecl *decl = cxxConstructExpr->getConstructor();
				if (nullptr == decl)
				{
					// llvm::errs() << "------------ CXXConstructExpr->getConstructor() = null ------------:\n";
					// s->dumpColor();
					return false;
				}

				m_main->UseValueDecl(loc, decl);
			}
			/*
			// 注意：下面这一大段很重要，用于以后日志跟踪

			// 类似于DeclRefExpr，要等实例化时才知道类型，比如：T::
			else if (isa<DependentScopeDeclRefExpr>(s))
			{
			}
			// return语句，例如：return 4;、return;
			else if (isa<ReturnStmt>(s))
			{
				// 对于ReturnStmt不需要多做处理，因为return后的语句仍然会被VisitStmt访问到
			}
			// 括号表达式，例如：(1)、(a + b)
			else if (isa<ParenExpr>(s))
			{
				// 对于ParenExpr不需要多做处理，因为括号内的语句仍然会被VisitStmt访问到
			}
			// 复合表达式，由不同表达式组合而成
			else if(isa<CompoundStmt>(s))
			{
				// 对于CompoundStmt不需要多做处理，因为其子语句仍然会被VisitStmt访问到
			}
			// 二元表达式，例如："x + y" or "x <= y"，若操作符未被重载，则类型是BinaryOperator，若操作符被重载，则类型将是CXXOperatorCallExpr
			else if (isa<BinaryOperator>(s))
			{
				// 对于BinaryOperator不需要多做处理，因为其2个子操作数将作为语句被VisitStmt访问到
				// 对于CXXOperatorCallExpr也不需要多做处理，因为其将作为CallExpr被VisitStmt访问到
			}
			// 一元表达式，例如：正号+，负号-，自增++，自减--,逻辑非！，按位取反~，取变量地址&，取指针所指值*等
			else if (isa<UnaryOperator>(s))
			{
				// 对于UnaryOperator不需要多做处理，因为其1个子操作数将作为语句被VisitStmt访问到
			}
			else if (isa<IntegerLiteral>(s))
			{
			}
			// 三元表达式，即条件表达式，例如： x ? y : z
			else if (isa<ConditionalOperator>(s))
			{
			}
			// for语句，例如：for(;;){ int i = 0; }
			else if (isa<ForStmt>(s))
			{
			}
			else if (isa<UnaryExprOrTypeTraitExpr>(s) || isa<InitListExpr>(s) || isa<MaterializeTemporaryExpr>(s))
			{
			}
			// 代表未知类型的构造函数，例如：下面的T(a1)
			// template<typename T, typename A1>
			// inline T make_a(const A1& a1)
			// {
			//     return T(a1);
			// }
			else if (isa<CXXUnresolvedConstructExpr>(s))
			{
			}
			// c++11的打包扩展语句，后面跟着...省略号，例如：下面的static_cast<Types&&>(args)...就是PackExpansionExpr
			// template<typename F, typename ...Types>
			// void forward(F f, Types &&...args)
			// {
			//     f(static_cast<Types&&>(args)...);
			// }
			else if (isa<PackExpansionExpr>(s))
			{
			}
			else if (isa<UnresolvedLookupExpr>(s) || isa<CXXBindTemporaryExpr>(s) || isa<ExprWithCleanups>(s))
			{
			}
			else if (isa<ParenListExpr>(s))
			{
			}
			else if (isa<DeclStmt>(s))
			{
			}
			else if (isa<IfStmt>(s) || isa<SwitchStmt>(s) || isa<CXXTryStmt>(s) || isa<CXXCatchStmt>(s) || isa<CXXThrowExpr>(s))
			{
			}
			else if (isa<StringLiteral>(s) || isa<CharacterLiteral>(s) || isa<CXXBoolLiteralExpr>(s) || isa<FloatingLiteral>(s))
			{
			}
			else if (isa<NullStmt>(s))
			{
			}
			else if (isa<CXXDefaultArgExpr>(s))
			{
			}
			//	代表c++的成员访问语句，访问可能是显式或隐式，例如：
			//	struct A
			//	{
			//		int a, b;
			//		int explicitAccess() { return this->a + this->A::b; }
			//		int implicitAccess() { return a + A::b; }
			//	};
			else if (isa<UnresolvedMemberExpr>(s))
			{
			}
			// new关键字
			else if (isa<CXXNewExpr>(s))
			{
				// 注：这里不需要处理，由之后的CXXConstructExpr处理
			}
			else
			{
				llvm::outs() << "<pre>------------ havn't support stmt ------------:</pre>\n";
				PrintStmt(s);
			}
			*/

			return true;
		}

		bool VisitFunctionDecl(FunctionDecl *f)
		{
			// 识别返回值类型
			{
				// 函数的返回值
				QualType returnType = f->getReturnType();
				m_main->UseVarType(f->getLocStart(), returnType);
			}

			// 识别函数参数
			{
				// 依次遍历参数，建立引用关系
				for (FunctionDecl::param_iterator itr = f->param_begin(), end = f->param_end(); itr != end; ++itr)
				{
					ParmVarDecl *vardecl = *itr;
					QualType vartype = vardecl->getType();
					m_main->UseVarType(f->getLocStart(), vartype);
				}
			}

			/*
				尝试找到函数的原型声明
				如：
					1. class Hello
					2.	{
					3.		void print();
					4.	}
					5. void Hello::print() {}

				则第5行是函数实现，其声明位于第3行
			*/
			{
				/*
					注意：非类函数可以反复进行声明，如
						int hello();
						int hello();
						int hello();

					这里仅关注最早的函数声明
				*/
				{
					FunctionDecl *oldestFunc = nullptr;

					for (FunctionDecl *prevFunc = f->getPreviousDecl(); prevFunc; prevFunc = prevFunc->getPreviousDecl())
					{
						oldestFunc = prevFunc;
					}

					if (nullptr == oldestFunc)
					{
						return true;
					}

					// 引用函数原型
					// m_main->on_use_decl(f->getLocStart(), oldestFunc);
				}

				// 是否属于某个类的成员函数
				if (isa<CXXMethodDecl>(f))
				{
					CXXMethodDecl *method = cast<CXXMethodDecl>(f);
					if (nullptr == method)
					{
						return false;
					}

					// 引用对应的方法
					m_main->UseFuncDecl(f->getLocStart(),	method);

					// 尝试找到该成员函数所属的struct/union/class.
					CXXRecordDecl *record = method->getParent();
					if (nullptr == record)
					{
						return false;
					}

					// 引用对应的struct/union/class.
					m_main->UseRecord(f->getLocStart(),	record);
				}
			}

			// ModifyFunc(f);
			return true;
		}

		// 访问class、struct、union、enum时
		bool VisitCXXRecordDecl(CXXRecordDecl *r)
		{
			if (!r->hasDefinition())
			{
				return true;
			}

			// 遍历所有基类
			for (CXXRecordDecl::base_class_iterator itr = r->bases_begin(), end = r->bases_end(); itr != end; ++itr)
			{
				CXXBaseSpecifier &base = *itr;
				m_main->UseQualType(r->getLocStart(), base.getType());
			}

			// 遍历成员变量（注意：不包括static成员变量，static成员变量将在VisitVarDecl中被访问到）
			for (CXXRecordDecl::field_iterator itr = r->field_begin(), end = r->field_end(); itr != end; ++itr)
			{
				FieldDecl *field = *itr;
				m_main->UseValueDecl(r->getLocStart(), field);
			}

			// 成员函数不需要在这里遍历，因为VisitFunctionDecl将会访问成员函数
			return true;
		}

		// 当发现变量定义时该接口被调用
		bool VisitVarDecl(VarDecl *var)
		{
			/*
				注意：
					本方法涵盖了
						1. 函数形参
						2. 非类成员的变量声明
						3. 类成员但含有static修饰符

					如:int a或 A a等，且已经涵盖了类似下面class A模板类的成员变量color这种情况

					template<typename T>
					class A
					{
						enum Color
						{
							// constants for file positioning options
							Green,
							Yellow,
							Blue,
							Red
						};

					private:
						static const Color g_color = (Color)2;
					};

					template<typename T>
					const typename A<T>::Color A<T>::g_color;
			*/

			// 引用变量的类型
			m_main->UseVarDecl(var->getLocStart(), var);

			// 类的static成员变量（支持模板类的成员）
			if (var->isCXXClassMember())
			{
				/*
					若为static成员变量的实现，则引用其声明（注：也可以用isStaticDataMember方法来判断var是否为static成员变量）
					例如：
							1. class Hello
							2.	{
							3.		static int g_num;
							4.	}
							5. static int Hello::g_num;

					则第5行的声明位于第3行处
				*/
				const VarDecl *prevVar = var->getPreviousDecl();
				m_main->UseVarDecl(var->getLocStart(), prevVar);
			}

			return true;
		}

		// 比如：typedef int A;
		bool VisitTypedefDecl(clang::TypedefDecl *d)
		{
			// llvm::errs() << "Visiting " << d->getDeclKindName() << " " << d->getName() << "\n";

			m_main->UseQualType(d->getLocStart(), d->getUnderlyingType());
			return true;
		}

		// 比如：namespace A{}
		bool VisitNamespaceDecl(clang::NamespaceDecl *d)
		{
			m_main->DeclareNamespace(d);
			return true;
		}

		// 比如：using namespace std;
		bool VisitUsingDirectiveDecl(clang::UsingDirectiveDecl *d)
		{
			m_main->UsingNamespace(d);
			return true;
		}

		// 比如：using std::string;
		bool VisitUsingDecl(clang::UsingDecl *d)
		{
			m_main->UsingXXX(d);
			return true;
		}

		// 访问成员变量
		bool VisitFieldDecl(FieldDecl *decl)
		{
			// m_main->use_var(decl->getLocStart(), decl->getType());
			return true;
		}

		bool VisitCXXConversionDecl(CXXConversionDecl *decl)
		{
			return true;
		}

		bool VisitCXXConstructorDecl(CXXConstructorDecl *decl)
		{
			for (auto itr = decl->init_begin(), end = decl->init_end(); itr != end; ++itr)
			{
				CXXCtorInitializer *initializer = *itr;
				if (initializer->isAnyMemberInitializer())
				{
					m_main->UseValueDecl(initializer->getSourceLocation(), initializer->getAnyMember());
				}
				else if (initializer->isBaseInitializer())
				{
					m_main->UseType(initializer->getSourceLocation(), initializer->getBaseClass());
				}
				else if (initializer->isDelegatingInitializer())
				{
					m_main->UseQualType(initializer->getSourceLocation(), initializer->getTypeSourceInfo()->getType());
				}
				else
				{ 
					// decl->dump();
				}
			}

			return true;
		}

	private:
		ParsingFile*	m_main;
		Rewriter&	m_rewriter;
	};

	// 遍历声明，实现ASTConsumer接口用于读取clang解析器生成的ast语法树
	class CxxCleanASTConsumer : public ASTConsumer
	{
	public:
		CxxCleanASTConsumer(Rewriter &r, ParsingFile *mainFile) : m_main(mainFile), m_visitor(r, mainFile) {}

		// 覆盖：遍历最顶层声明的方法
		bool HandleTopLevelDecl(DeclGroupRef declgroup) override
		{
			return true;
		}

		// 这个函数对于每个源文件仅被调用一次，比如，若有一个hello.cpp中#include了许多头文件，也只会调用一次本函数
		void HandleTranslationUnit(ASTContext& context) override
		{
			/*
			llvm::outs() << "<pre>------------ HandleTranslationUnit ------------:</pre>\n";
			llvm::outs() << "<pre>";
			context.getTranslationUnitDecl()->dump(llvm::outs());
			llvm::outs() << "</pre>";
			*/

			m_visitor.TraverseDecl(context.getTranslationUnitDecl());
		}

	public:
		ParsingFile*	m_main;
		DeclASTVisitor	m_visitor;
	};

	// `TextDiagnosticPrinter`可以将错误信息打印在控制台上，为了调试方便我从它派生而来
	class CxxcleanDiagnosticConsumer : public TextDiagnosticPrinter
	{
	public:
		CxxcleanDiagnosticConsumer(DiagnosticOptions *diags)
			: TextDiagnosticPrinter(g_log, diags)
		{
		}

		void Clear()
		{
			g_log.flush();
			g_errorTip.clear();
		}

		void BeginSourceFile(const LangOptions &LO, const Preprocessor *PP) override
		{
			TextDiagnosticPrinter::BeginSourceFile(LO, PP);

			Clear();

			NumErrors		= 0;
			NumWarnings		= 0;
		}

		virtual void EndSourceFile() override
		{
			TextDiagnosticPrinter::EndSourceFile();

			CompileErrorHistory &errHistory = ParsingFile::g_atFile->GetCompileErrorHistory();
			errHistory.m_errNum				= NumErrors;
		}

		// 是否是严重编译错误（暂时用不到）
		bool IsFatalError(int errid)
		{
			return (diag::DIAG_START_PARSE < errid && errid < diag::DIAG_START_AST);
		}

		// 当一个错误发生时，会调用此函数，在这个函数里记录编译错误和错误号
		virtual void HandleDiagnostic(DiagnosticsEngine::Level diagLevel, const Diagnostic &info)
		{
			TextDiagnosticPrinter::HandleDiagnostic(diagLevel, info);

			CompileErrorHistory &errHistory = ParsingFile::g_atFile->GetCompileErrorHistory();

			int errId = info.getID();
			if (errId == diag::fatal_too_many_errors)
			{
				errHistory.m_hasTooManyError = true;
			}

			if (diagLevel < DiagnosticIDs::Error)
			{
				Clear();
				return;
			}

			std::string tip = g_errorTip;
			Clear();

			int errNum = errHistory.m_errTips.size() + 1;

			if (diagLevel >= DiagnosticIDs::Fatal)
			{
				errHistory.m_fatalErrors.insert(errId);

				tip += strtool::get_text(cn_fatal_error_num_tip, strtool::itoa(errNum).c_str(), htmltool::get_number_html(errId).c_str());
			}
			else if (diagLevel >= DiagnosticIDs::Error)
			{
				tip += strtool::get_text(cn_error_num_tip, strtool::itoa(errNum).c_str(), htmltool::get_number_html(errId).c_str());
			}

			errHistory.m_errTips.push_back(tip);
		}

		static std::string			g_errorTip;
		static raw_string_ostream	g_log;
	};

	std::string			CxxcleanDiagnosticConsumer::g_errorTip;
	raw_string_ostream	CxxcleanDiagnosticConsumer::g_log(g_errorTip);

	// 对于ClangTool接收到的每个源文件，都将new一个CxxCleanAction
	class CxxCleanAction : public ASTFrontendAction
	{
	public:
		CxxCleanAction()
		{
		}

		bool BeginSourceFileAction(CompilerInstance &compiler, StringRef filename) override
		{
			++ProjectHistory::instance.g_printFileNo;

			if (ProjectHistory::instance.m_isFirst)
			{
				bool only1Step = !Project::instance.m_need2Step;
				if (only1Step)
				{
					llvm::errs() << "cleaning file: " << filename << " ...\n";
				}
				else
				{
					llvm::errs() << "step 1 of 2. analyze file: " << ProjectHistory::instance.g_printFileNo
					             << "/" << Project::instance.m_cpps.size() << ". " << filename << " ...\n";
				}
			}
			else
			{
				llvm::errs() << "step 2 of 2. cleaning file: " << ProjectHistory::instance.g_printFileNo
				             << "/" << Project::instance.m_cpps.size() << ". " << filename << " ...\n";
			}

			return true;
		}

		void EndSourceFileAction() override
		{
			SourceManager &srcMgr	= m_rewriter.getSourceMgr();
			ASTConsumer &consumer	= this->getCompilerInstance().getASTConsumer();
			CxxCleanASTConsumer *c	= (CxxCleanASTConsumer*)&consumer;

			// llvm::errs() << "** EndSourceFileAction for: " << srcMgr.getFileEntryForID(srcMgr.getMainFileID())->getName() << "\n";
			// CompilerInstance &compiler = this->getCompilerInstance();

			c->m_main->GenerateResult();

			if (ProjectHistory::instance.m_isFirst)
			{
				ProjectHistory::instance.AddFile(c->m_main);
				c->m_main->Print();
			}

			bool can_clean	 = false;
			can_clean		|= Project::instance.m_onlyHas1File;;
			can_clean		|= !Project::instance.m_isDeepClean;
			can_clean		|= !ProjectHistory::instance.m_isFirst;

			if (can_clean)
			{
				c->m_main->Clean();
			}

			delete c->m_main;
		}

		std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &compiler, StringRef file) override
		{
			m_rewriter.setSourceMgr(compiler.getSourceManager(), compiler.getLangOpts());

			ParsingFile *parsingCpp	= new ParsingFile(m_rewriter, compiler);
			parsingCpp->Init();

			HtmlLog::instance.m_newDiv.Clear();

			compiler.getPreprocessor().addPPCallbacks(llvm::make_unique<CxxCleanPreprocessor>(parsingCpp));
			return llvm::make_unique<CxxCleanASTConsumer>(m_rewriter, parsingCpp);
		}

	private:
		void PrintIncludes()
		{
			llvm::outs() << "\n////////////////////////////////\n";
			llvm::outs() << "print fileinfo_iterator include:\n";
			llvm::outs() << "////////////////////////////////\n";

			SourceManager &srcMgr = m_rewriter.getSourceMgr();

			typedef SourceManager::fileinfo_iterator fileinfo_iterator;

			fileinfo_iterator beg = srcMgr.fileinfo_begin();
			fileinfo_iterator end = srcMgr.fileinfo_end();

			for (fileinfo_iterator itr = beg; itr != end; ++itr)
			{
				const FileEntry *fileEntry = itr->first;
				SrcMgr::ContentCache *cache = itr->second;

				//printf("#include = %s\n", fileEntry->getName());
				llvm::outs() << "    #include = "<< fileEntry->getName()<< "\n";
			}
		}

		void PrintTopIncludes()
		{
			llvm::outs() << "\n////////////////////////////////\n";
			llvm::outs() << "print top getLocalSLocEntry include:\n";
			llvm::outs() << "////////////////////////////////\n";

			SourceManager &srcMgr = m_rewriter.getSourceMgr();
			int include_size = srcMgr.local_sloc_entry_size();

			for (int i = 1; i < include_size; i++)
			{
				const SrcMgr::SLocEntry &locEntry = srcMgr.getLocalSLocEntry(i);
				if (!locEntry.isFile())
				{
					continue;
				}

				llvm::outs() << "    #include = "<< srcMgr.getFilename(locEntry.getFile().getIncludeLoc()) << "\n";
			}
		}

	private:
		Rewriter m_rewriter;
	};
}

using namespace cxxcleantool;

static cl::opt<string>	g_cleanOption	("clean",
        cl::desc("you can use this option to:\n"
                 "    1. clean directory, for example: -clean ../hello/\n"
                 "    2. clean visual studio project(version 2005 or upper): for example: -clean ./hello.vcproj or -clean ./hello.vcxproj\n"),
        cl::cat(g_optionCategory));

static cl::opt<string>	g_src			("src",
        cl::desc("target c++ source file to be cleaned, must have valid path, if this option was valued, only the target c++ file will be cleaned\n"
                 "this option can be used with -clean option, for example, you can use: \n"
                 "    cxxclean -clean hello.vcproj -src ./hello.cpp\n"
                 "it will clean hello.cpp and using hello.vcproj configuration"),
        cl::cat(g_optionCategory));

static cl::opt<bool>	g_noWriteOption	("no",
        cl::desc("means no overwrite, if this option is checked, all of the c++ file will not be changed"),
        cl::cat(g_optionCategory));

static cl::opt<bool>	g_onlyCleanCpp	("onlycpp",
        cl::desc("only allow clean cpp file(cpp, cc, cxx), don't clean the header file(h, hxx, hh)"),
        cl::cat(g_optionCategory));

static cl::opt<bool>	g_printVsConfig	("print-vs",
        cl::desc("print vs configuration, for example, print header search path, print c++ file list, print predefine macro and so on"),
        cl::cat(g_optionCategory));

static cl::opt<bool>	g_printProject	("print-project",
        cl::desc("print project configuration, for example, print c++ source list to be cleaned, print allowed clean directory or allowed clean c++ file list, and so on"),
        cl::cat(g_optionCategory));

static cl::opt<int>		g_verbose		("v",
        cl::desc("verbose level, level can be 1 ~ 5, default is 1, higher level will print more detail"),
        cl::NotHidden);

// cxx-clean-include命令行参数解析器，参照clang库的CommonOptionParser类实现而成
class CxxCleanOptionsParser
{
public:
	CxxCleanOptionsParser() {}

	// 解析选项并将解析结果存入相应的对象，若应中途退出则返回true，否则返回false
	bool ParseOptions(int &argc, const char **argv, llvm::cl::OptionCategory &category)
	{
		m_compilation.reset(CxxCleanOptionsParser::SplitCommandLine(argc, argv));

		cl::ParseCommandLineOptions(argc, argv, nullptr);

		if (!ParseVerboseOption())
		{
			return false;
		}

		if (!ParseSrcOption())
		{
			return false;
		}

		if (!ParseCleanOption())
		{
			return false;
		}

		HtmlLog::instance.BeginLog();

		if (g_printVsConfig)
		{
			Vsproject::instance.Print();
			return false;
		}

		if (g_printProject)
		{
			Project::instance.Print();
			return false;
		}

		if (Project::instance.m_cpps.empty())
		{
			llvm::errs() << "cxx-clean-include: \n    try use -help argument to see more information.\n";
			return 0;
		}

		if (Project::instance.m_verboseLvl >= VerboseLvl_2)
		{
			Project::instance.Print();
		}

		return true;
	}

	// 拆分传入的命令行参数，"--"分隔符前面的命令行参数将被cxx-clean-include解析，后面的命令行参数将被clang库解析
	// 注意：argc将被更改为"--"分隔符前的参数个数
	// 例如：
	//		假设使用cxx-clean-include -clean ./hello/ -- -include log.h
	//		则-clean ./hello/将被本工具解析，-include log.h将被clang库解析
	static FixedCompilationDatabase *CxxCleanOptionsParser::SplitCommandLine(int &argc, const char *const *argv, Twine directory = ".")
	{
		const char *const *doubleDash = std::find(argv, argv + argc, StringRef("--"));
		if (doubleDash == argv + argc)
		{
			return new FixedCompilationDatabase(directory, std::vector<std::string>());
		}

		std::vector<const char *> commandLine(doubleDash + 1, argv + argc);
		argc = doubleDash - argv;

		std::vector<std::string> strippedArgs;
		strippedArgs.reserve(commandLine.size());

		for (const char * arg : commandLine)
		{
			strippedArgs.push_back(arg);
		}

		return new FixedCompilationDatabase(directory, strippedArgs);
	}

	// 根据vs工程文件里调整clang的参数
	bool AddCleanVsArgument(const Vsproject &vs, ClangTool &tool)
	{
		if (vs.m_configs.empty())
		{
			return false;
		}

		const VsConfiguration &vsconfig = vs.m_configs[0];

		for (const std::string &dir	: vsconfig.searchDirs)
		{
			std::string arg			= "-I" + dir;

			ArgumentsAdjuster argAdjuster = getInsertArgumentAdjuster(arg.c_str(), ArgumentInsertPosition::BEGIN);
			tool.appendArgumentsAdjuster(argAdjuster);
		}

		for (const std::string &force_include : vsconfig.forceIncludes)
		{
			std::string arg						= "-include" + force_include;

			ArgumentsAdjuster argAdjuster = getInsertArgumentAdjuster(arg.c_str(), ArgumentInsertPosition::BEGIN);
			tool.appendArgumentsAdjuster(argAdjuster);
		}

		for (auto predefine : vsconfig.preDefines)
		{
			std::string arg = "-D" + predefine;

			ArgumentsAdjuster argAdjuster = getInsertArgumentAdjuster(arg.c_str(), ArgumentInsertPosition::BEGIN);
			tool.appendArgumentsAdjuster(argAdjuster);
		}

		tool.appendArgumentsAdjuster(getInsertArgumentAdjuster("-fms-extensions", ArgumentInsertPosition::BEGIN));
		tool.appendArgumentsAdjuster(getInsertArgumentAdjuster("-fms-compatibility", ArgumentInsertPosition::BEGIN));
		tool.appendArgumentsAdjuster(getInsertArgumentAdjuster("-fms-compatibility-version=18", ArgumentInsertPosition::BEGIN));

		AddVsSearchDir(tool);

		pathtool::cd(vs.m_project_dir.c_str());
		return true;
	}

	// 获取visual studio的安装路径
	std::string GetVsInstallDir()
	{
		std::string vsInstallDir;

#if defined(LLVM_ON_WIN32)
		DiagnosticsEngine engine(
		    IntrusiveRefCntPtr<clang::DiagnosticIDs>(new DiagnosticIDs()), nullptr,
		    nullptr, false);

		clang::driver::Driver d("", "", engine);
		llvm::opt::InputArgList args(nullptr, nullptr);
		toolchains::MSVCToolChain mscv(d, Triple(), args);

		mscv.getVisualStudioInstallDir(vsInstallDir);

		if (!pathtool::exist(vsInstallDir))
		{
			const std::string maybeList[] =
			{
				"C:\\Program Files\\Microsoft Visual Studio 12.0",
				"C:\\Program Files\\Microsoft Visual Studio 11.0",
				"C:\\Program Files\\Microsoft Visual Studio 10.0",
				"C:\\Program Files\\Microsoft Visual Studio 9.0",
				"C:\\Program Files\\Microsoft Visual Studio 8",

				"C:\\Program Files (x86)\\Microsoft Visual Studio 12.0",
				"C:\\Program Files (x86)\\Microsoft Visual Studio 11.0",
				"C:\\Program Files (x86)\\Microsoft Visual Studio 10.0",
				"C:\\Program Files (x86)\\Microsoft Visual Studio 9.0",
				"C:\\Program Files (x86)\\Microsoft Visual Studio 8"
			};

			for (const std::string &maybePath : maybeList)
			{
				if (pathtool::exist(maybePath))
				{
					return maybePath;
				}
			}
		}
#endif

		return vsInstallDir;
	}

	// 添加visual studio的额外的包含路径，因为clang库遗漏了一个包含路径会导致#include <atlcomcli.h>时找不到头文件
	void AddVsSearchDir(ClangTool &tool)
	{
		if (Vsproject::instance.m_configs.empty())
		{
			return;
		}

		std::string vsInstallDir = GetVsInstallDir();
		if (vsInstallDir.empty())
		{
			return;
		}

		const char* search[] =
		{
			"VC\\atlmfc\\include",
			"VC\\PlatformSDK\\Include",
			"VC\\include"
		};

		for (const char *try_path : search)
		{
			std::string path = pathtool::append_path(vsInstallDir.c_str(), try_path);
			if (!pathtool::exist(path))
			{
				continue;
			}

			std::string arg = "-I" + path;
			tool.appendArgumentsAdjuster(getInsertArgumentAdjuster(arg.c_str(), ArgumentInsertPosition::BEGIN));
		}
	}

	// 解析-src选项
	bool ParseSrcOption()
	{
		std::string src = g_src;
		if (src.empty())
		{
			return true;
		}

		if (pathtool::exist(src))
		{
			HtmlLog::instance.SetHtmlTitle(strtool::get_text(cn_cpp_file, src.c_str()));
			HtmlLog::instance.SetBigTitle(strtool::get_text(cn_cpp_file, htmltool::get_file_html(src).c_str()));
			m_sourceList.push_back(src);
		}
		else
		{
			bool ok = pathtool::ls(src, m_sourceList);
			if (!ok)
			{
				llvm::errs() << "error: parse argument -src " << src << " failed, not found the c++ files.\n";
				return false;
			}

			HtmlLog::instance.SetHtmlTitle(strtool::get_text(cn_folder, src.c_str()));
			HtmlLog::instance.SetBigTitle(strtool::get_text(cn_folder, htmltool::get_file_html(src).c_str()));
		}

		return true;
	}

	// 解析-clean选项
	bool ParseCleanOption()
	{
		Vsproject &vs				= Vsproject::instance;
		Project &project			= Project::instance;

		project.m_isDeepClean		= !g_onlyCleanCpp;
		project.m_isOverWrite		= !g_noWriteOption;
		project.m_workingDir		= pathtool::get_current_path();
		project.m_cpps				= m_sourceList;

		std::string clean_option	= g_cleanOption;

		if (!clean_option.empty())
		{
			const string ext = strtool::get_ext(clean_option);
			if (ext == "vcproj" || ext == "vcxproj")
			{
				if (!vs.ParseVs(clean_option))
				{
					llvm::errs() << "parse vs project<" << clean_option << "> failed!\n";
					return false;
				}

				// llvm::outs() << "parse vs project<" << clean_option << "> succeed!\n";

				HtmlLog::instance.SetHtmlTitle(strtool::get_text(cn_project, clean_option.c_str()));
				HtmlLog::instance.SetBigTitle(strtool::get_text(cn_project, htmltool::get_file_html(clean_option).c_str()));

				vs.TakeSourceListTo(project);
			}
			else if (llvm::sys::fs::is_directory(clean_option))
			{
				std::string folder = pathtool::get_absolute_path(clean_option.c_str());

				if (!strtool::end_with(folder, "/"))
				{
					folder += "/";
				}

				Project::instance.m_allowCleanDir = folder;

				if (project.m_cpps.empty())
				{
					bool ok = pathtool::ls(folder, project.m_cpps);
					if (!ok)
					{
						llvm::errs() << "error: -clean " << folder << " failed!\n";
						return false;
					}

					HtmlLog::instance.SetHtmlTitle(strtool::get_text(cn_project, clean_option.c_str()));
					HtmlLog::instance.SetBigTitle(strtool::get_text(cn_project, htmltool::get_file_html(clean_option).c_str()));
				}
			}
			else
			{
				llvm::errs() << "unsupport parsed <" << clean_option << ">!\n";
				return false;
			}
		}
		else
		{
			project.GenerateAllowCleanList();
		}

		// 移除非c++后缀的源文件
		project.Fix();

		if (project.m_cpps.size() == 1)
		{
			project.m_onlyHas1File = true;
		}

		Project::instance.m_need2Step = Project::instance.m_isDeepClean && !Project::instance.m_onlyHas1File && Project::instance.m_isOverWrite;
		return true;
	}

	// 解析-v选项
	bool ParseVerboseOption()
	{
		Project::instance.m_verboseLvl = g_verbose;

		if (g_verbose < 0 || g_verbose > VerboseLvl_Max)
		{
			return false;
		}

		if (0 == g_verbose)
		{
			Project::instance.m_verboseLvl = 1;
		}

		return true;
	}

	/// Returns a reference to the loaded compilations database.
	CompilationDatabase &getCompilations() {return *m_compilation;}

	/// Returns a list of source file paths to process.
	std::vector<std::string>& getSourcePathList() {return m_sourceList;}

	static const char *const HelpMessage;

public:
	bool									m_exit;

private:
	std::unique_ptr<CompilationDatabase>	m_compilation;
	std::vector<std::string>				m_sourceList;
};

static cl::extrahelp MoreHelp(
    "\n"
    "\nExample Usage:"
    "\n"
    "\n    there are 2 ways to use cxx-clean-include"
    "\n"
    "\n    1. if your project is msvc project(visual studio 2005 and upper)"
    "\n        if you want to clean the whole vs project: you can use:"
    "\n            cxxclean -clean hello.vcproj -src hello.cpp"
    "\n"
    "\n        if your only want to clean a single c++ file, and use the vs configuration, you can use:"
    "\n            cxxclean -clean hello.vcproj -src hello.cpp"
    "\n"
    "\n    2. if all your c++ file is in a directory\n"
    "\n        if you wan to clean all c++ file in the directory, you can use:"
    "\n            cxxclean -clean ./hello"
    "\n"
    "\n        if your only want to clean a single c++ file, you can use:"
    "\n            cxxclean -clean ./hello -src hello.cpp"
);

static void PrintVersion()
{
	llvm::outs() << "cxx-clean-include version is 1.0\n";
	llvm::outs() << clang::getClangToolFullVersion("clang lib which cxx-clean-include using is") << '\n';
}

int main(int argc, const char **argv)
{
	llvm::sys::PrintStackTraceOnErrorSignal();
	cl::HideUnrelatedOptions(g_optionCategory);	// 使用-help选项时，仅打印cxx-clean-include工具的选项

	llvm::InitializeNativeTarget();
	llvm::InitializeNativeTargetAsmParser();	// 支持解析asm

	cl::SetVersionPrinter(PrintVersion);

	CxxCleanOptionsParser optionParser;

	bool ok = optionParser.ParseOptions(argc, argv, cxxcleantool::g_optionCategory);
	if (!ok)
	{
		return 0;
	}

	ClangTool tool(optionParser.getCompilations(), Project::instance.m_cpps);
	tool.clearArgumentsAdjusters();
	tool.appendArgumentsAdjuster(getClangSyntaxOnlyAdjuster());

	optionParser.AddCleanVsArgument(Vsproject::instance, tool);

	tool.appendArgumentsAdjuster(getInsertArgumentAdjuster("-fcxx-exceptions",			ArgumentInsertPosition::BEGIN));
	tool.appendArgumentsAdjuster(getInsertArgumentAdjuster("-Winvalid-source-encoding", ArgumentInsertPosition::BEGIN));
	tool.appendArgumentsAdjuster(getInsertArgumentAdjuster("-Wdeprecated-declarations", ArgumentInsertPosition::BEGIN));
	tool.appendArgumentsAdjuster(getInsertArgumentAdjuster("-nobuiltininc",				ArgumentInsertPosition::BEGIN));	// 禁止使用clang内置的头文件
	tool.appendArgumentsAdjuster(getInsertArgumentAdjuster("-w",						ArgumentInsertPosition::BEGIN));	// 禁用警告
	tool.appendArgumentsAdjuster(getInsertArgumentAdjuster("-ferror-limit=5",			ArgumentInsertPosition::BEGIN));	// 限制单个cpp产生的编译错误数，超过则不再编译

	DiagnosticOptions diagnosticOptions;
	diagnosticOptions.ShowOptionNames = 1;
	tool.setDiagnosticConsumer(new CxxcleanDiagnosticConsumer(&diagnosticOptions));

	// 第1遍对每个文件进行分析，然后汇总并打印统计日志
	std::unique_ptr<FrontendActionFactory> factory = newFrontendActionFactory<cxxcleantool::CxxCleanAction>();
	tool.run(factory.get());

	// 打印统计日志
	ProjectHistory::instance.Print();

	// 第2遍才开始清理，第2遍就不打印html日志了
	if (Project::instance.m_need2Step)
	{
		ProjectHistory::instance.m_isFirst		= false;
		ProjectHistory::instance.g_printFileNo	= 0;
		tool.run(factory.get());
	}

	return 0;
}