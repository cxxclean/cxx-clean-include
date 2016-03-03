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
#include "clang/Basic/Version.h"

#include "tool.h"
#include "vs_config.h"
#include "parsing_cpp.h"

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
	static llvm::cl::OptionCategory g_optionCategory("List Decl");

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
			SourceManager &srcMgr = m_main->m_rewriter->getSourceMgr();
			if (reason != PPCallbacks::ExitFile)
			{
				// 注意：PPCallbacks::EnterFile时的prevFileID无效，无法有效利用所以不予考虑
				return;
			}

			/*
				假设有a.cpp中#include "b.h"，则此处，prevFileID代表b.h
				loc则表示a.cpp中#include "b.h"最后一个双引号的后面一个位置，如下
				#include "b.h"
							  ^
							  loc的位置
			*/

			FileID curFileID = srcMgr.getFileID(loc);
			if (!prevFileID.isValid() || !curFileID.isValid())
			{
				return;
			}

			if (prevFileID.isValid())
			{
				m_main->m_files[m_main->get_absolute_file_name(prevFileID)] = prevFileID;
			}

			if (curFileID.isValid())
			{
				m_main->m_files[m_main->get_absolute_file_name(curFileID)] = curFileID;
			}


			PresumedLoc presumed_loc = srcMgr.getPresumedLoc(loc);
			string curFileName = presumed_loc.getFilename();

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

			m_main->m_parentIDs[prevFileID] = curFileID;

			if (srcMgr.getMainFileID() == curFileID)
			{
				m_main->m_topIncludeIDs.push_back(prevFileID);
			}
		}

		void FileSkipped(const FileEntry &SkippedFile,
		                 const Token &FilenameTok,
		                 SrcMgr::CharacteristicKind FileType) override
		{
		}

		// 处理#include
		void InclusionDirective(SourceLocation HashLoc /*#的位置*/, const Token &includeTok,
		                        StringRef fileName, bool isAngled/*是否被<>包含，否则是被""包围*/, CharSourceRange filenameRange,
		                        const FileEntry *file, StringRef searchPath, StringRef relativePath, const Module *Imported) override
		{
			// 当编译时采用-include<c++头文件>选项，但却又找不到该头文件时，将导致file无效
			if (nullptr == file)
			{
				return;
			}

			FileID curFileID = m_main->m_srcMgr->getFileID(HashLoc);
			if (!curFileID.isValid())
			{
				return;
			}

			SourceRange range(HashLoc, filenameRange.getEnd());

			m_main->m_locToFileName[filenameRange.getAsRange().getBegin()] = file->getName();
			m_main->m_locToRange[filenameRange.getAsRange().getBegin()] = range;

			if (isAngled)
			{

			}

			// llvm::outs() << "file relativePath = " << relativePath << "\n";
		}

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
			m_main->use_macro(macroName.getLocation(), definition, macroName);
		}

		/// \brief Called by Preprocessor::HandleMacroExpandedIdentifier when a
		/// macro invocation is found.
		void MacroExpands(const Token &macroName,
		                  const MacroDefinition &definition,
		                  SourceRange range,
		                  const MacroArgs *args) override
		{
			m_main->use_macro(range.getBegin(), definition, macroName);
			// llvm::outs() << "text = " << m_main->m_rewriter->getRewrittenText(range) << "\n";
			// llvm::outs() << "text = " << m_main->get_source_of_range(range) << "\n";
		}

		// #ifdef
		void Ifdef(SourceLocation loc, const Token &macroName, const MacroDefinition &definition) override
		{
			m_main->use_macro(loc, definition, macroName);
		}

		// #ifndef
		void Ifndef(SourceLocation loc, const Token &macroName, const MacroDefinition &definition) override
		{
			m_main->use_macro(loc, definition, macroName);
		}

		ParsingFile *m_main;
	};

	// 通过实现RecursiveASTVisitor，自定义访问到c++抽象语法树时的操作
	class DeclASTVisitor : public RecursiveASTVisitor<DeclASTVisitor>
	{
	public:
		DeclASTVisitor(Rewriter &rewriter, ParsingFile *mainFile)
			: m_rewriter(rewriter), m_main(mainFile)
		{
		}

		void ModifyFunc(FunctionDecl *f)
		{
			// 函数名
			DeclarationName declName = f->getNameInfo().getName();
			std::string funcName = declName.getAsString();

			if (f->isTemplateDecl() || f->isLateTemplateParsed())
			{
				// 在前面加上注释
				std::stringstream before;
				before << "// function <" << funcName << "> is template\n";
				SourceLocation location = f->getSourceRange().getBegin();
				m_rewriter.InsertText(location, before.str(), true, true);
			}

			if (f->isReferenced() || f->isThisDeclarationReferenced() || f->isUsed())
			{
				// 在前面加上注释
				std::stringstream before;
				before << "// function <" << funcName << "> is referenced\n";
				SourceLocation location = f->getSourceRange().getBegin();
				m_rewriter.InsertText(location, before.str(), true, true);
			}

			// 函数实现
			if (f->hasBody())
			{
				Stmt *funcBody = f->getBody();
				if (NULL == funcBody)
				{
					return;
				}

				// 函数的返回值
				QualType returnType = f->getReturnType();

				// 在前面加上注释
				std::stringstream before;
				before << "// Begin function " << funcName << " returning " << returnType.getAsString() << "\n";
				SourceLocation location = f->getSourceRange().getBegin();
				m_rewriter.InsertText(location, before.str(), true, true);

				// 在后面加上注释
				std::stringstream after;
				after << "\n// End function " << funcName;
				location = funcBody->getLocEnd().getLocWithOffset(1);
				m_rewriter.InsertText(location, after.str(), true, true);
			}
			// 函数声明，不含实现
			else
			{
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
		}

		bool ModifyStmt(Stmt *s)
		{
			// Only care about If statements.
			if (isa<IfStmt>(s))
			{
				IfStmt *IfStatement = cast<IfStmt>(s);
				Stmt *Then = IfStatement->getThen();

				m_rewriter.InsertText(Then->getLocStart(), "// the 'if' part\n", true,
				                      true);

				Stmt *Else = IfStatement->getElse();
				if (Else)
					m_rewriter.InsertText(Else->getLocStart(), "// the 'else' part\n",
					                      true, true);
			}
			else if (isa<DeclStmt>(s))
			{
				DeclStmt *decl = cast<DeclStmt>(s);

				m_rewriter.InsertText(decl->getLocStart(), "// DeclStmt begin\n", true, true);
				m_rewriter.InsertText(decl->getLocEnd(), "// DeclStmt end\n", true, true);
			}
			else if (isa<CastExpr>(s))
			{
				CastExpr *castExpr = cast<CastExpr>(s);

				std::stringstream before;
				before << "// begin ImplicitCastExpr " << castExpr->getCastKindName() << "\n";

				std::stringstream after;
				after << "// end ImplicitCastExpr " << castExpr->getCastKindName() << "\n";

				m_rewriter.InsertText(castExpr->getLocStart(), before.str() , true, true);
				m_rewriter.InsertText(castExpr->getLocEnd(), after.str() , true, true);

				Expr *subExpr = castExpr->getSubExpr();

				if (isa<DeclRefExpr>(subExpr))
				{
					DeclRefExpr *declExpr = cast<DeclRefExpr>(subExpr);

					std::stringstream before;
					before << "// begin DeclRefExpr " << declExpr->getDecl()->getNameAsString() << "\n";

					std::stringstream after;
					after << "// end DeclRefExpr " << declExpr->getDecl()->getNameAsString() << "\n";

					m_rewriter.InsertText(declExpr->getLocStart(), before.str() , true, true);
					m_rewriter.InsertText(declExpr->getLocEnd(), after.str() , true, true);
				}
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
					std::stringstream before;
					before << "// begin CallExpr " << callExpr->getDirectCallee()->getNameAsString() << "\n";

					std::stringstream after;
					after << "// end CallExpr " << callExpr->getDirectCallee()->getNameAsString() << "\n";

					m_rewriter.InsertText(callExpr->getLocStart(), before.str() , true, true);
					m_rewriter.InsertText(callExpr->getLocEnd().getLocWithOffset(1), after.str() , true, true);
				}
			}

			return true;
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

				m_main->use_type(loc, castType);

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
					m_main->on_use_decl(loc, func);
				}
			}
			else if (isa<DeclRefExpr>(s))
			{
				DeclRefExpr *declRefExpr = cast<DeclRefExpr>(s);
				m_main->on_use_decl(loc, declRefExpr->getDecl());
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
				m_main->on_use_decl(loc, memberExpr->getMemberDecl());
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

				m_main->on_use_decl(loc, decl);
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
				m_main->use_var(f->getLocStart(), returnType);
			}

			// 识别函数参数
			{
				// 依次遍历参数，建立引用关系
				for (FunctionDecl::param_iterator itr = f->param_begin(), end = f->param_end(); itr != end; ++itr)
				{
					ParmVarDecl *vardecl = *itr;
					QualType vartype = vardecl->getType();
					m_main->use_var(f->getLocStart(), vartype);
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
					m_main->on_use_record(f->getLocStart(),	record);
				}
			}

			// ModifyFunc(f);
			return true;
		}

		bool ModifyCXXRecordDecl(CXXRecordDecl *r)
		{
			if (!r->hasDefinition())
			{
				return true;
			}

			// 遍历所有基类
			for (CXXRecordDecl::base_class_iterator itr = r->bases_begin(), end = r->bases_end(); itr != end; ++itr)
			{
				CXXBaseSpecifier &base = *itr;
			}

			// 遍历成员变量（注意：不包括static成员变量，static成员变量将在VisitVarDecl中被访问到）
			for (CXXRecordDecl::field_iterator itr = r->field_begin(), end = r->field_end(); itr != end; ++itr)
			{
				FieldDecl *field = *itr;

				// 在前面加上注释
				std::stringstream before;
				before << "// member <" << field->getNameAsString() << "> type is " << field->getType().getAsString() << "\n";
				SourceLocation location = field->getSourceRange().getBegin();
				m_rewriter.InsertText(location, before.str(), true, true);
			}

			// 成员函数不需要在这里遍历，因为VisitFunctionDecl将会访问成员函数
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
				m_main->use_type(r->getLocStart(), base.getType());
			}

			// 遍历成员变量（注意：不包括static成员变量，static成员变量将在VisitVarDecl中被访问到）
			for (CXXRecordDecl::field_iterator itr = r->field_begin(), end = r->field_end(); itr != end; ++itr)
			{
				FieldDecl *field = *itr;
				m_main->use_var(r->getLocStart(), field->getType());
			}

			// 成员函数不需要在这里遍历，因为VisitFunctionDecl将会访问成员函数
			return true;
		}

		bool ModifyVarDecl(VarDecl *var)
		{
			/*
				注意：本方法已经涵盖了类似下面class A模板类的成员变量color这种情况

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
						static const Color color = (Color)2;
					};

					template<typename T>
					const typename A<T>::Color A<T>::color;
			*/

			// 在前面加上注释
			std::stringstream before;
			before << "// ";

			// 类的成员变量（对于模板类同样可以处理得很好）
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
					m_main->use(var->getLocStart(), prevVar->getLocStart());
					before << "static ";
				}
			}

			before << "variable <" << var->getNameAsString() << "> type is " << var->getType().getAsString() << "\n";
			SourceLocation location = var->getSourceRange().getBegin();
			m_rewriter.InsertText(location, before.str(), true, true);

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

			// llvm::outs() << "\n" << "VisitVarDecl" << m_main->debug_loc_text(var->getTypeSourceInfo()->getTypeLoc().getLocStart()) << "\n";

			// 引用变量的类型
			m_main->use_var(var->getLocStart(), var->getType());

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
					m_main->use(var->getLocStart(), prevVar->getLocStart());
				}
			}

			return true;
		}

		bool VisitTypedefDecl(clang::TypedefDecl *d)
		{
			// llvm::errs() << "Visiting " << d->getDeclKindName() << " " << d->getName() << "\n";

			m_main->use_type(d->getLocStart(), d->getUnderlyingType());
			return true; // returning false aborts the traversal
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
			//			for (DeclGroupRef::iterator itr = declgroup.begin(), end = declgroup.end(); itr != end; ++itr) {
			//				Decl *decl = *itr;
			//
			//				// 使用ast访问器遍历声明
			//				m_visitor.TraverseDecl(decl);
			//			}

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
			if (WholeProject::instance.m_isFirst)
			{
				llvm::errs() << "analyze file: " << Filename << " ...\n";
			}
			else
			{
				llvm::errs() << "cleaning file: " << Filename << " ...\n";
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

			c->m_main->generate_result();

			{
				if (WholeProject::instance.m_isFirst || WholeProject::instance.m_onlyHas1File)
				{
					WholeProject::instance.AddFile(c->m_main);
					c->m_main->print();
				}

				bool can_clean	 = false;
				can_clean		|= WholeProject::instance.m_onlyHas1File;;
				can_clean		|= !WholeProject::instance.m_isOptimized;
				can_clean		|= !WholeProject::instance.m_isFirst;

				if (can_clean)
				{
					c->m_main->clean();
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

			ParsingFile *mainFile = new ParsingFile;
			mainFile->m_rewriter = &m_rewriter;
			mainFile->init_header_search_path(&compiler.getSourceManager(), compiler.getPreprocessor().getHeaderSearchInfo());

			compiler.getPreprocessor().addPPCallbacks(llvm::make_unique<CxxCleanPreprocessor>(mainFile));
			return llvm::make_unique<ListDeclASTConsumer>(m_rewriter, mainFile);
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

bool add_clean_vs_argument(const Vsproject &vs, ClangTool &tool)
{
	const VsConfiguration &vsconfig = vs.configs[0];

	for (int i = 0, size = vsconfig.search_dirs.size(); i < size; i++)
	{
		const std::string &dir	= vsconfig.search_dirs[i];
		std::string arg			= "-I" + dir;

		ArgumentsAdjuster argAdjuster = getInsertArgumentAdjuster(arg.c_str(), ArgumentInsertPosition::BEGIN);
		tool.appendArgumentsAdjuster(argAdjuster);
	}

	for (int i = 0, size = vsconfig.force_includes.size(); i < size; i++)
	{
		const std::string &force_include	= vsconfig.force_includes[i];
		std::string arg						= "-include" + force_include;

		ArgumentsAdjuster argAdjuster = getInsertArgumentAdjuster(arg.c_str(), ArgumentInsertPosition::BEGIN);
		tool.appendArgumentsAdjuster(argAdjuster);
	}

	for (auto predefine : vsconfig.pre_defines)
	{
		std::string arg = "-D" + predefine;

		ArgumentsAdjuster argAdjuster = getInsertArgumentAdjuster(arg.c_str(), ArgumentInsertPosition::BEGIN);
		tool.appendArgumentsAdjuster(argAdjuster);
	}

	tool.appendArgumentsAdjuster(getInsertArgumentAdjuster("-fms-extensions", ArgumentInsertPosition::BEGIN));
	tool.appendArgumentsAdjuster(getInsertArgumentAdjuster("-fms-compatibility", ArgumentInsertPosition::BEGIN));
	tool.appendArgumentsAdjuster(getInsertArgumentAdjuster("-fms-compatibility-version=18", ArgumentInsertPosition::BEGIN));

	filetool::cd(vs.project_dir.c_str());
	return true;
}

class CxxCleanOptionsParser
{
public:
	CxxCleanOptionsParser(int &argc, const char **argv,
	                      llvm::cl::OptionCategory &Category,
	                      const char *Overview = nullptr)
		: CxxCleanOptionsParser(argc, argv, Category, llvm::cl::OneOrMore,
		                        Overview) {}

	CxxCleanOptionsParser(int &argc, const char **argv,
	                      llvm::cl::OptionCategory &category,
	                      llvm::cl::NumOccurrencesFlag occurrencesFlag,
	                      const char *Overview = nullptr)
	{
		static cl::opt<bool>		Help("h",			cl::desc("Alias for -help"), cl::NotHidden);
		static cl::opt<std::string> g_source("src",	cl::desc("c++ source file"), cl::NotHidden);

		// static cl::list<std::string> SourcePaths(
		//    cl::NormalFormatting, cl::desc("<source0> [... <sourceN>]"), occurrencesFlag,
		//    cl::cat(category));

		// cl::HideUnrelatedOptions(Category);

		m_compilations.reset(FixedCompilationDatabase::loadFromCommandLine(argc, argv));
		cl::ParseCommandLineOptions(argc, argv, Overview);

		std::string src = g_source;
		if (!src.empty())
		{
			m_sourcePathList.push_back(src);
		}

		// m_sourcePathList = SourcePaths;

		if ((occurrencesFlag == cl::ZeroOrMore || occurrencesFlag == cl::Optional) && m_sourcePathList.empty())
		{
			return;
		}

		if (!m_compilations)
		{
			std::string ErrorMessage;
			if (m_sourcePathList.empty())
			{
				m_compilations = CompilationDatabase::autoDetectFromDirectory("./", ErrorMessage);
			}
			else
			{
				m_compilations = CompilationDatabase::autoDetectFromSource(m_sourcePathList[0], ErrorMessage);
			}
			if (!m_compilations)
			{
				llvm::errs() << "Error while trying to load a compilation database:\n"
				             << ErrorMessage << "Running without flags.\n";
				m_compilations.reset(
				    new FixedCompilationDatabase(".", std::vector<std::string>()));
			}
		}
	}

	/// Returns a reference to the loaded compilations database.
	CompilationDatabase &getCompilations() {return *m_compilations;}

	/// Returns a list of source file paths to process.
	std::vector<std::string>& getSourcePathList() {return m_sourcePathList;}

	static const char *const HelpMessage;

private:
	std::unique_ptr<CompilationDatabase> m_compilations;
	std::vector<std::string> m_sourcePathList;
};

const char *const CxxCleanOptionsParser::HelpMessage =
    "\n"
    "\n";

static cl::extrahelp CommonHelp(CxxCleanOptionsParser::HelpMessage);
static cl::extrahelp MoreHelp(
    "Example Usage:\n"
    "\n"
    "\tmsvc project, use:\n"
    "\n"
    "\t  ./cxxclean -clean hello.vcproj -src hello.cpp\n"
    "\n"
);

static cl::opt<bool>	g_helpOption	("h1",			cl::desc("Alias for -help"),				cl::NotHidden);
static cl::opt<string>	g_cleanOption	("clean",		cl::desc("clean <directory(eg. ./hello/)> or <vs 2005 project: ../hello.vcproj> or <vs 2008 and upper version: ../hello.vcxproj>\n"
        "or <vs project solution: ../hello.sln>\n"), cl::NotHidden);
static cl::opt<bool>	g_insteadOption	("i",			cl::desc("it means instead, output will overwrite the original c++ file"),	cl::NotHidden);
static cl::opt<bool>	g_printVsConfig	("print-vs",	cl::desc("print vs configuration"),	cl::NotHidden);
static cl::opt<bool>	g_isOptimized	("optimized",	cl::desc("try clean the whole project more deeply and need more time"),	cl::NotHidden);

static void PrintVersion()
{
	raw_ostream &OS = outs();
	OS << clang::getClangToolFullVersion("clang-format") << '\n';
}


#include "clang/Rewrite/Core/RewriteBuffer.h"

using namespace llvm;
using namespace clang;

namespace
{
	static void tagRange(unsigned Offset, unsigned Len, StringRef tagName,
	                     RewriteBuffer &Buf)
	{
		std::string BeginTag;
		raw_string_ostream(BeginTag) << '<' << tagName << '>';
		std::string EndTag;
		raw_string_ostream(EndTag) << "</" << tagName << '>';

		Buf.InsertTextAfter(Offset, BeginTag);
		Buf.InsertTextBefore(Offset+Len, EndTag);
	}

	void test()
	{
		StringRef Input = "hello world";
		const char *Output = "<outer><inner>hello</inner></outer> ";

		RewriteBuffer Buf;
		Buf.Initialize(Input);
		StringRef RemoveStr = "world";
		size_t Pos = Input.find(RemoveStr);
		Buf.RemoveText(Pos, RemoveStr.size());

		StringRef TagStr = "hello";
		Pos = Input.find(TagStr);
		tagRange(Pos, TagStr.size(), "outer", Buf);
		tagRange(Pos, TagStr.size(), "inner", Buf);

		std::string Result;
		raw_string_ostream OS(Result);
		Buf.write(OS);
		OS.flush();

		llvm::outs() << "Output = " << Output << "\n";
		llvm::outs() << "Result = " << Result << "\n";
	}

	void test2()
	{
		StringRef oldText =
		    "#include 'a.h'\r\n"
		    "#include 'b.h'\r\n"
		    "#include 'c.h'\r\n"
		    "#include 'd.h'\r\n"
		    "#include 'e.h'\r\n"
		    "#include 'f.h'\r\n"
		    "#include 'g.h'\r\n"
		    "#include 'h.h'\r\n"
		    "#include 'i.h'\r\n";

		std::string newText;

		{
			RewriteBuffer Buf;
			Buf.Initialize(oldText);

			{
				StringRef RemoveStr = "#include 'a.h'\r\n";
				size_t Pos = oldText.find(RemoveStr);
				Buf.RemoveText(Pos, RemoveStr.size());
			}

			{
				StringRef replaceStr = "#include 'b.h'\r\n";
				size_t Pos = oldText.find(replaceStr);
				Buf.ReplaceText(Pos, replaceStr.size(), "#include 'b_1.h'\r\n");
			}

			{
				StringRef RemoveStr = "#include 'c.h'\r\n";
				size_t Pos = oldText.find(RemoveStr);
				Buf.RemoveText(Pos, RemoveStr.size());
			}

			{
				StringRef insertStr = "#include 'c.h'\r\n";
				size_t Pos = oldText.find(insertStr);
				Buf.InsertText(Pos, "class C;\n");
			}

			{
				StringRef replaceStr = "#include 'd.h'\r\n";
				size_t Pos = oldText.find(replaceStr);
				Buf.ReplaceText(Pos, replaceStr.size(), "#include 'd_1.h'\r\n");
			}

			raw_string_ostream OS(newText);
			Buf.write(OS);
			OS.flush();
		}

		llvm::outs() << "old =\n" << oldText << "\n";
		llvm::outs() << "new =\n" << newText << "\n";
	}
} // anonymous namespace

int main(int argc, const char **argv)
{
	if (argc == 0)
	{
		test2();
		return 0;
	}

	llvm::InitializeNativeTarget();
	llvm::InitializeNativeTargetAsmParser();

	CxxCleanOptionsParser optionParser(argc, argv, cxxcleantool::g_optionCategory);
	if (g_helpOption)
	{
		cl::PrintHelpMessage();
	}

	std::string clean_option = g_cleanOption;

	std::vector<std::string> &source_list = optionParser.getSourcePathList();

	Vsproject &vs = Vsproject::instance;

	if (!clean_option.empty())
	{
		const string ext = strtool::get_ext(clean_option);
		if (ext == "vcproj" || ext == "vcxproj")
		{
			if (!parse_vs(clean_option, vs))
			{
				llvm::errs() << "parse vs project<" << clean_option << "> failed!\n";
				return 0;
			}

			llvm::outs() << "parse vs project<" << clean_option << "> succeed!\n";

			if (source_list.empty())
			{
				add_source_file(vs, source_list);
			}
		}
		else if (llvm::sys::fs::is_directory(clean_option))
		{
			llvm::outs() << "unsupport parsed " << clean_option << " directory\n";
		}
		else
		{
			llvm::errs() << "unsupport parsed <" << clean_option << ">!\n";
		}
	}

	if (g_printVsConfig)
	{
		vs.dump();
		return 0;
	}

	if (source_list.size() == 1)
	{
		WholeProject::instance.m_onlyHas1File = true;
	}

	ClangTool tool(optionParser.getCompilations(), source_list);
	tool.clearArgumentsAdjusters();
	tool.appendArgumentsAdjuster(getClangSyntaxOnlyAdjuster());

	if (!clean_option.empty())
	{
		add_clean_vs_argument(vs, tool);
		WholeProject::instance.m_isOptimized = g_isOptimized;
	}

	cxxcleantool::ParsingFile::m_isOverWriteOption = g_insteadOption;

	tool.appendArgumentsAdjuster(getInsertArgumentAdjuster("-fcxx-exceptions",			ArgumentInsertPosition::BEGIN));
	tool.appendArgumentsAdjuster(getInsertArgumentAdjuster("-Winvalid-source-encoding", ArgumentInsertPosition::BEGIN));
	tool.appendArgumentsAdjuster(getInsertArgumentAdjuster("-Wdeprecated-declarations", ArgumentInsertPosition::BEGIN));

	std::unique_ptr<FrontendActionFactory> factory = newFrontendActionFactory<cxxcleantool::ListDeclAction>();
	tool.run(factory.get());

	if (WholeProject::instance.m_isOptimized && !WholeProject::instance.m_onlyHas1File)
	{
		WholeProject::instance.m_isFirst = false;

		llvm::outs() << "\n\n////////////////////////////////////////////////////////////////\n";
		llvm::outs() << "[ 2. second times parse" << "]";
		llvm::outs() << "\n////////////////////////////////////////////////////////////////\n";

		tool.run(factory.get());
	}

	WholeProject::instance.print();
	return 0;
}