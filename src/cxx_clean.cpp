//<------------------------------------------------------------------------------
//< @file  : cxx_clean.cpp
//< @author: 洪坤安
//< @brief : 实现clang库中与抽象语法树有关的各种基础类
//< Copyright (c) 2016. All rights reserved.
///<------------------------------------------------------------------------------

#include "cxx_clean.h"

#include <sstream>

#include "clang/Lex/HeaderSearch.h"
#include "llvm/Option/ArgList.h"
#include "clang/Basic/Version.h"
#include "clang/Driver/ToolChain.h"
#include "clang/Driver/Driver.h"
#include "clang/Parse/ParseDiagnostic.h"
#include "lib/Driver/ToolChains.h"

#include "tool.h"
#include "vs.h"
#include "parser.h"
#include "project.h"
#include "html_log.h"

namespace cxxclean
{
	// 预处理器，当#define、#if、#else等预处理关键字被预处理时使用本预处理器
	CxxCleanPreprocessor::CxxCleanPreprocessor(ParsingFile *rootFile)
		: m_root(rootFile)
	{
	}

	// 文件切换
	void CxxCleanPreprocessor::FileChanged(SourceLocation loc, FileChangeReason reason,
	                                       SrcMgr::CharacteristicKind FileType,
	                                       FileID prevFileID /* = FileID() */)
	{
		// 注意：当为PPCallbacks::EnterFile时，prevFileID是无效的
		if (reason != PPCallbacks::EnterFile)
		{
			return;
		}

		SourceManager &srcMgr	= m_root->GetSrcMgr();
		FileID curFileID		= srcMgr.getFileID(loc);

		if (curFileID.isInvalid())
		{
			return;
		}

		// 这里要注意，有的文件是会被FileChanged遗漏掉的，除非把HeaderSearch::ShouldEnterIncludeFile方法改为每次都返回true
		m_root->AddFile(curFileID);

		FileID parentID = srcMgr.getFileID(srcMgr.getIncludeLoc(curFileID));
		if (parentID.isInvalid())
		{
			return;
		}

		if (m_root->IsForceIncluded(curFileID))
		{
			parentID = srcMgr.getMainFileID();
		}

		if (curFileID == parentID)
		{
			return;
		}

		m_root->AddParent(curFileID, parentID);
		m_root->AddInclude(parentID, curFileID);
	}

	// 文件被跳过
	void CxxCleanPreprocessor::FileSkipped(const FileEntry &SkippedFile,
	                                       const Token &FilenameTok,
	                                       SrcMgr::CharacteristicKind FileType)
	{
		SourceManager &srcMgr		= m_root->GetSrcMgr();
		FileID curFileID = srcMgr.getFileID(FilenameTok.getLocation());

		m_root->AddFile(curFileID);
	}

	// 处理#include
	void CxxCleanPreprocessor::InclusionDirective(SourceLocation HashLoc /*#的位置*/, const Token &includeTok,
	        StringRef fileName, bool isAngled/*是否被<>包含，否则是被""包围*/, CharSourceRange filenameRange,
	        const FileEntry *file, StringRef searchPath, StringRef relativePath, const clang::Module *Imported)
	{
		// 注：当编译时采用-include<c++头文件>选项，但却又找不到该头文件时，将导致file无效，但这里不影响
		if (nullptr == file)
		{
			return;
		}

		FileID curFileID = m_root->GetSrcMgr().getFileID(HashLoc);
		if (curFileID.isInvalid())
		{
			return;
		}

		SourceRange range(HashLoc, filenameRange.getEnd());

		if (filenameRange.getBegin() == filenameRange.getEnd())
		{
			cxx::log() << "InclusionDirective filenameRange.getBegin() == filenameRange.getEnd()\n";
		}

		m_root->AddIncludeLoc(filenameRange.getBegin(), range);
	}

	// 定义宏，如#if defined DEBUG
	void CxxCleanPreprocessor::Defined(const Token &macroName, const MacroDefinition &definition, SourceRange range)
	{
		m_root->UseMacro(macroName.getLocation(), definition, macroName);
	}

	// 处理#define
	void CxxCleanPreprocessor::MacroDefined(const Token &macroName, const MacroDirective *direct)
	{
	}

	// 宏被#undef
	void CxxCleanPreprocessor::MacroUndefined(const Token &macroName, const MacroDefinition &definition)
	{
		m_root->UseMacro(macroName.getLocation(), definition, macroName);
	}

	// 宏扩展
	void CxxCleanPreprocessor::MacroExpands(const Token &macroName,
	                                        const MacroDefinition &definition,
	                                        SourceRange range,
	                                        const MacroArgs *args)
	{
		m_root->UseMacro(range.getBegin(), definition, macroName, args);

		/*
		SourceManager &srcMgr = m_root->GetSrcMgr();
		if (srcMgr.isInMainFile(range.getBegin()))
		{
			SourceRange spellingRange(srcMgr.getSpellingLoc(range.getBegin()), srcMgr.getSpellingLoc(range.getEnd()));

			cxx::log() << "<pre>text = " << m_root->GetSourceOfRange(range) << "</pre>\n";
			cxx::log() << "<pre>macroName = " << macroName.getIdentifierInfo()->getName().str() << "</pre>\n";
		}
		*/
	}

	// #ifdef
	void CxxCleanPreprocessor::Ifdef(SourceLocation loc, const Token &macroName, const MacroDefinition &definition)
	{
		m_root->UseMacro(loc, definition, macroName);
	}

	// #ifndef
	void CxxCleanPreprocessor::Ifndef(SourceLocation loc, const Token &macroName, const MacroDefinition &definition)
	{
		m_root->UseMacro(loc, definition, macroName);
	}

	CxxCleanASTVisitor::CxxCleanASTVisitor(ParsingFile *rootFile)
		: m_root(rootFile)
	{
	}

	// 用于调试：打印语句的信息
	void CxxCleanASTVisitor::PrintStmt(Stmt *s)
	{
		SourceLocation loc = s->getLocStart();

		cxx::log() << "<pre>source = " << m_root->DebugRangeText(s->getSourceRange()) << "</pre>\n";
		cxx::log() << "<pre>";
		s->dump(cxx::log());
		cxx::log() << "</pre>";
	}

	// 访问单条语句
	bool CxxCleanASTVisitor::VisitStmt(Stmt *s)
	{
		SourceLocation loc = s->getLocStart();

		/*
		if (m_root->GetSrcMgr().isInMainFile(loc))
		{
			cxx::log() << "<pre>------------ VisitStmt ------------:</pre>\n";
			PrintStmt(s);
		}
		*/

		// 参见：http://clang.llvm.org/doxygen/classStmt.html
		if (isa<CastExpr>(s))
		{
			CastExpr *castExpr = cast<CastExpr>(s);

			QualType castType = castExpr->getType();

			CastKind castKind = castExpr->getCastKind();

			switch (castKind)
			{
			// 父类与子类互相转换时，需要特殊处理
			case CK_UncheckedDerivedToBase :
			case CK_BaseToDerived:
			case CK_BaseToDerivedMemberPointer:
			case CK_DerivedToBase:
			case CK_DerivedToBaseMemberPointer:
			case CK_Dynamic:
				m_root->UseQualType(loc, castType);
				m_root->UseQualType(loc, castExpr->getSubExpr()->getType());
				break;

			default:
				m_root->UseVarType(loc, castType);
			}

			/*
			if (m_root->GetSrcMgr().isInMainFile(loc))
			{
				cxx::log() << "<pre>------------ CastExpr ------------:</pre>\n";
				cxx::log() << "<pre>";
				cxx::log() << castExpr->getCastKindName();
				cxx::log() << "</pre>";
			}
			*/

		}
		else if (isa<CXXMemberCallExpr>(s))
		{
			CXXMemberCallExpr *callExpr = cast<CXXMemberCallExpr>(s);
			m_root->UseFuncDecl(loc, callExpr->getMethodDecl());
			m_root->UseRecord(loc, callExpr->getRecordDecl());
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
				ValueDecl *valueDecl = cast<ValueDecl>(calleeDecl);
				m_root->UseValueDecl(loc, valueDecl);

				{
					//cxx::log() << "<pre>------------ CallExpr: NamedDecl ------------:</pre>\n";
					//PrintStmt(s);
				}
			}
		}
		else if (isa<DeclRefExpr>(s))
		{
			DeclRefExpr *declRefExpr = cast<DeclRefExpr>(s);

			if (declRefExpr->hasQualifier())
			{
				m_root->UseQualifier(loc, declRefExpr->getQualifier());
			}

			m_root->UseValueDecl(loc, declRefExpr->getDecl());
		}
		// 依赖当前范围取成员语句，例如：this->print();
		else if (isa<CXXDependentScopeMemberExpr>(s))
		{
			CXXDependentScopeMemberExpr *expr = cast<CXXDependentScopeMemberExpr>(s);
			m_root->UseQualType(loc, expr->getBaseType());
		}
		// this
		else if (isa<CXXThisExpr>(s))
		{
			CXXThisExpr *expr = cast<CXXThisExpr>(s);
			m_root->UseQualType(loc, expr->getType());
		}
		/// 结构体或union的成员，例如：X->F、X.F.
		else if (isa<MemberExpr>(s))
		{
			MemberExpr *memberExpr = cast<MemberExpr>(s);
			m_root->UseValueDecl(loc, memberExpr->getMemberDecl());
		}
		// delete语句
		else if (isa<CXXDeleteExpr>(s))
		{
			CXXDeleteExpr *expr = cast<CXXDeleteExpr>(s);
			m_root->UseQualType(loc, expr->getDestroyedType());
		}
		// 数组取元素语句，例如：a[0]或4[a]
		else if (isa<ArraySubscriptExpr>(s))
		{
			ArraySubscriptExpr *expr = cast<ArraySubscriptExpr>(s);
			m_root->UseQualType(loc, expr->getType());
		}
		// typeid语句，例如：typeid(int) or typeid(*obj)
		else if (isa<CXXTypeidExpr>(s))
		{
			CXXTypeidExpr *expr = cast<CXXTypeidExpr>(s);
			m_root->UseQualType(loc, expr->getType());
		}
		// 类构造语句
		else if (isa<CXXConstructExpr>(s))
		{
			CXXConstructExpr *cxxConstructExpr = cast<CXXConstructExpr>(s);
			m_root->UseValueDecl(loc, cxxConstructExpr->getConstructor());
		}
		// new语句
		else if (isa<CXXNewExpr>(s))
		{
			CXXNewExpr *cxxNewExpr = cast<CXXNewExpr>(s);

			const FunctionDecl *operatorNew		= cxxNewExpr->getOperatorNew();
			const FunctionDecl *operatorDelete	= cxxNewExpr->getOperatorDelete();

			m_root->UseFuncDecl(loc, operatorNew);
			m_root->UseFuncDecl(loc, operatorDelete);
		}
		// delete语句
		else if (isa<CXXDeleteExpr>(s))
		{
			CXXDeleteExpr *cxxDeleteExpr	= cast<CXXDeleteExpr>(s);
			const FunctionDecl *operatorDelete	= cxxDeleteExpr->getOperatorDelete();

			m_root->UseFuncDecl(loc, operatorDelete);
			m_root->UseQualType(loc, cxxDeleteExpr->getDestroyedType());
		}
		// sizeof(A)语句
		else if (isa<UnaryExprOrTypeTraitExpr>(s))
		{
			UnaryExprOrTypeTraitExpr *unaryExprOrTypeTraitExpr = cast<UnaryExprOrTypeTraitExpr>(s);
			m_root->UseVarType(loc, unaryExprOrTypeTraitExpr->getTypeOfArgument());

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
		else
		{
			cxx::log() << "<pre>------------ havn't support stmt ------------:</pre>\n";
			PrintStmt(s);
		}
		*/

		return true;
	}

	// 访问函数声明
	bool CxxCleanASTVisitor::VisitFunctionDecl(FunctionDecl *f)
	{
		// 处理函数参数、返回值等
		m_root->UseFuncDecl(f->getLocStart(), f);

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
				const FunctionDecl *oldestFunction = nullptr;

				for (const FunctionDecl *prevFunc = f->getPreviousDecl(); prevFunc; prevFunc = prevFunc->getPreviousDecl())
				{
					oldestFunction = prevFunc;
				}

				if (nullptr == oldestFunction)
				{
					return true;
				}

				// 引用函数原型
				m_root->UseFuncDecl(f->getLocStart(), oldestFunction);
			}

			// 是否属于某个类的成员函数
			if (isa<CXXMethodDecl>(f))
			{
				CXXMethodDecl *method = cast<CXXMethodDecl>(f);
				if (nullptr == method)
				{
					return false;
				}

				// 尝试找到该成员函数所属的struct/union/class.
				CXXRecordDecl *record = method->getParent();
				if (nullptr == record)
				{
					return false;
				}

				// 引用对应的struct/union/class.
				m_root->UseRecord(f->getLocStart(),	record);
			}
		}

		// ModifyFunc(f);
		return true;
	}

	// 访问class、struct、union、enum时
	bool CxxCleanASTVisitor::VisitCXXRecordDecl(CXXRecordDecl *r)
	{
		if (!r->hasDefinition())
		{
			return true;
		}

		// 遍历所有基类
		for (CXXRecordDecl::base_class_iterator itr = r->bases_begin(), end = r->bases_end(); itr != end; ++itr)
		{
			CXXBaseSpecifier &base = *itr;
			m_root->UseQualType(r->getLocStart(), base.getType());
		}

		// 遍历成员变量（注意：不包括static成员变量，static成员变量将在VisitVarDecl中被访问到）
		for (CXXRecordDecl::field_iterator itr = r->field_begin(), end = r->field_end(); itr != end; ++itr)
		{
			FieldDecl *field = *itr;
			m_root->UseValueDecl(r->getLocStart(), field);
		}

		// 成员函数不需要在这里遍历，因为VisitFunctionDecl将会访问成员函数
		return true;
	}

	// 当发现变量定义时该接口被调用
	bool CxxCleanASTVisitor::VisitVarDecl(VarDecl *var)
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
		m_root->UseVarDecl(var->getLocStart(), var);

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
			m_root->UseVarDecl(var->getLocStart(), prevVar);
		}

		if (var->hasExternalStorage())
		{
			for (const VarDecl *next : var->redecls())
			{
				if (!next->hasExternalStorage())
				{
					m_root->UseVarDecl(next->getLocStart(), var);
				}
			}
		}

		return true;
	}

	// 比如：typedef int A;
	bool CxxCleanASTVisitor::VisitTypedefDecl(clang::TypedefDecl *d)
	{
		// llvm::errs() << "Visiting " << d->getDeclKindName() << " " << d->getName() << "\n";

		m_root->UseQualType(d->getLocStart(), d->getUnderlyingType());
		return true;
	}

	// 比如：namespace A{}
	bool CxxCleanASTVisitor::VisitNamespaceDecl(clang::NamespaceDecl *d)
	{
		m_root->DeclareNamespace(d);
		return true;
	}

	// 比如：using namespace std;
	bool CxxCleanASTVisitor::VisitUsingDirectiveDecl(clang::UsingDirectiveDecl *d)
	{
		m_root->UsingNamespace(d);
		return true;
	}

	// 比如：using std::string;
	bool CxxCleanASTVisitor::VisitUsingDecl(clang::UsingDecl *d)
	{
		m_root->UsingXXX(d);
		return true;
	}

	// 访问成员变量
	bool CxxCleanASTVisitor::VisitFieldDecl(FieldDecl *decl)
	{
		m_root->UseValueDecl(decl->getLocStart(), decl);
		return true;
	}

	// 构造声明
	bool CxxCleanASTVisitor::VisitCXXConstructorDecl(CXXConstructorDecl *decl)
	{
		for (auto itr = decl->init_begin(), end = decl->init_end(); itr != end; ++itr)
		{
			CXXCtorInitializer *initializer = *itr;
			if (initializer->isAnyMemberInitializer())
			{
				m_root->UseValueDecl(initializer->getSourceLocation(), initializer->getAnyMember());
			}
			else if (initializer->isBaseInitializer())
			{
				m_root->UseType(initializer->getSourceLocation(), initializer->getBaseClass());
			}
			else if (initializer->isDelegatingInitializer())
			{
				if (initializer->getTypeSourceInfo())
				{
					m_root->UseQualType(initializer->getSourceLocation(), initializer->getTypeSourceInfo()->getType());
				}
			}
			else
			{
				// decl->dump();
			}
		}

		return true;
	}

	CxxCleanASTConsumer::CxxCleanASTConsumer(ParsingFile *rootFile)
		: m_root(rootFile), m_visitor(rootFile)
	{}

	// 覆盖：遍历最顶层声明的方法
	bool CxxCleanASTConsumer::HandleTopLevelDecl(DeclGroupRef declgroup)
	{
		return true;
	}

	// 这个函数对于每个源文件仅被调用一次，比如，若有一个hello.cpp中#include了许多头文件，也只会调用一次本函数
	void CxxCleanASTConsumer::HandleTranslationUnit(ASTContext& context)
	{
		if (!ProjectHistory::instance.m_isFirst)
		{
			return;
		}

		// 用于调试：打印语法树
		if (Project::instance.m_verboseLvl >= VerboseLvl_6)
		{
			cxx::log() << "<pre>------------ HandleTranslationUnit ------------:</pre>\n";
			cxx::log() << "<pre>";
			context.getTranslationUnitDecl()->dump(cxx::log());
			cxx::log() << "</pre>";
			cxx::log() << "<pre>------------ HandleTranslationUnit-End ------------:</pre>\n";
		}

		// 1. 生成每个文件的后代文件集（分析过程中需要用到）
		m_root->GenerateChildren();

		// 2. 遍历语法树
		m_visitor.TraverseDecl(context.getTranslationUnitDecl());
	}

	CxxcleanDiagnosticConsumer::CxxcleanDiagnosticConsumer(DiagnosticOptions *diags)
		: m_log(m_errorTip)
		, TextDiagnosticPrinter(m_log, diags, false)
	{
	}

	void CxxcleanDiagnosticConsumer::Clear()
	{
		m_log.flush();
		m_errorTip.clear();
	}

	void CxxcleanDiagnosticConsumer::BeginSourceFile(const LangOptions &LO, const Preprocessor *PP)
	{
		TextDiagnosticPrinter::BeginSourceFile(LO, PP);

		Clear();

		NumErrors		= 0;
		NumWarnings		= 0;
	}

	void CxxcleanDiagnosticConsumer::EndSourceFile()
	{
		TextDiagnosticPrinter::EndSourceFile();

		CompileErrorHistory &errHistory = ParsingFile::g_atFile->GetCompileErrorHistory();
		errHistory.errNum				= NumErrors;
	}

	// 是否是严重编译错误
	bool CxxcleanDiagnosticConsumer::IsFatalError(int errid)
	{
		if (errid == clang::diag::err_expected_lparen_after_type)
		{
			// 这个是unsigned int(0)类型的错误
			// int n =  unsigned int(0);
			//          ~~~~~~~~ ^
			// error: expected '(' for function-style cast or type construction
			return true;
		}

		return false;
	}

	// 当一个错误发生时，会调用此函数，在这个函数里记录编译错误和错误号
	void CxxcleanDiagnosticConsumer::HandleDiagnostic(DiagnosticsEngine::Level diagLevel, const Diagnostic &info)
	{
		if (!ProjectHistory::instance.m_isFirst)
		{
			return;
		}

		TextDiagnosticPrinter::HandleDiagnostic(diagLevel, info);

		CompileErrorHistory &errHistory = ParsingFile::g_atFile->GetCompileErrorHistory();

		int errId = info.getID();
		if (errId == diag::fatal_too_many_errors)
		{
			errHistory.hasTooManyError = true;
		}

		if (diagLevel < DiagnosticIDs::Error)
		{
			Clear();
			return;
		}

		std::string tip = m_errorTip;
		Clear();

		int errNum = errHistory.errTips.size() + 1;

		if (diagLevel >= DiagnosticIDs::Fatal || IsFatalError(errId))
		{
			errHistory.fatalErrors.insert(errId);

			tip += strtool::get_text(cn_fatal_error_num_tip, strtool::itoa(errNum).c_str(), htmltool::get_number_html(errId).c_str());
		}
		else if (diagLevel >= DiagnosticIDs::Error)
		{
			tip += strtool::get_text(cn_error_num_tip, strtool::itoa(errNum).c_str(), htmltool::get_number_html(errId).c_str());
		}

		errHistory.errTips.push_back(tip);
	}

	// 开始文件处理
	bool CxxCleanAction::BeginSourceFileAction(CompilerInstance &compiler, StringRef filename)
	{
		++ProjectHistory::instance.g_printFileNo;

		if (ProjectHistory::instance.m_isFirst)
		{
			bool only1Step = !Project::instance.m_need2Step;
			if (only1Step)
			{
				llvm::errs() << "cleaning file: ";
			}
			else
			{
				llvm::errs() << "step 1 of 2. analyze file: ";
			}
		}
		else
		{
			llvm::errs() << "step 2 of 2. cleaning file: ";
		}

		llvm::errs() << ProjectHistory::instance.g_printFileNo
		             << "/" << Project::instance.m_cpps.size() << ". " << filename << " ...\n";

		return true;
	}

	// 结束文件处理
	void CxxCleanAction::EndSourceFileAction()
	{
		// llvm::errs() << "** EndSourceFileAction for: " << srcMgr.getFileEntryForID(srcMgr.getMainFileID())->getName() << "\n";

		// 第2遍时不需要再分析了，因为第1遍已经分析好了
		if (ProjectHistory::instance.m_isFirst)
		{
			m_root->GenerateResult();
			m_root->Print();
		}

		bool can_clean	 = false;
		can_clean		|= Project::instance.m_onlyHas1File;;
		can_clean		|= !Project::instance.m_isCleanAll;
		can_clean		|= !ProjectHistory::instance.m_isFirst;

		if (can_clean)
		{
			m_root->Clean();
		}

		delete m_root;
		m_root = nullptr;
	}

	// 创建抽象语法树解析器
	std::unique_ptr<ASTConsumer> CxxCleanAction::CreateASTConsumer(CompilerInstance &compiler, StringRef file)
	{
		m_rewriter.setSourceMgr(compiler.getSourceManager(), compiler.getLangOpts());

		m_root = new ParsingFile(m_rewriter, compiler);

		HtmlLog::instance.m_newDiv.Clear();

		compiler.getPreprocessor().addPPCallbacks(llvm::make_unique<CxxCleanPreprocessor>(m_root));
		return llvm::make_unique<CxxCleanASTConsumer>(m_root);
	}

	// 用于调试：打印包含文件
	void CxxCleanAction::PrintIncludes()
	{
		cxx::log() << "\n////////////////////////////////\n";
		cxx::log() << "print fileinfo_iterator include:\n";
		cxx::log() << "////////////////////////////////\n";

		SourceManager &srcMgr = m_rewriter.getSourceMgr();

		typedef SourceManager::fileinfo_iterator fileinfo_iterator;

		fileinfo_iterator beg = srcMgr.fileinfo_begin();
		fileinfo_iterator end = srcMgr.fileinfo_end();

		for (fileinfo_iterator itr = beg; itr != end; ++itr)
		{
			const FileEntry *fileEntry = itr->first;
			SrcMgr::ContentCache *cache = itr->second;

			//printf("#include = %s\n", fileEntry->getName());
			cxx::log() << "    #include = "<< fileEntry->getName()<< "\n";
		}
	}

	// 用于调试：打印主文件的包含文件
	void CxxCleanAction::PrintTopIncludes()
	{
		cxx::log() << "\n////////////////////////////////\n";
		cxx::log() << "print top getLocalSLocEntry include:\n";
		cxx::log() << "////////////////////////////////\n";

		SourceManager &srcMgr = m_rewriter.getSourceMgr();
		int include_size = srcMgr.local_sloc_entry_size();

		for (int i = 1; i < include_size; i++)
		{
			const SrcMgr::SLocEntry &locEntry = srcMgr.getLocalSLocEntry(i);
			if (!locEntry.isFile())
			{
				continue;
			}

			cxx::log() << "    #include = "<< srcMgr.getFilename(locEntry.getFile().getIncludeLoc()) << "\n";
		}
	}

	static llvm::cl::OptionCategory g_optionCategory("cxx-clean-include category");

	static cl::opt<string>	g_cleanOption	("clean",
	        cl::desc("you can use this option to:\n"
	                 "    1. clean directory, for example: -clean ../hello/\n"
	                 "    2. clean visual studio project(version 2005 or upper): for example: -clean ./hello.vcproj or -clean ./hello.vcxproj\n"
	                ),
	        cl::cat(g_optionCategory));

	static cl::opt<string>	g_cleanModes	("mode",
	        cl::desc("clean modes, can use like [ -mode 1+2+3 ], default is [ -mode 1+2 ]\n"
	                 "mode 1. clean unused #include\n"
	                 "mode 2. replace some #include to other #include\n"
	                 "mode 3. try move #include to other file"
	                ),
	        cl::cat(g_optionCategory));

	static cl::opt<string>	g_src			("src",
	        cl::desc("target c++ source file to be cleaned, must have valid path, if this option was valued, only the target c++ file will be cleaned\n"
	                 "this option can be used with -clean option, for example, you can use: \n"
	                 "    cxxclean -clean hello.vcproj -src ./hello.cpp\n"
	                 "it will only clean hello.cpp and using hello.vcproj configuration"
	                ),
	        cl::cat(g_optionCategory));

	static cl::opt<bool>	g_noWriteOption	("no",
	        cl::desc("means no overwrite, if this option is checked, all of the c++ file will not be changed"),
	        cl::cat(g_optionCategory));

	static cl::opt<bool>	g_onlyCleanCpp	("onlycpp",
	        cl::desc("only allow clean cpp file(cpp, cc, cxx), don't clean the header file(h, hxx, hh)"),
	        cl::cat(g_optionCategory));

	static cl::opt<bool>	g_deepClean	("deep",
	                                     cl::desc("deep clean"),
	                                     cl::cat(g_optionCategory));

	static cl::opt<bool>	g_printVsConfig	("print-vs",
	        cl::desc("print vs configuration, for example, print header search path, print c++ file list, print predefine macro and so on"),
	        cl::cat(g_optionCategory));

	static cl::opt<bool>	g_printProject	("print-project",
	        cl::desc("print project configuration, for example, print c++ source list to be cleaned, print allowed clean directory or allowed clean c++ file list, and so on"),
	        cl::cat(g_optionCategory));

	static cl::opt<int>		g_verbose		("v",
	        cl::desc("verbose level, level can be 0 ~ 6, default is 1, higher level will print more detail"),
	        cl::cat(g_optionCategory));

	void PrintVersion()
	{
		llvm::outs() << "cxx-clean-include version is 1.0\n";
		llvm::outs() << clang::getClangToolFullVersion("clang lib version is") << '\n';
	}

	// 解析选项并将解析结果存入相应的对象，若应中途退出则返回true，否则返回false
	bool CxxCleanOptionsParser::ParseOptions(int &argc, const char **argv)
	{
		// 使用-help选项时，仅打印本工具的选项
		cl::HideUnrelatedOptions(cxxclean::g_optionCategory);
		cl::SetVersionPrinter(cxxclean::PrintVersion);

		m_compilation.reset(CxxCleanOptionsParser::SplitCommandLine(argc, argv));

		cl::ParseCommandLineOptions(argc, argv, nullptr);

		if (!ParseVerboseOption())
		{
			return false;
		}

		if (!ParseCleanLvlOption())
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

		if (g_printVsConfig)
		{
			VsProject::instance.Print();
			return false;
		}

		if (g_printProject)
		{
			HtmlLog::instance.BeginLog();
			Project::instance.Print();
			return false;
		}

		if (Project::instance.m_cpps.empty())
		{
			llvm::errs() << "cxx-clean-include: \n    try use -help argument to see more information.\n";
			return 0;
		}

		HtmlLog::instance.BeginLog();

		if (Project::instance.m_verboseLvl >= VerboseLvl_3)
		{
			Project::instance.Print();
		}

		return true;
	}

	// 拆分传入的命令行参数，"--"分隔符前面的命令行参数将被本工具解析，后面的命令行参数将被clang库解析
	// 注意：argc将被更改为"--"分隔符前的参数个数
	// 例如：
	//		假设使用cxxclean -clean ./hello/ -- -include log.h
	//		则-clean ./hello/将被本工具解析，-include log.h将被clang库解析
	FixedCompilationDatabase *CxxCleanOptionsParser::SplitCommandLine(int &argc, const char *const *argv, Twine directory /* = "." */)
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
	bool CxxCleanOptionsParser::AddCleanVsArgument(const VsProject &vs, ClangTool &tool) const
	{
		if (vs.m_configs.empty())
		{
			return false;
		}

		const VsConfig &vsconfig = vs.m_configs[0];

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

		for (auto &predefine : vsconfig.preDefines)
		{
			std::string arg = "-D" + predefine;

			ArgumentsAdjuster argAdjuster = getInsertArgumentAdjuster(arg.c_str(), ArgumentInsertPosition::BEGIN);
			tool.appendArgumentsAdjuster(argAdjuster);
		}

		tool.appendArgumentsAdjuster(getInsertArgumentAdjuster("-fms-extensions", ArgumentInsertPosition::BEGIN));
		tool.appendArgumentsAdjuster(getInsertArgumentAdjuster("-fms-compatibility", ArgumentInsertPosition::BEGIN));

		if (vs.m_version >= 2008)
		{
			tool.appendArgumentsAdjuster(getInsertArgumentAdjuster("-fms-compatibility-version=18", ArgumentInsertPosition::BEGIN));
		}

		AddVsSearchDir(tool);

		pathtool::cd(vs.m_project_dir.c_str());
		return true;
	}

	// 获取visual studio的安装路径
	std::string CxxCleanOptionsParser::GetVsInstallDir() const
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
	void CxxCleanOptionsParser::AddVsSearchDir(ClangTool &tool) const
	{
		if (VsProject::instance.m_configs.empty())
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
	bool CxxCleanOptionsParser::ParseSrcOption()
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

			cxx::init_log(strtool::get_text(cn_cpp_file, pathtool::get_file_name(src).c_str()));
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

			std::string log_file = strtool::replace(src, "/", "_");
			log_file = strtool::replace(src, ".", "_");
			cxx::init_log(strtool::get_text(cn_folder, log_file.c_str()));
		}

		return true;
	}

	// 解析-clean选项
	bool CxxCleanOptionsParser::ParseCleanOption()
	{
		VsProject &vs				= VsProject::instance;
		Project &project			= Project::instance;

		project.m_isCleanAll		= !g_onlyCleanCpp;
		project.m_isOverWrite		= !g_noWriteOption;
		project.m_isDeepClean		= g_deepClean;
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

				// cxx::log() << "parse vs project<" << clean_option << "> succeed!\n";

				HtmlLog::instance.SetHtmlTitle(strtool::get_text(cn_project, clean_option.c_str()));
				HtmlLog::instance.SetBigTitle(strtool::get_text(cn_project, htmltool::get_file_html(clean_option).c_str()));

				cxx::init_log(strtool::get_text(cn_project_1, llvm::sys::path::stem(clean_option).str().c_str()));
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

					HtmlLog::instance.SetHtmlTitle(strtool::get_text(cn_folder, clean_option.c_str()));
					HtmlLog::instance.SetBigTitle(strtool::get_text(cn_folder, htmltool::get_file_html(clean_option).c_str()));

					cxx::init_log(strtool::get_text(cn_folder, clean_option.c_str()));
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

		Project::instance.m_need2Step = Project::instance.m_isCleanAll && !Project::instance.m_onlyHas1File && Project::instance.m_isOverWrite;
		return true;
	}

	// 解析-v选项
	bool CxxCleanOptionsParser::ParseVerboseOption()
	{
		if (g_verbose.getNumOccurrences() == 0)
		{
			Project::instance.m_verboseLvl = VerboseLvl_1;
			return true;
		}

		int lvl = g_verbose;
		Project::instance.m_verboseLvl = (VerboseLvl)lvl;

		if (lvl < 0 || lvl >= VerboseLvl_Max)
		{
			llvm::errs() << "unsupport verbose level: " << lvl << ", must be 1 ~ " << VerboseLvl_Max - 1 << "!\n";
			return false;
		}

		return true;
	}

	// 解析-level选项
	bool CxxCleanOptionsParser::ParseCleanLvlOption()
	{
		string strLvls ;

		if (g_cleanModes.getNumOccurrences() == 0)
		{
			// 默认： [删除多余#include] + [替换#include]
			strLvls = "1+2";
		}
		else
		{
			strLvls = g_cleanModes;
		}

		std::vector<string> strLvlVec;
		strtool::split(strLvls, strLvlVec, '+');

		Project::instance.m_cleanModes.resize(CleanMode_Max - 1, false);

		for (const string &strLvl : strLvlVec)
		{
			int lvl = strtool::atoi(strLvl.c_str());

			if (lvl < 0 || lvl >= CleanMode_Max)
			{
				llvm::errs() << "[ -level " << strLvls << " ] is invalid: unsupport [" << lvl << "], must be 1 ~ " << CleanMode_Max - 1 << "!\n";
				return false;
			}

			Project::instance.m_cleanModes[lvl - 1] = true;
		}

		return true;
	}

	static cl::extrahelp MoreHelp(
	    "\n"
	    "\nExample Usage:"
	    "\n"
	    "\n    there are 2 ways to use cxx-clean-include"
	    "\n"
	    "\n    1. for msvc project(visual studio 2005 and upper)"
	    "\n"
	    "\n        -> clean the whole msvc project:"
	    "\n            cxxclean -clean hello.vcproj"
	    "\n"
	    "\n        -> only want to clean a single c++ file, and using the vs configuration:"
	    "\n            cxxclean -clean hello.vcproj -src hello.cpp"
	    "\n"
	    "\n    2. for a directory"
	    "\n"
	    "\n        -> clean all c++ file in the directory:"
	    "\n            cxxclean -clean ./hello"
	    "\n"
	    "\n        -> only clean a single c++ file, you can use:"
	    "\n            cxxclean -clean ./hello -src hello.cpp"
	);
}