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
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Analysis/CFG.h"

#include "tool.h"
#include "vs.h"
#include "parser.h"
#include "project.h"
#include "html_log.h"

using namespace ast_matchers;

// 预处理器，当#define、#if、#else等预处理关键字被预处理时使用本预处理器
CxxCleanPreprocessor::CxxCleanPreprocessor(ParsingFile *rootFile)
	: m_root(rootFile)
{}

// 文件切换
void CxxCleanPreprocessor::FileChanged(SourceLocation loc, FileChangeReason reason, SrcMgr::CharacteristicKind FileType, FileID prevFileID /* = FileID() */)
{
	// 注意：当为PPCallbacks::EnterFile时，prevFileID是无效的
	if (reason != PPCallbacks::EnterFile)
	{
		return;
	}

	FileID curFileID = m_root->GetSrcMgr().getFileID(loc);

	// 这里要注意，有的文件是会被FileChanged遗漏掉的，除非把HeaderSearch::ShouldEnterIncludeFile方法改为每次都返回true
	m_root->AddFile(curFileID);
}

// 文件被跳过
void CxxCleanPreprocessor::FileSkippedWithFileID(FileID fileID)
{
	m_root->AddFile(fileID);
}

// 定义宏，如#if defined DEBUG
void CxxCleanPreprocessor::Defined(const Token &macroName, const MacroDefinition &definition, SourceRange range)
{
	m_root->UseMacro(macroName.getLocation(), definition, macroName);
}

// 处理#define
void CxxCleanPreprocessor::MacroDefined(const Token &macroName, const MacroDirective *direct) {}

// 宏被#undef
void CxxCleanPreprocessor::MacroUndefined(const Token &macroName, const MacroDefinition &definition)
{
	m_root->UseMacro(macroName.getLocation(), definition, macroName);
}

// 宏扩展
void CxxCleanPreprocessor::MacroExpands(const Token &macroName, const MacroDefinition &definition, SourceRange range, const MacroArgs *args)
{
	m_root->UseMacro(range.getBegin(), definition, macroName, args);
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
{}

// 访问单条语句
bool CxxCleanASTVisitor::VisitStmt(Stmt *s)
{
	SourceLocation loc = s->getLocStart();
	if (m_root->IsInSystemHeader(loc))
	{
		return true;
	}

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
		}
	}
	else if (isa<DeclRefExpr>(s))
	{
		DeclRefExpr *declRefExpr = cast<DeclRefExpr>(s);
		ValueDecl *valueDecl = declRefExpr->getDecl();
		const NestedNameSpecifier *specifier = declRefExpr->getQualifier();

		m_root->SearchUsingAny(loc, specifier, valueDecl);
		m_root->UseQualifier(loc, specifier);
		m_root->UseValueDecl(loc, valueDecl);
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
		m_root->UseConstructor(loc, cxxConstructExpr->getConstructor());
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
	// 注意：下面这一大段很重要，保留用于以后日志跟踪

	// 类似于DeclRefExpr，要等实例化时才知道类型，比如：T::
	else if (isa<DependentScopeDeclRefExpr>(s)){}
	// return语句，例如：return 4;、return;
	else if (isa<ReturnStmt>(s)){}
	// 括号表达式，例如：(1)、(a + b)
	else if (isa<ParenExpr>(s)){}
	// 复合表达式，由不同表达式组合而成
	else if(isa<CompoundStmt>(s)){}
	// 二元表达式，例如："x + y" or "x <= y"，若操作符未被重载，则类型是BinaryOperator，若操作符被重载，则类型将是CXXOperatorCallExpr
	else if (isa<BinaryOperator>(s)){}
	// 一元表达式，例如：正号+，负号-，自增++，自减--,逻辑非！，按位取反~，取变量地址&，取指针所指值*等
	else if (isa<UnaryOperator>(s)){}
	// 整数
	else if (isa<IntegerLiteral>(s)){}
	// 三元表达式，即条件表达式，例如： x ? y : z
	else if (isa<ConditionalOperator>(s)){}
	// for语句，例如：for(;;){ int i = 0; }
	else if (isa<ForStmt>(s)){}
	else if (isa<UnaryExprOrTypeTraitExpr>(s) || isa<InitListExpr>(s) || isa<MaterializeTemporaryExpr>(s)){}
	// 代表未知类型的构造函数
	else if (isa<CXXUnresolvedConstructExpr>(s)){}
	// c++11的打包扩展语句，后面跟着...省略号
	else if (isa<PackExpansionExpr>(s)){}
	else if (isa<UnresolvedLookupExpr>(s) || isa<CXXBindTemporaryExpr>(s) || isa<ExprWithCleanups>(s)){}
	else if (isa<ParenListExpr>(s)){}
	else if (isa<DeclStmt>(s)){}
	else if (isa<IfStmt>(s) || isa<SwitchStmt>(s) || isa<CXXTryStmt>(s) || isa<CXXCatchStmt>(s) || isa<CXXThrowExpr>(s)){}
	else if (isa<StringLiteral>(s) || isa<CharacterLiteral>(s) || isa<CXXBoolLiteralExpr>(s) || isa<FloatingLiteral>(s)){}
	else if (isa<NullStmt>(s)){}
	else if (isa<CXXDefaultArgExpr>(s)){}
	//	代表c++的成员访问语句，访问可能是显式或隐式
	else if (isa<UnresolvedMemberExpr>(s)){}
	else
	{
		HtmlLog::GetLog() << "<pre>------------ havn't support stmt ------------:</pre>\n";
		PrintStmt(s);
	}
	*/

	return true;
}

// 访问函数声明
bool CxxCleanASTVisitor::VisitFunctionDecl(FunctionDecl *f)
{
	SourceLocation loc = f->getLocStart();
	if (m_root->IsInSystemHeader(loc))
	{
		return true;
	}

	// 处理函数参数、返回值等
	m_root->UseFuncDecl(loc, f);

	// 注意：非类函数可以反复进行声明，这里仅关注最早的函数声明
	const FunctionDecl *oldestFunction = nullptr;

	for (const FunctionDecl *prevFunc = f->getPreviousDecl(); prevFunc; prevFunc = prevFunc->getPreviousDecl())
	{
		oldestFunction = prevFunc;
	}

	if (oldestFunction)
	{
		// 引用函数原型
		m_root->UseFuncDecl(loc, oldestFunction);
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
		m_root->UseRecord(loc,	record);
	}

	return true;
}

// 访问class、struct、union、enum时
bool CxxCleanASTVisitor::VisitCXXRecordDecl(CXXRecordDecl *r)
{
	if (!r->hasDefinition())
	{
		return true;
	}

	SourceLocation loc = r->getLocStart();
	if (m_root->IsInSystemHeader(loc))
	{
		return true;
	}

	// 模板特化，例子如下：
	//	template<typename T> class array;
	//	
	//	template<> class array<bool> { }; // class template specialization array<bool>
	if (const ClassTemplateSpecializationDecl *Spec = dyn_cast<ClassTemplateSpecializationDecl>(r))
	{
		m_root->UseTemplateDecl(loc, Spec->getSpecializedTemplate());
	}

	// 遍历所有基类
	for (CXXRecordDecl::base_class_iterator itr = r->bases_begin(), end = r->bases_end(); itr != end; ++itr)
	{
		CXXBaseSpecifier &base = *itr;
		m_root->UseQualType(loc, base.getType());
	}

	// 遍历成员变量（注意：不包括static成员变量，static成员变量将在VisitVarDecl中被访问到）
	for (CXXRecordDecl::field_iterator itr = r->field_begin(), end = r->field_end(); itr != end; ++itr)
	{
		FieldDecl *field = *itr;
		m_root->UseValueDecl(loc, field);
	}

	m_root->UseQualifier(loc, r->getQualifier());

	// 成员函数不需要在这里遍历，因为VisitFunctionDecl将会访问成员函数
	return true;
}

// 当发现变量定义时该接口被调用
bool CxxCleanASTVisitor::VisitVarDecl(VarDecl *var)
{
	SourceLocation loc = var->getLocStart();
	if (m_root->IsInSystemHeader(loc))
	{
		return true;
	}

	// 注意：本方法涵盖了1. 函数形参 2. 非类成员的变量声明 3. 类成员但含有static修饰符，等等

	// 引用变量的类型
	m_root->UseVarDecl(loc, var);

	// 类的static成员变量（支持模板类的成员）
	if (var->isCXXClassMember())
	{
		// 若为static成员变量的实现，则引用其声明（注：也可以用isStaticDataMember方法来判断var是否为static成员变量）
		const VarDecl *prevVar = var->getPreviousDecl();
		m_root->UseVarDecl(loc, prevVar);
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
	SourceLocation loc = d->getLocStart();
	if (m_root->IsInSystemHeader(loc))
	{
		return true;
	}

	m_root->UseQualType(loc, d->getUnderlyingType());
	return true;
}

// 比如：namespace A{}
bool CxxCleanASTVisitor::VisitNamespaceDecl(clang::NamespaceDecl *d)
{
	SourceLocation loc = d->getLocStart();
	if (m_root->IsInSystemHeader(loc))
	{
		return true;
	}

	m_root->DeclareNamespace(d);
	return true;
}

// 比如：namespace s = std;
bool CxxCleanASTVisitor::VisitNamespaceAliasDecl(clang::NamespaceAliasDecl *d)
{
	SourceLocation loc = d->getLocation();
	if (m_root->IsInSystemHeader(loc))
	{
		return true;
	}

	m_root->UseNamespaceDecl(loc, d->getNamespace());
	return true;
}

// 比如：using namespace std;
bool CxxCleanASTVisitor::VisitUsingDirectiveDecl(clang::UsingDirectiveDecl *d)
{
	SourceLocation loc = d->getLocation();
	if (m_root->IsInSystemHeader(loc))
	{
		return true;
	}

	m_root->UsingNamespace(d);
	return true;
}

// 比如：using std::string;
bool CxxCleanASTVisitor::VisitUsingDecl(clang::UsingDecl *d)
{
	SourceLocation loc = d->getLocation();
	if (m_root->IsInSystemHeader(loc))
	{
		return true;
	}

	m_root->UsingXXX(d);
	return true;
}

// 访问成员变量
bool CxxCleanASTVisitor::VisitFieldDecl(FieldDecl *decl)
{
	SourceLocation loc = decl->getLocStart();
	if (m_root->IsInSystemHeader(loc))
	{
		return true;
	}

	m_root->UseValueDecl(decl->getLocStart(), decl);
	return true;
}

// 构造声明
bool CxxCleanASTVisitor::VisitCXXConstructorDecl(CXXConstructorDecl *constructor)
{
	SourceLocation loc = constructor->getLocStart();
	if (m_root->IsInSystemHeader(loc))
	{
		return true;
	}

	m_root->UseConstructor(constructor->getLocStart(), constructor);
	return true;
}

// 构造语句
bool CxxCleanASTVisitor::VisitCXXConstructExpr(CXXConstructExpr *expr)
{
	SourceLocation loc = expr->getLocStart();
	if (m_root->IsInSystemHeader(loc))
	{
		return true;
	}

	m_root->UseConstructor(expr->getLocStart(), expr->getConstructor());
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
	// 用于调试：打印语法树
	if (Project::instance.m_logLvl >= LogLvl_Max)
	{
		std::string log;
		raw_string_ostream logStream(log);
		context.getTranslationUnitDecl()->dump(logStream);
		logStream.flush();

		HtmlLog::GetLog() << "<div class=\"box\"><div><dl><dd><div><span class=\"bold\">------------ HandleTranslationUnit begin ------------</span>";
		HtmlLog::GetLog() << m_root->DebugBeIncludeText(m_root->GetSrcMgr().getMainFileID());
		HtmlLog::GetLog() << "<pre>";
		HtmlLog::GetLog() << escape_html(log);
		HtmlLog::GetLog() << "</pre><span class=\"bold\">------------ HandleTranslationUnit end ------------</span></div></dd></dl></div></div>\n";
	}

	// 1. 当前cpp文件分析开始
	m_root->Begin();

	// 2. 遍历语法树
	m_visitor.TraverseDecl(context.getTranslationUnitDecl());
}

CxxcleanDiagnosticConsumer::CxxcleanDiagnosticConsumer(DiagnosticOptions *diags)
	: m_log(m_errorTip)
	, TextDiagnosticPrinter(m_log, diags, false)
{}

void CxxcleanDiagnosticConsumer::Clear()
{
	m_log.flush();
	m_errorTip.clear();
}

void CxxcleanDiagnosticConsumer::BeginSourceFile(const LangOptions &LO, const Preprocessor *PP)
{
	PP->getDiagnostics().Reset();
	TextDiagnosticPrinter::BeginSourceFile(LO, PP);

	Clear();

	NumErrors		= 0;
	NumWarnings		= 0;
}

void CxxcleanDiagnosticConsumer::EndSourceFile()
{
	TextDiagnosticPrinter::EndSourceFile();

	if (nullptr == ParsingFile::g_nowFile)
	{
		return;
	}

	CompileErrorHistory &errHistory = ParsingFile::g_nowFile->GetCompileErrorHistory();
	errHistory.errNum				= NumErrors;
}

// 当一个错误发生时，会调用此函数，在这个函数里记录编译错误和错误号
void CxxcleanDiagnosticConsumer::HandleDiagnostic(DiagnosticsEngine::Level diagLevel, const Diagnostic &info)
{
	TextDiagnosticPrinter::HandleDiagnostic(diagLevel, info);

	if (nullptr == ParsingFile::g_nowFile)
	{
		return;
	}

	CompileErrorHistory &errHistory = ParsingFile::g_nowFile->GetCompileErrorHistory();

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

	m_log.flush();
	std::string err = escape_html(m_errorTip);
	Clear();

	int errNum = errHistory.errors.size() + 1;

	if (diagLevel >= DiagnosticIDs::Fatal)
	{
		errHistory.fatalErrorIds.insert(errId);

		err += strtool::get_text(cn_fatal_error_num_tip, strtool::itoa(errNum).c_str(), get_number_html(errId).c_str());
	}
	else if (diagLevel >= DiagnosticIDs::Error)
	{
		err += strtool::get_text(cn_error_num_tip, strtool::itoa(errNum).c_str(), get_number_html(errId).c_str());
	}

	errHistory.errors.push_back(err);
}

// 开始文件处理
bool CxxCleanAction::BeginSourceFileAction(CompilerInstance &compiler, StringRef filename)
{
	++ProjectHistory::instance.g_fileNum;
	Log("cleaning file: " << ProjectHistory::instance.g_fileNum << "/" << Project::instance.m_cpps.size() << ". " << filename << " ...");
	return true;
}

// 结束文件处理
void CxxCleanAction::EndSourceFileAction()
{
	m_root->End();
	delete m_root;
	m_root = nullptr;
}

// 创建抽象语法树解析器
std::unique_ptr<ASTConsumer> CxxCleanAction::CreateASTConsumer(CompilerInstance &compiler, StringRef file)
{
	m_root = new ParsingFile(compiler);

	compiler.getPreprocessor().addPPCallbacks(llvm::make_unique<CxxCleanPreprocessor>(m_root));
	return llvm::make_unique<CxxCleanASTConsumer>(m_root);
}

static llvm::cl::OptionCategory g_optionCategory("cxx-clean-include category");

static cl::opt<string>	g_vsOption		("vs", cl::desc("clean visual studio project(version 2005 or upper), format:-vs ./hello.vcproj or -vs ./hello.vcxproj\n"), cl::cat(g_optionCategory));
static cl::opt<bool>	g_noOverWrite	("no", cl::desc("means no overwrite, all c++ file will not be changed"), cl::cat(g_optionCategory));
static cl::opt<bool>	g_onlyCleanCpp	("onlycpp", cl::desc("only allow clean cpp file(cpp, cc, cxx), don't clean the header file(h, hxx, hh)"), cl::cat(g_optionCategory));
static cl::opt<bool>	g_printVsConfig	("print-vs", cl::desc("print vs configuration"), cl::cat(g_optionCategory));
static cl::opt<int>		g_logLevel		("v", cl::desc("log level(verbose level), level can be 0 ~ 6, default is 1, higher level will print more detail"), cl::cat(g_optionCategory));
static cl::list<string>	g_skips			("skip", cl::desc("skip files"), cl::cat(g_optionCategory));
static cl::opt<string>	g_cleanOption	("clean",
        cl::desc("format:\n"
                 "    1. clean directory: -clean ../hello/\n"
                 "    2. clean a c++ file: -clean ./hello.cpp\n"
                ), cl::cat(g_optionCategory));

void PrintVersion()
{
	llvm::outs() << clang::getClangToolFullVersion("clang lib version: ") << '\n';
}

// 解析选项并将解析结果存入相应的对象，若应中途退出则返回true，否则返回false
bool CxxCleanOptionsParser::ParseOptions(int &argc, const char **argv)
{
	// 使用-help选项时，仅打印本工具的选项
	cl::HideUnrelatedOptions(g_optionCategory);
	cl::SetVersionPrinter(PrintVersion);

	m_compilation.reset(SplitCommandLine(argc, argv));

	cl::ParseCommandLineOptions(argc, argv);

	if (!ParseLogOption() || !ParseCleanOption())
	{
		return false;
	}

	if (Project::instance.m_cpps.empty())
	{
		Log("cxx-clean-include: \n    try use -help argument to see more information.");
		return 0;
	}

	// 存下忽略文件-skip选项的值
	Add(Project::instance.m_skips, g_skips);

	HtmlLog::instance.BeginLog();

	if (g_printVsConfig)
	{
		VsProject::instance.Print();
		return false;
	}

	Project::instance.Print();
	return true;
}

// 拆分传入的命令行参数，"--"分隔符前面的命令行参数将被本工具解析，后面的命令行参数将被clang库解析
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

// 添加clang参数
void CxxCleanOptionsParser::AddClangArgument(ClangTool &tool, const char *arg) const
{
	ArgumentsAdjuster argAdjuster = getInsertArgumentAdjuster(arg, ArgumentInsertPosition::BEGIN);
	tool.appendArgumentsAdjuster(argAdjuster);
}

// 根据命令行添加clang参数
void CxxCleanOptionsParser::AddClangArgumentByOption(ClangTool &tool) const
{
	// 根据vs工程文件调整clang的参数
	AddVsArgument(VsProject::instance, tool);

	// 若只有一个文件
	if (Project::instance.m_cpps.size() == 1)
	{
		const std::string &file = Project::instance.m_cpps[0];

		// 且是头文件
		if (cpptool::is_header(file))
		{
			// 若只分析头文件，必须加上下面参数clang才能成功解析
			AddClangArgument(tool, "-xc++-header");
		}
	}
}

// 添加系统头文件搜索路径
void CxxCleanOptionsParser::AddSystemHeaderSearchPath(ClangTool &tool, const char *path) const
{
	const std::string arg = std::string("-isystem") + path;
	AddClangArgument(tool, arg.c_str());
}

// 添加用户头文件搜索路径
void CxxCleanOptionsParser::AddHeaderSearchPath(ClangTool &tool, const char *path) const
{
	const std::string arg = std::string("-I") + path;
	AddClangArgument(tool, arg.c_str());
}

// 根据vs工程文件调整clang的参数
bool CxxCleanOptionsParser::AddVsArgument(const VsProject &vs, ClangTool &tool) const
{
	if (vs.m_configs.empty())
	{
		return false;
	}

	const VsConfig &vsconfig = vs.m_configs[0];

	for (const std::string &dir	: vsconfig.searchDirs)
	{
		AddHeaderSearchPath(tool, dir.c_str());
	}

	for (const std::string &force_include : vsconfig.forceIncludes)
	{
		const std::string arg = "-include" + force_include;
		AddClangArgument(tool, arg.c_str());
	}

	for (auto &predefine : vsconfig.preDefines)
	{
		const std::string arg = "-D" + predefine;
		AddClangArgument(tool, arg.c_str());
	}

	AddClangArgument(tool, "-fms-extensions");
	AddClangArgument(tool, "-fms-compatibility");

	if (vs.m_version >= 2008)
	{
		// AddArgument(tool, "-fms-compatibility-version=18");
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
	DiagnosticsEngine engine(IntrusiveRefCntPtr<clang::DiagnosticIDs>(new DiagnosticIDs()), nullptr, nullptr, false);

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
		const std::string path = pathtool::append_path(vsInstallDir.c_str(), try_path);
		AddSystemHeaderSearchPath(tool, path.c_str());
	}
}

bool CxxCleanOptionsParser::ParseCleanOption()
{
	Project &project			= Project::instance;

	project.m_isOverWrite		= !g_noOverWrite;
	project.m_workingDir		= pathtool::get_current_path();

	std::string vsOption		= g_vsOption;
	std::string clean_option	= g_cleanOption;

	// 解析-vs参数（-vs指定了应清理的visual studio工程文件）
	if (!vsOption.empty())
	{
		VsProject &vs = VsProject::instance;

		const string ext = strtool::get_ext(vsOption);
		if (ext == "vcproj" || ext == "vcxproj")
		{
			if (!vs.ParseVs(vsOption))
			{
				Log("parse vs project<" << vsOption << "> failed!");
				return false;
			}

			project.m_cpps = vs.m_cpps;
			project.m_canCleanFiles = vs.m_all;

			std::string vsPath = llvm::sys::path::stem(vsOption).str();

			HtmlLog::instance.Init(strtool::get_wide_text(cn_log_name_project, strtool::s2ws(vsPath).c_str()),
			                       strtool::get_text(cn_project, vsOption.c_str()),
			                       strtool::get_text(cn_project, get_file_html(vsOption.c_str()).c_str()));
		}
		else
		{
			Log("unsupport parsed visual studio project <" << vsOption << ">!");
			return false;
		}
	}

	// 解析-clean参数（-clean配置了哪些cpp文件应被清理）
	if (!clean_option.empty())
	{
		if (!pathtool::exist(clean_option))
		{
			Log("error: parse argument -clean " << clean_option << " failed, not found the directory or c++ file.");
			return false;
		}

		// 文件夹
		if (llvm::sys::fs::is_directory(clean_option))
		{
			std::string directory = pathtool::get_absolute_path(clean_option.c_str());
			if (!strtool::end_with(directory, "/"))
			{
				directory += "/";
			}

			// 列出本文件夹下所有文件
			std::vector<std::string> all;
			bool ok = pathtool::ls(directory, all);
			if (!ok)
			{
				Log("error: -clean " << directory << " failed, not found files at the directory!");
				return false;
			}

			for (std::string &file : all)
			{
				file = pathtool::get_lower_absolute_path(file.c_str());
			}

			Add(project.m_canCleanFiles, all);

			// -vs选项为空
			if (vsOption.empty())
			{
				project.m_cpps = all;

				std::string logFile = directory;
				strtool::replace(logFile, "/", "_");
				strtool::replace(logFile, ".", "_");

				HtmlLog::instance.Init(strtool::get_wide_text(cn_log_name_folder, strtool::s2ws(logFile).c_str()),
				                       strtool::get_text(strtool::ws2s(cn_log_name_folder).c_str(), directory.c_str()),
				                       strtool::get_text(strtool::ws2s(cn_log_name_folder).c_str(), get_file_html(directory.c_str()).c_str()));
			}
			// 已有-vs参数
			else
			{
				// 找出该文件夹下属于项目的cpp
				FileNameSet vsCpps(project.m_cpps.begin(), project.m_cpps.end());
				project.m_cpps.clear();

				for (const std::string &file : all)
				{
					if (Has(vsCpps, file))
					{
						// 仅保留处于该文件夹的项目cpp成员
						project.m_cpps.push_back(file);
					}
				}
			}
		}
		// 文件
		else
		{
			std::string filePath = pathtool::get_lower_absolute_path(clean_option.c_str());
			project.m_cpps.clear();
			project.m_cpps.push_back(filePath);
			project.m_canCleanFiles.insert(filePath);

			std::string fileName = pathtool::get_file_name(filePath.c_str());

			HtmlLog::instance.Init(strtool::get_wide_text(cn_log_name_cpp_file, strtool::s2ws(fileName).c_str()),
			                       strtool::get_text(strtool::ws2s(cn_log_name_cpp_file).c_str(), filePath.c_str()),
			                       strtool::get_text(strtool::ws2s(cn_log_name_cpp_file).c_str(), get_file_html(filePath.c_str()).c_str()));
		}

		project.Fix();
	}

	// 解析-onlycpp（该选项开启时，仅允许清理源文件，禁止清理头文件）
	if (g_onlyCleanCpp)
	{
		project.m_canCleanFiles.clear();
		Add(project.m_canCleanFiles, project.m_cpps);
	}

	return true;
}

// 解析-v选项
bool CxxCleanOptionsParser::ParseLogOption()
{
	if (g_logLevel.getNumOccurrences() == 0)
	{
		Project::instance.m_logLvl = LogLvl_1;
		return true;
	}

	int logLvl = g_logLevel;
	Project::instance.m_logLvl = (LogLvl)logLvl;

	if (logLvl < 0 || logLvl > LogLvl_Max)
	{
		Log("unsupport verbose level: " << logLvl << ", must be 1 ~ " << LogLvl_Max << "!");
		return false;
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