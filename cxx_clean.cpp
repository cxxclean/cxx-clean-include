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
#include "clang/Basic/Version.h"

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
			if (reason != PPCallbacks::ExitFile)
			{
				// 注意：PPCallbacks::EnterFile时的prevFileID无效，无法有效利用所以不予考虑
				return;
			}

			/*
				关于函数参数的含义，以a.cpp中有#include "b.h"为例

				则此处，prevFileID代表b.h，loc则表示a.cpp中#include "b.h"最后一个双引号的后面一个位置

				例如：
			        #include "b.h"
			                      ^
			                      loc代表的位置
			*/

			SourceManager &srcMgr		= m_main->GetSrcMgr();

			FileID curFileID = srcMgr.getFileID(loc);
			if (!prevFileID.isValid() || !curFileID.isValid())
			{
				return;
			}

			m_main->AddFile(prevFileID);
			m_main->AddFile(curFileID);

			PresumedLoc presumed_loc	= srcMgr.getPresumedLoc(loc);
			string curFileName			= presumed_loc.getFilename();

			if (curFileName.empty() || nullptr == srcMgr.getFileEntryForID(prevFileID))
			{
				// llvm::outs() << "    now: " << presumed_loc.getFilename() << " line = " << presumed_loc.getLine() << ", exit = " << m_main->get_file_name(prevFileID) << ", loc = " << m_main->debug_loc_text(loc) << "\n";
				return;
			}

			if (curFileName[0] == '<' || *curFileName.rbegin() == '>')
			{
				curFileID = srcMgr.getMainFileID();
			}

			if (prevFileID == curFileID)
			{
				// llvm::outs() << "same file\n";
				return;
			}

			m_main->AddParent(prevFileID, curFileID);
			m_main->AddInclude(curFileID, prevFileID);
		}

		void FileSkipped(const FileEntry &SkippedFile,
		                 const Token &FilenameTok,
		                 SrcMgr::CharacteristicKind FileType) override
		{
		}

		// 处理#include
		void InclusionDirective(SourceLocation HashLoc /*#的位置*/, const Token &includeTok,
		                        StringRef fileName, bool isAngled/*是否被<>包含，否则是被""包围*/, CharSourceRange filenameRange,
		                        const FileEntry *file, StringRef searchPath, StringRef relativePath, const clang::Module *Imported) override
		{
			// 当编译时采用-include<c++头文件>选项，但却又找不到该头文件时，将导致file无效
			if (nullptr == file)
			{
				return;
			}

			FileID curFileID = m_main->GetSrcMgr().getFileID(HashLoc);
			if (!curFileID.isValid())
			{
				return;
			}

			SourceRange range(HashLoc, filenameRange.getEnd());

			m_main->AddIncludeLoc(filenameRange.getAsRange().getBegin(), range);
		}

		// 定义宏，如#define DEBUG
		void Defined(const Token &macroName, const MacroDefinition &md, SourceRange range) override
		{
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
			m_main->UseMacro(range.getBegin(), definition, macroName);
			// llvm::outs() << "text = " << m_main->m_rewriter->getRewrittenText(range) << "\n";
			// llvm::outs() << "text = " << m_main->get_source_of_range(range) << "\n";
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

		// 访问单条语句
		bool VisitStmt(Stmt *s)
		{
			SourceLocation loc = s->getLocStart();

			// 参见：http://clang.llvm.org/doxygen/classStmt.html
			if (isa<CastExpr>(s))
			{
				CastExpr *castExpr = cast<CastExpr>(s);
				QualType castType = castExpr->getType();

				m_main->UseQualType(loc, castType);

				// 这里注意，CastExpr有一个getSubExpr()函数，表示子表达式，但此处不需要继续处理，因为子表达式将作为Stmt仍然会被VisitStmt访问到
			}
			else if (isa<CallExpr>(s))
			{
				CallExpr *callExpr = cast<CallExpr>(s);
				Expr *callee = callExpr->getCallee();
				Decl *calleeDecl = callExpr->getCalleeDecl();

				if (NULL == calleeDecl)
				{
					return true;
				}

				if (isa<FunctionDecl>(calleeDecl))
				{
					FunctionDecl *func = cast<FunctionDecl>(calleeDecl);
					m_main->OnUseDecl(loc, func);
				}
			}
			else if (isa<DeclRefExpr>(s))
			{
				DeclRefExpr *declRefExpr = cast<DeclRefExpr>(s);
				m_main->OnUseDecl(loc, declRefExpr->getDecl());
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
			else if (isa<UnaryExprOrTypeTraitExpr>(s))
			{
			}
			/// 结构体或union的成员，例如：X->F、X.F.
			else if (isa<MemberExpr>(s))
			{
				MemberExpr *memberExpr = cast<MemberExpr>(s);
				m_main->OnUseDecl(loc, memberExpr->getMemberDecl());
			}
			else if (isa<DeclStmt>(s))
			{
			}
			else if (isa<IfStmt>(s) || isa<SwitchStmt>(s))
			{
			}
			else if (isa<CXXConstructExpr>(s))
			{
				CXXConstructExpr *cxxConstructExpr = cast<CXXConstructExpr>(s);
				CXXConstructorDecl *decl = cxxConstructExpr->getConstructor();
				if (nullptr == decl)
				{
					llvm::errs() << "------------ CXXConstructExpr->getConstructor() = null ------------:\n";
					s->dumpColor();
					return false;
				}

				m_main->OnUseDecl(loc, decl);
			}
			else
			{
				// llvm::errs() << "------------ havn't support stmt ------------:\n";
				// s->dumpColor();
			}

			return true;
		}

		bool VisitFunctionDecl(FunctionDecl *f)
		{
			//			if (m_main->m_srcMgr->isInMainFile(f->getLocStart()))
			//			{
			//				f->dumpColor();
			//			}

			// 识别返回值类型
			{
				// 函数的返回值
				QualType returnType = f->getReturnType();
				m_main->UseVar(f->getLocStart(), returnType);
			}

			// 识别函数参数
			{
				// 依次遍历参数，建立引用关系
				for (FunctionDecl::param_iterator itr = f->param_begin(), end = f->param_end(); itr != end; ++itr)
				{
					ParmVarDecl *vardecl = *itr;
					QualType vartype = vardecl->getType();
					m_main->UseVar(f->getLocStart(), vartype);
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

					// 尝试找到该成员函数所属的struct/union/class.
					CXXRecordDecl *record = method->getParent();
					if (nullptr == record)
					{
						return false;
					}

					// 引用对应的struct/union/class.
					m_main->OnUseRecord(f->getLocStart(),	record);
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
				m_main->UseVar(r->getLocStart(), field->getType());
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

			// var->dumpColor();
			//llvm::outs() << "\n" << "VisitVarDecl" << m_main->(var->getTypeSourceInfo()->getTypeLoc().getLocStart()) << "\n";

			// 引用变量的类型
			m_main->UseVar(var->getLocStart(), var->getType());

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
				if (prevVar)
				{
					m_main->OnUseDecl(var->getLocStart(), prevVar);
				}
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



	private:
		ParsingFile*	m_main;
		Rewriter&	m_rewriter;
	};

	// 遍历声明，实现ASTConsumer接口用于读取clang解析器生成的ast语法树
	class ListDeclASTConsumer : public ASTConsumer
	{
	public:
		ListDeclASTConsumer(Rewriter &r, ParsingFile *mainFile) : m_main(mainFile), m_visitor(r, mainFile) {}

		// 覆盖：遍历最顶层声明的方法
		bool HandleTopLevelDecl(DeclGroupRef declgroup) override
		{
			return true;
		}

		// 这个函数对于每个源文件仅被调用一次，比如，若有一个hello.cpp中#include了许多头文件，也只会调用一次本函数
		void HandleTranslationUnit(ASTContext& context) override
		{
			m_visitor.TraverseDecl(context.getTranslationUnitDecl());
		}

	public:
		ParsingFile*	m_main;
		DeclASTVisitor	m_visitor;
	};

	// 对于tool接收到的每个源文件，都将new一个ListDeclAction
	class ListDeclAction : public ASTFrontendAction
	{
	public:
		ListDeclAction()
		{
		}

		bool BeginSourceFileAction(CompilerInstance &CI, StringRef Filename) override
		{
			if (ProjectHistory::instance.m_isFirst)
			{
				bool only1Step = (!Project::instance.m_isDeepClean || Project::instance.m_onlyHas1File);
				if (only1Step)
				{
					llvm::errs() << "cleaning file: " << Filename << " ...\n";
				}
				else
				{
					llvm::errs() << "step 1. analyze file: " << Filename << " ...\n";
				}
			}
			else
			{
				llvm::errs() << "step 2. cleaning file: " << Filename << " ...\n";
			}

			return true;
		}

		void EndSourceFileAction() override
		{
			SourceManager &srcMgr	= m_rewriter.getSourceMgr();
			ASTConsumer &consumer	= this->getCompilerInstance().getASTConsumer();
			ListDeclASTConsumer *c	= (ListDeclASTConsumer*)&consumer;

			// llvm::errs() << "** EndSourceFileAction for: " << srcMgr.getFileEntryForID(srcMgr.getMainFileID())->getName() << "\n";
			// CompilerInstance &compiler = this->getCompilerInstance();

			c->m_main->GenerateResult();

			{
				if (ProjectHistory::instance.m_isFirst || Project::instance.m_onlyHas1File)
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
			}

			// llvm::errs() << "end clean file: " << c->m_main->get_file_name(srcMgr.getMainFileID()) << "\n";

			// Now emit the rewritten buffer.
			// m_rewriter.getEditBuffer(srcMgr.getMainFileID()).write(llvm::errs());
			delete c->m_main;
		}

		std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &compiler, StringRef file) override
		{
			m_rewriter.setSourceMgr(compiler.getSourceManager(), compiler.getLangOpts());

			ParsingFile *parsingCpp	= new ParsingFile(m_rewriter, compiler);
			parsingCpp->Init();

			compiler.getPreprocessor().addPPCallbacks(llvm::make_unique<CxxCleanPreprocessor>(parsingCpp));
			return llvm::make_unique<ListDeclASTConsumer>(m_rewriter, parsingCpp);
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

		if (Project::instance.m_cpps.size() < 10)
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

	bool AddCleanVsArgument(const Vsproject &vs, ClangTool &tool)
	{
		if (vs.m_configs.empty())
		{
			return false;
		}

		const VsConfiguration &vsconfig = vs.m_configs[0];

		for (int i = 0, size = vsconfig.searchDirs.size(); i < size; i++)
		{
			const std::string &dir	= vsconfig.searchDirs[i];
			std::string arg			= "-I" + dir;

			ArgumentsAdjuster argAdjuster = getInsertArgumentAdjuster(arg.c_str(), ArgumentInsertPosition::BEGIN);
			tool.appendArgumentsAdjuster(argAdjuster);
		}

		for (int i = 0, size = vsconfig.forceIncludes.size(); i < size; i++)
		{
			const std::string &force_include	= vsconfig.forceIncludes[i];
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

		pathtool::cd(vs.m_project_dir.c_str());
		return true;
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

	std::unique_ptr<FrontendActionFactory> factory = newFrontendActionFactory<cxxcleantool::ListDeclAction>();
	tool.run(factory.get());

	if (Project::instance.m_isDeepClean && !Project::instance.m_onlyHas1File && Project::instance.m_isOverWrite)
	{
		ProjectHistory::instance.m_isFirst = false;

		tool.run(factory.get());
	}

	ProjectHistory::instance.Print();
	return 0;
}