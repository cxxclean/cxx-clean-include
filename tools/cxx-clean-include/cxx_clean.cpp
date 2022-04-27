//<------------------------------------------------------------------------------
//< @file  : cxx_clean.cpp
//< @author: ������
//< @brief : ʵ��clang����������﷨���йصĸ��ֻ�����
///<------------------------------------------------------------------------------

#include "cxx_clean.h"
#include <sstream>
#include <llvm/Option/ArgList.h>
#include <clang/Driver/ToolChain.h>
#include <clang/Lex/HeaderSearch.h>
#include <clang/Basic/Version.h>
#include <clang/Driver/Driver.h>
#include <clang/Parse/ParseDiagnostic.h>
#include <clang/ASTMatchers/ASTMatchers.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/Analysis/CFG.h>
#include "tool.h"
#include "vs.h"
#include "parser.h"
#include "project.h"
#include "html_log.h"

using namespace ast_matchers;

// Ԥ����������#define��#if��#else��Ԥ����ؼ��ֱ�Ԥ����ʱʹ�ñ�Ԥ������
CxxCleanPreprocessor::CxxCleanPreprocessor(ParsingFile *rootFile)
	: m_root(rootFile)
{}

// �ļ��л�
void CxxCleanPreprocessor::FileChanged(SourceLocation loc, FileChangeReason reason, SrcMgr::CharacteristicKind FileType, FileID prevFileID /* = FileID() */)
{
	// ע�⣺��ΪPPCallbacks::EnterFileʱ��prevFileID����Ч��
	if (reason != PPCallbacks::EnterFile)
	{
		return;
	}

	FileID curFileID = m_root->GetSrcMgr().getFileID(loc);

	// ����Ҫע�⣬�е��ļ��ǻᱻFileChanged��©���ģ����ǰ�HeaderSearch::ShouldEnterIncludeFile������Ϊÿ�ζ�����true
	m_root->AddFile(curFileID);
}

// �ļ�������
void CxxCleanPreprocessor::FileSkippedWithFileID(FileID fileID)
{
	m_root->AddFile(fileID);
}

// ����꣬��#if defined DEBUG
void CxxCleanPreprocessor::Defined(const Token &macroName, const MacroDefinition &definition, SourceRange range)
{
	m_root->UseMacro(macroName.getLocation(), definition, macroName);
}

// ����#define
void CxxCleanPreprocessor::MacroDefined(const Token &macroName, const MacroDirective *direct) {}

// �걻#undef
void CxxCleanPreprocessor::MacroUndefined(const Token &macroName, const MacroDefinition &definition, const MacroDirective *Undef)
{
	m_root->UseMacro(macroName.getLocation(), definition, macroName);
}

// ����չ
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

// ���ʵ������
bool CxxCleanASTVisitor::VisitStmt(Stmt *s)
{
	SourceLocation loc = s->getBeginLoc();
	if (!loc.isValid()) {
		loc = s->getEndLoc();
	}

	if (!loc.isValid()) {
		return true;
	}

	if (m_root->IsInSystemHeader(loc))
	{
		return true;
	}

	// �μ���http://clang.llvm.org/doxygen/classStmt.html
	if (isa<CastExpr>(s))
	{
		CastExpr *castExpr = cast<CastExpr>(s);
		QualType castType = castExpr->getType();
		CastKind castKind = castExpr->getCastKind();

		switch (castKind)
		{
		// ���������໥��ת��ʱ����Ҫ���⴦��
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
	// ������ǰ��Χȡ��Ա��䣬���磺this->print();
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
	/// �ṹ���union�ĳ�Ա�����磺X->F��X.F.
	else if (isa<MemberExpr>(s))
	{
		MemberExpr *memberExpr = cast<MemberExpr>(s);
		m_root->UseValueDecl(loc, memberExpr->getMemberDecl());
	}
	// delete���
	else if (isa<CXXDeleteExpr>(s))
	{
		CXXDeleteExpr *expr = cast<CXXDeleteExpr>(s);
		m_root->UseQualType(loc, expr->getDestroyedType());
	}
	// ����ȡԪ����䣬���磺a[0]��4[a]
	else if (isa<ArraySubscriptExpr>(s))
	{
		ArraySubscriptExpr *expr = cast<ArraySubscriptExpr>(s);
		m_root->UseQualType(loc, expr->getType());
	}
	// typeid��䣬���磺typeid(int) or typeid(*obj)
	else if (isa<CXXTypeidExpr>(s))
	{
		CXXTypeidExpr *expr = cast<CXXTypeidExpr>(s);
		m_root->UseQualType(loc, expr->getType());
	}
	// �๹�����
	else if (isa<CXXConstructExpr>(s))
	{
		CXXConstructExpr *cxxConstructExpr = cast<CXXConstructExpr>(s);
		m_root->UseConstructor(loc, cxxConstructExpr->getConstructor());
	}
	// new���
	else if (isa<CXXNewExpr>(s))
	{
		CXXNewExpr *cxxNewExpr = cast<CXXNewExpr>(s);

		const FunctionDecl *operatorNew		= cxxNewExpr->getOperatorNew();
		const FunctionDecl *operatorDelete	= cxxNewExpr->getOperatorDelete();

		m_root->UseFuncDecl(loc, operatorNew);
		m_root->UseFuncDecl(loc, operatorDelete);
	}
	// delete���
	else if (isa<CXXDeleteExpr>(s))
	{
		CXXDeleteExpr *cxxDeleteExpr	= cast<CXXDeleteExpr>(s);
		const FunctionDecl *operatorDelete	= cxxDeleteExpr->getOperatorDelete();

		m_root->UseFuncDecl(loc, operatorDelete);
		m_root->UseQualType(loc, cxxDeleteExpr->getDestroyedType());
	}
	// sizeof(A)���
	else if (isa<UnaryExprOrTypeTraitExpr>(s))
	{
		UnaryExprOrTypeTraitExpr *unaryExprOrTypeTraitExpr = cast<UnaryExprOrTypeTraitExpr>(s);
		m_root->UseVarType(loc, unaryExprOrTypeTraitExpr->getTypeOfArgument());
	}
	/*
	// ע�⣺������һ��κ���Ҫ�����������Ժ���־����

	// ������DeclRefExpr��Ҫ��ʵ����ʱ��֪�����ͣ����磺T::
	else if (isa<DependentScopeDeclRefExpr>(s)){}
	// return��䣬���磺return 4;��return;
	else if (isa<ReturnStmt>(s)){}
	// ���ű��ʽ�����磺(1)��(a + b)
	else if (isa<ParenExpr>(s)){}
	// ���ϱ��ʽ���ɲ�ͬ���ʽ��϶���
	else if(isa<CompoundStmt>(s)){}
	// ��Ԫ���ʽ�����磺"x + y" or "x <= y"����������δ�����أ���������BinaryOperator���������������أ������ͽ���CXXOperatorCallExpr
	else if (isa<BinaryOperator>(s)){}
	// һԪ���ʽ�����磺����+������-������++���Լ�--,�߼��ǣ�����λȡ��~��ȡ������ַ&��ȡָ����ֵָ*��
	else if (isa<UnaryOperator>(s)){}
	// ����
	else if (isa<IntegerLiteral>(s)){}
	// ��Ԫ���ʽ�����������ʽ�����磺 x ? y : z
	else if (isa<ConditionalOperator>(s)){}
	// for��䣬���磺for(;;){ int i = 0; }
	else if (isa<ForStmt>(s)){}
	else if (isa<UnaryExprOrTypeTraitExpr>(s) || isa<InitListExpr>(s) || isa<MaterializeTemporaryExpr>(s)){}
	// ����δ֪���͵Ĺ��캯��
	else if (isa<CXXUnresolvedConstructExpr>(s)){}
	// c++11�Ĵ����չ��䣬�������...ʡ�Ժ�
	else if (isa<PackExpansionExpr>(s)){}
	else if (isa<UnresolvedLookupExpr>(s) || isa<CXXBindTemporaryExpr>(s) || isa<ExprWithCleanups>(s)){}
	else if (isa<ParenListExpr>(s)){}
	else if (isa<DeclStmt>(s)){}
	else if (isa<IfStmt>(s) || isa<SwitchStmt>(s) || isa<CXXTryStmt>(s) || isa<CXXCatchStmt>(s) || isa<CXXThrowExpr>(s)){}
	else if (isa<StringLiteral>(s) || isa<CharacterLiteral>(s) || isa<CXXBoolLiteralExpr>(s) || isa<FloatingLiteral>(s)){}
	else if (isa<NullStmt>(s)){}
	else if (isa<CXXDefaultArgExpr>(s)){}
	//	����c++�ĳ�Ա������䣬���ʿ�������ʽ����ʽ
	else if (isa<UnresolvedMemberExpr>(s)){}
	else
	{
		log() << "<pre>------------ havn't support stmt ------------:</pre>\n";
		PrintStmt(s);
	}
	*/

	return true;
}

// ���ʺ�������
bool CxxCleanASTVisitor::VisitFunctionDecl(FunctionDecl *f)
{
	SourceLocation loc = f->getBeginLoc();
	if (m_root->IsInSystemHeader(loc))
	{
		return true;
	}

	// ����������������ֵ��
	m_root->UseFuncDecl(loc, f);

	// ע�⣺���ຯ�����Է��������������������ע����ĺ�������
	const FunctionDecl *oldestFunction = nullptr;

	for (const FunctionDecl *prevFunc = f->getPreviousDecl(); prevFunc; prevFunc = prevFunc->getPreviousDecl())
	{
		oldestFunction = prevFunc;
	}

	if (oldestFunction)
	{
		// ���ú���ԭ��
		m_root->UseFuncDecl(loc, oldestFunction);
	}

	// �Ƿ�����ĳ����ĳ�Ա����
	if (isa<CXXMethodDecl>(f))
	{
		CXXMethodDecl *method = cast<CXXMethodDecl>(f);
		if (nullptr == method)
		{
			return false;
		}

		// �����ҵ��ó�Ա����������struct/union/class.
		CXXRecordDecl *record = method->getParent();
		if (nullptr == record)
		{
			return false;
		}

		// ���ö�Ӧ��struct/union/class.
		m_root->UseRecord(loc,	record);
	}

	return true;
}

// ����class��struct��union��enumʱ
bool CxxCleanASTVisitor::VisitCXXRecordDecl(CXXRecordDecl *r)
{
	if (!r->hasDefinition())
	{
		return true;
	}

	SourceLocation loc = r->getBeginLoc();
	if (m_root->IsInSystemHeader(loc))
	{
		return true;
	}

	// ģ���ػ����������£�
	//	template<typename T> class array;
	//	
	//	template<> class array<bool> { }; // class template specialization array<bool>
	if (const ClassTemplateSpecializationDecl *Spec = dyn_cast<ClassTemplateSpecializationDecl>(r))
	{
		m_root->UseTemplateDecl(loc, Spec->getSpecializedTemplate());
	}

	// �������л���
	for (CXXRecordDecl::base_class_iterator itr = r->bases_begin(), end = r->bases_end(); itr != end; ++itr)
	{
		CXXBaseSpecifier &base = *itr;
		m_root->UseQualType(loc, base.getType());
	}

	// ������Ա������ע�⣺������static��Ա������static��Ա��������VisitVarDecl�б����ʵ���
	for (CXXRecordDecl::field_iterator itr = r->field_begin(), end = r->field_end(); itr != end; ++itr)
	{
		FieldDecl *field = *itr;
		m_root->UseValueDecl(loc, field);
	}

	m_root->UseQualifier(loc, r->getQualifier());

	// ��Ա��������Ҫ�������������ΪVisitFunctionDecl������ʳ�Ա����
	return true;
}

// �����ֱ�������ʱ�ýӿڱ�����
bool CxxCleanASTVisitor::VisitVarDecl(VarDecl *var)
{
	SourceLocation loc = var->getBeginLoc();
	if (m_root->IsInSystemHeader(loc))
	{
		return true;
	}

	// ע�⣺������������1. �����β� 2. �����Ա�ı������� 3. ���Ա������static���η����ȵ�

	// ���ñ���������
	m_root->UseVarDecl(loc, var, var->getQualifier());

	// ���static��Ա������֧��ģ����ĳ�Ա��
	if (var->isCXXClassMember())
	{
		// ��Ϊstatic��Ա������ʵ�֣���������������ע��Ҳ������isStaticDataMember�������ж�var�Ƿ�Ϊstatic��Ա������
		const VarDecl *prevVar = var->getPreviousDecl();
		m_root->UseVarDecl(loc, prevVar, var->getQualifier());
	}

	if (var->hasExternalStorage())
	{
		for (const VarDecl *next : var->redecls())
		{
			if (!next->hasExternalStorage())
			{
				m_root->UseVarDecl(next->getBeginLoc(), var, var->getQualifier());
			}
		}
	}

	return true;
}

// ���磺typedef int A;
bool CxxCleanASTVisitor::VisitTypedefDecl(clang::TypedefDecl *d)
{
	SourceLocation loc = d->getBeginLoc();
	if (m_root->IsInSystemHeader(loc))
	{
		return true;
	}

	m_root->UseQualType(loc, d->getUnderlyingType());
	return true;
}

// ���磺namespace A{}
bool CxxCleanASTVisitor::VisitNamespaceDecl(clang::NamespaceDecl *d)
{
	SourceLocation loc = d->getBeginLoc();
	if (m_root->IsInSystemHeader(loc))
	{
		return true;
	}

	m_root->DeclareNamespace(d);
	return true;
}

// ���磺namespace s = std;
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

// ���磺using namespace std;
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

// ���磺using std::string;
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

// ���ʳ�Ա����
bool CxxCleanASTVisitor::VisitFieldDecl(FieldDecl *decl)
{
	SourceLocation loc = decl->getBeginLoc();
	if (m_root->IsInSystemHeader(loc))
	{
		return true;
	}

	m_root->UseValueDecl(decl->getBeginLoc(), decl);
	return true;
}

// ��������
bool CxxCleanASTVisitor::VisitCXXConstructorDecl(CXXConstructorDecl *constructor)
{
	SourceLocation loc = constructor->getBeginLoc();
	if (m_root->IsInSystemHeader(loc))
	{
		return true;
	}

	m_root->UseConstructor(constructor->getBeginLoc(), constructor);
	return true;
}

// �������
bool CxxCleanASTVisitor::VisitCXXConstructExpr(CXXConstructExpr *expr)
{
	SourceLocation loc = expr->getBeginLoc();
	if (m_root->IsInSystemHeader(loc))
	{
		return true;
	}

	m_root->UseConstructor(expr->getBeginLoc(), expr->getConstructor());
	return true;
}

CxxCleanASTConsumer::CxxCleanASTConsumer(ParsingFile *rootFile)
	: m_root(rootFile), m_visitor(rootFile)
{}

// ���ǣ�������������ķ���
bool CxxCleanASTConsumer::HandleTopLevelDecl(DeclGroupRef declgroup)
{
	return true;
}

// �����������ÿ��Դ�ļ���������һ�Σ����磬����һ��hello.cpp��#include�����ͷ�ļ���Ҳֻ�����һ�α�����
void CxxCleanASTConsumer::HandleTranslationUnit(ASTContext& context)
{
	// ���ڵ��ԣ���ӡ�﷨��
	if (Project::instance.m_logLvl >= LogLvl_Max)
	{
		std::string strLog;
		raw_string_ostream logStream(strLog);
		context.getTranslationUnitDecl()->dump(logStream);
		logStream.flush();

		log() << "<div class=\"box\"><div><dl><dd><div><span class=\"bold\">------------ HandleTranslationUnit begin ------------</span>";
		log() << m_root->DebugBeIncludeText(m_root->GetSrcMgr().getMainFileID());
		log() << "<pre>";
		log() << escape_html(strLog);
		log() << "</pre><span class=\"bold\">------------ HandleTranslationUnit end ------------</span></div></dd></dl></div></div>\n";
	}

	// 1. ��ǰcpp�ļ�������ʼ
	m_root->Begin();

	// 2. �����﷨��
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

// ��һ��������ʱ������ô˺�����������������¼�������ʹ����
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

// ��ʼ�ļ�����
bool CxxCleanAction::BeginSourceFileAction(CompilerInstance &compiler)
{
	string filename = "";
	++ProjectHistory::instance.g_fileNum;
	Log("cleaning file: " << ProjectHistory::instance.g_fileNum << "/" << Project::instance.m_cpps.size() << ". " << filename << " ...");
	return true;
}

// �����ļ�����
void CxxCleanAction::EndSourceFileAction()
{
	m_root->End();
	delete m_root;
	m_root = nullptr;
}

// ���������﷨��������
std::unique_ptr<ASTConsumer> CxxCleanAction::CreateASTConsumer(CompilerInstance &compiler, StringRef file)
{
	m_root = new ParsingFile(compiler);

	compiler.getPreprocessor().addPPCallbacks(std::make_unique<CxxCleanPreprocessor>(m_root));
	return std::make_unique<CxxCleanASTConsumer>(m_root);
}

static llvm::cl::OptionCategory g_optionCategory("cxx-clean-include category");

static cl::opt<string>	g_vsOption		("vs", cl::desc("clean visual studio project(version 2005 or upper), format:-vs ./hello.vcproj or -vs ./hello.vcxproj\n"), cl::cat(g_optionCategory));
static cl::opt<bool>	g_noOverWrite	("no", cl::desc("means no overwrite, all c++ file will not be changed"), cl::cat(g_optionCategory));
static cl::opt<bool>	g_onlyCleanCpp	("onlycpp", cl::desc("only allow clean cpp file(cpp, cc, cxx), don't clean the header file(h, hxx, hh)"), cl::cat(g_optionCategory));
static cl::opt<bool>	g_printVsConfig	("print-vs", cl::desc("print vs configuration"), cl::cat(g_optionCategory));
static cl::opt<int>		g_logLevel		("v", cl::desc("log level(verbose level), level can be 0 ~ 4, default is 1, higher level will print more detail"), cl::cat(g_optionCategory));
static cl::list<string>	g_skips			("skip", cl::desc("skip files"), cl::cat(g_optionCategory));
static cl::opt<string>	g_cleanOption	("clean",
        cl::desc("format:\n"
                 "    1. clean directory: -clean ../hello/\n"
                 "    2. clean a c++ file: -clean ./hello.cpp\n"
                ), cl::cat(g_optionCategory));

void PrintVersion(raw_ostream &o) {
	llvm::outs() << clang::getClangToolFullVersion("clang lib version: ") << '\n';
}

// ����ѡ����������������Ӧ�Ķ�����Ӧ��;�˳��򷵻�true�����򷵻�false
bool CxxCleanOptionsParser::ParseOptions(int &argc, const char **argv)
{
	// ʹ��-helpѡ��ʱ������ӡ�����ߵ�ѡ��
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

	// ���º����ļ�-skipѡ���ֵ
	Add(Project::instance.m_skips, g_skips);

	HtmlLog::instance->Open();

	if (g_printVsConfig)
	{
		VsProject::instance.Print();
		return false;
	}

	Project::instance.Print();
	return true;
}

// ��ִ���������в�����"--"�ָ���ǰ��������в������������߽���������������в�������clang�����
FixedCompilationDatabase *CxxCleanOptionsParser::SplitCommandLine(int &argc, const char *const *argv, Twine directory /* = "." */)
{
	const char *const *doubleDash = std::find(argv, argv + argc, StringRef("--"));
	if (doubleDash == argv + argc)
	{
		return new clang::tooling::FixedCompilationDatabase(directory, std::vector<std::string>());
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

// ���clang����
void CxxCleanOptionsParser::AddClangArgument(ClangTool &tool, const char *arg) const
{
	ArgumentsAdjuster argAdjuster = getInsertArgumentAdjuster(arg, ArgumentInsertPosition::BEGIN);
	tool.appendArgumentsAdjuster(argAdjuster);
}

// �������������clang����
void CxxCleanOptionsParser::AddClangArgumentByOption(ClangTool &tool) const
{
	// ����vs�����ļ�����clang�Ĳ���
	AddVsArgument(VsProject::instance, tool);

	// ��ֻ��һ���ļ�
	if (Project::instance.m_cpps.size() == 1)
	{
		const std::string &file = Project::instance.m_cpps[0];

		// ����ͷ�ļ�
		if (cpptool::is_header(file))
		{
			// ��ֻ����ͷ�ļ�����������������clang���ܳɹ�����
			AddClangArgument(tool, "-xc++-header");
		}
	}
}

// ���ϵͳͷ�ļ�����·��
void CxxCleanOptionsParser::AddSystemHeaderSearchPath(ClangTool &tool, const char *path) const
{
	const std::string arg = std::string("-isystem") + path;
	AddClangArgument(tool, arg.c_str());
}

// ����û�ͷ�ļ�����·��
void CxxCleanOptionsParser::AddHeaderSearchPath(ClangTool &tool, const char *path) const
{
	const std::string arg = std::string("-I") + path;
	AddClangArgument(tool, arg.c_str());
}

// ����vs�����ļ�����clang�Ĳ���
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

// ��ȡvisual studio�İ�װ·��
std::string CxxCleanOptionsParser::GetVsInstallDir() const
{
	std::string vsInstallDir;

#if defined(LLVM_ON_WIN64)
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

// ���visual studio�Ķ���İ���·������Ϊclang����©��һ������·���ᵼ��#include <atlcomcli.h>ʱ�Ҳ���ͷ�ļ�
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

	HtmlLog::instance = new HtmlLog();

	// ����-vs������-vsָ����Ӧ�����visual studio�����ļ���
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

			std::wstring htmlPath = strtool::s2ws(vsPath) + std::wstring(cn_log_name_project);
			std::string htmlTitle = string(cn_project1) + vsOption + cn_project2;
			std::string tip = string(cn_project1) + get_file_html(vsOption.c_str()) + cn_project2;
			HtmlLog::instance->Init(htmlPath, htmlTitle, tip);
		}
		else
		{
			Log("unsupport parsed visual studio project <" << vsOption << ">!");
			return false;
		}
	}

	// ����-clean������-clean��������Щcpp�ļ�Ӧ������
	if (!clean_option.empty())
	{
		if (!pathtool::exist(clean_option))
		{
			Log("error: parse argument -clean " << clean_option << " failed, not found the directory or c++ file.");
			return false;
		}

		// �ļ���
		if (llvm::sys::fs::is_directory(clean_option))
		{
			std::string directory = pathtool::get_absolute_path(clean_option.c_str());
			if (!strtool::end_with(directory, "/"))
			{
				directory += "/";
			}

			// �г����ļ����������ļ�
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

			// -vsѡ��Ϊ��
			if (vsOption.empty())
			{
				project.m_cpps = all;

				std::string logFile = directory;
				strtool::replace(logFile, "/", "_");
				strtool::replace(logFile, ".", "_");

				
				HtmlLog::instance->Init(cn_log_name_folder1 + strtool::s2ws(logFile) + cn_log_name_folder2,
					strtool::ws2s(cn_log_name_folder1) + directory + strtool::ws2s(cn_log_name_folder2),
					strtool::ws2s(cn_log_name_folder1) + get_file_html(directory.c_str()) + strtool::ws2s(cn_log_name_folder2));
			}
			// ����-vs����
			else
			{
				// �ҳ����ļ�����������Ŀ��cpp
				FileNameSet vsCpps(project.m_cpps.begin(), project.m_cpps.end());
				project.m_cpps.clear();

				for (const std::string &file : all)
				{
					if (Has(vsCpps, file))
					{
						// ���������ڸ��ļ��е���Ŀcpp��Ա
						project.m_cpps.push_back(file);
					}
				}
			}
		}
		// �ļ�
		else
		{
			std::string filePath = pathtool::get_lower_absolute_path(clean_option.c_str());
			project.m_cpps.clear();
			project.m_cpps.push_back(filePath);
			project.m_canCleanFiles.insert(filePath);

			std::string fileName = pathtool::get_file_name(filePath.c_str());

			HtmlLog::instance->Init(cn_log_name_cpp_file1 + strtool::s2ws(fileName) + cn_log_name_cpp_file2,
				strtool::ws2s(cn_log_name_cpp_file1) + filePath + strtool::ws2s(cn_log_name_cpp_file2),
				strtool::ws2s(cn_log_name_cpp_file1) + get_file_html(filePath.c_str()) + strtool::ws2s(cn_log_name_cpp_file2));
		}

		project.Fix();
	}

	// ����-onlycpp����ѡ���ʱ������������Դ�ļ�����ֹ����ͷ�ļ���
	if (g_onlyCleanCpp)
	{
		project.m_canCleanFiles.clear();
		Add(project.m_canCleanFiles, project.m_cpps);
	}

	if (vsOption == clean_option && !vsOption.empty())
	{
		Log("error! need select c++ file of visual studio project <" << vsOption << ">!");
		return false;
	}

	return true;
}

// ����-vѡ��
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
    "\n            cxxclean -vs hello.vcproj"
    "\n"
    "\n        -> only want to clean a single c++ file, and using the vs configuration:"
    "\n            cxxclean -vs hello.vcproj -clean hello.cpp"
    "\n"
    "\n    2. for a directory"
    "\n"
    "\n        -> clean all c++ file in the directory:"
    "\n            cxxclean -clean ./hello"
    "\n"
    "\n        -> only clean a single c++ file, you can use:"
    "\n            cxxclean -clean hello.cpp"
);