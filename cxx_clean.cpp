///<------------------------------------------------------------------------------
//< @file  : cxx_clean.cpp
//< @author: ������
//< @date  : 2016��1��2��
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
#include "vs_project.h"
#include "parsing_cpp.h"
#include "project.h"

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

	// Ԥ����������#define��#if��#else��Ԥ�����ؼ��ֱ�Ԥ����ʱʹ�ñ�Ԥ������
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
				// ע�⣺PPCallbacks::EnterFileʱ��prevFileID��Ч���޷���Ч�������Բ��迼��
				return;
			}

			/*
				������a.cpp��#include "b.h"����˴���prevFileID����b.h
				loc���ʾa.cpp��#include "b.h"���һ��˫���ŵĺ���һ��λ�ã�����
				#include "b.h"
							  ^
							  loc��λ��
			*/

			FileID curFileID = srcMgr.getFileID(loc);
			if (!prevFileID.isValid() || !curFileID.isValid())
			{
				return;
			}

			if (prevFileID.isValid())
			{
				m_main->m_files[m_main->GetAbsoluteFileName(prevFileID)] = prevFileID;
			}

			if (curFileID.isValid())
			{
				m_main->m_files[m_main->GetAbsoluteFileName(curFileID)] = curFileID;
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

		// ����#include
		void InclusionDirective(SourceLocation HashLoc /*#��λ��*/, const Token &includeTok,
		                        StringRef fileName, bool isAngled/*�Ƿ�<>�����������Ǳ�""��Χ*/, CharSourceRange filenameRange,
		                        const FileEntry *file, StringRef searchPath, StringRef relativePath, const Module *Imported) override
		{
			// ������ʱ����-include<c++ͷ�ļ�>ѡ���ȴ���Ҳ�����ͷ�ļ�ʱ��������file��Ч
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

		// ����#define
		void MacroDefined(const Token &macroName, const MacroDirective *direct) override
		{
		}

		// �걻#undef
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

	// ͨ��ʵ��RecursiveASTVisitor���Զ�����ʵ�c++�����﷨��ʱ�Ĳ���
	class DeclASTVisitor : public RecursiveASTVisitor<DeclASTVisitor>
	{
	public:
		DeclASTVisitor(Rewriter &rewriter, ParsingFile *mainFile)
			: m_rewriter(rewriter), m_main(mainFile)
		{
		}

		void ModifyFunc(FunctionDecl *f)
		{
			// ������
			DeclarationName declName = f->getNameInfo().getName();
			std::string funcName = declName.getAsString();

			if (f->isTemplateDecl() || f->isLateTemplateParsed())
			{
				// ��ǰ�����ע��
				std::stringstream before;
				before << "// function <" << funcName << "> is template\n";
				SourceLocation location = f->getSourceRange().getBegin();
				m_rewriter.InsertText(location, before.str(), true, true);
			}

			if (f->isReferenced() || f->isThisDeclarationReferenced() || f->isUsed())
			{
				// ��ǰ�����ע��
				std::stringstream before;
				before << "// function <" << funcName << "> is referenced\n";
				SourceLocation location = f->getSourceRange().getBegin();
				m_rewriter.InsertText(location, before.str(), true, true);
			}

			// ����ʵ��
			if (f->hasBody())
			{
				Stmt *funcBody = f->getBody();
				if (NULL == funcBody)
				{
					return;
				}

				// �����ķ���ֵ
				QualType returnType = f->getReturnType();

				// ��ǰ�����ע��
				std::stringstream before;
				before << "// Begin function " << funcName << " returning " << returnType.getAsString() << "\n";
				SourceLocation location = f->getSourceRange().getBegin();
				m_rewriter.InsertText(location, before.str(), true, true);

				// �ں������ע��
				std::stringstream after;
				after << "\n// End function " << funcName;
				location = funcBody->getLocEnd().getLocWithOffset(1);
				m_rewriter.InsertText(location, after.str(), true, true);
			}
			// ��������������ʵ��
			else
			{
				// ��ǰ�����ע��
				std::stringstream before;
				before << "// begin function <" << funcName << "> only declaration\n";
				SourceLocation location = f->getSourceRange().getBegin();
				m_rewriter.InsertText(location, before.str(), true, true);

				// �ں������ע��
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

		// ���ʵ������
		bool VisitStmt(Stmt *s)
		{
			SourceLocation loc = s->getLocStart();

			// �μ���http://clang.llvm.org/doxygen/classStmt.html
			if (isa<CastExpr>(s))
			{
				CastExpr *castExpr = cast<CastExpr>(s);
				QualType castType = castExpr->getType();

				m_main->UseType(loc, castType);

				// ����ע�⣬CastExpr��һ��getSubExpr()��������ʾ�ӱ���ʽ�����˴�����Ҫ������������Ϊ�ӱ���ʽ����ΪStmt��Ȼ�ᱻVisitStmt���ʵ�
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
			// return��䣬���磺return 4;��return;
			else if (isa<ReturnStmt>(s))
			{
				// ����ReturnStmt����Ҫ������������Ϊreturn��������Ȼ�ᱻVisitStmt���ʵ�
			}
			// ���ű���ʽ�����磺(1)��(a + b)
			else if (isa<ParenExpr>(s))
			{
				// ����ParenExpr����Ҫ������������Ϊ�����ڵ������Ȼ�ᱻVisitStmt���ʵ�
			}
			// ���ϱ���ʽ���ɲ�ͬ����ʽ��϶���
			else if(isa<CompoundStmt>(s))
			{
				// ����CompoundStmt����Ҫ������������Ϊ���������Ȼ�ᱻVisitStmt���ʵ�
			}
			// ��Ԫ����ʽ�����磺"x + y" or "x <= y"����������δ�����أ���������BinaryOperator���������������أ������ͽ���CXXOperatorCallExpr
			else if (isa<BinaryOperator>(s))
			{
				// ����BinaryOperator����Ҫ������������Ϊ��2���Ӳ���������Ϊ��䱻VisitStmt���ʵ�
				// ����CXXOperatorCallExprҲ����Ҫ������������Ϊ�佫��ΪCallExpr��VisitStmt���ʵ�
			}
			// һԪ����ʽ�����磺����+������-������++���Լ�--,�߼��ǣ�����λȡ��~��ȡ������ַ&��ȡָ����ֵָ*��
			else if (isa<UnaryOperator>(s))
			{
				// ����UnaryOperator����Ҫ������������Ϊ��1���Ӳ���������Ϊ��䱻VisitStmt���ʵ�
			}
			else if (isa<IntegerLiteral>(s))
			{
			}
			else if (isa<UnaryExprOrTypeTraitExpr>(s))
			{
			}
			/// �ṹ���union�ĳ�Ա�����磺X->F��X.F.
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

			// ʶ�𷵻�ֵ����
			{
				// �����ķ���ֵ
				QualType returnType = f->getReturnType();
				m_main->UseVar(f->getLocStart(), returnType);
			}

			// ʶ��������
			{
				// ���α����������������ù�ϵ
				for (FunctionDecl::param_iterator itr = f->param_begin(), end = f->param_end(); itr != end; ++itr)
				{
					ParmVarDecl *vardecl = *itr;
					QualType vartype = vardecl->getType();
					m_main->UseVar(f->getLocStart(), vartype);
				}
			}

			/*
				�����ҵ�������ԭ������
				�磺
					1. class Hello
					2.	{
					3.		void print();
					4.	}
					5. void Hello::print() {}

				���5���Ǻ���ʵ�֣�������λ�ڵ�3��
			*/
			{
				/*
					ע�⣺���ຯ�����Է���������������
						int hello();
						int hello();
						int hello();

					�������ע����ĺ�������
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

					// ���ú���ԭ��
					// m_main->on_use_decl(f->getLocStart(), oldestFunc);
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
					m_main->OnUseRecord(f->getLocStart(),	record);
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

			// �������л���
			for (CXXRecordDecl::base_class_iterator itr = r->bases_begin(), end = r->bases_end(); itr != end; ++itr)
			{
				CXXBaseSpecifier &base = *itr;
			}

			// ������Ա������ע�⣺������static��Ա������static��Ա��������VisitVarDecl�б����ʵ���
			for (CXXRecordDecl::field_iterator itr = r->field_begin(), end = r->field_end(); itr != end; ++itr)
			{
				FieldDecl *field = *itr;

				// ��ǰ�����ע��
				std::stringstream before;
				before << "// member <" << field->getNameAsString() << "> type is " << field->getType().getAsString() << "\n";
				SourceLocation location = field->getSourceRange().getBegin();
				m_rewriter.InsertText(location, before.str(), true, true);
			}

			// ��Ա��������Ҫ�������������ΪVisitFunctionDecl������ʳ�Ա����
			return true;
		}

		// ����class��struct��union��enumʱ
		bool VisitCXXRecordDecl(CXXRecordDecl *r)
		{
			if (!r->hasDefinition())
			{
				return true;
			}

			// �������л���
			for (CXXRecordDecl::base_class_iterator itr = r->bases_begin(), end = r->bases_end(); itr != end; ++itr)
			{
				CXXBaseSpecifier &base = *itr;
				m_main->UseType(r->getLocStart(), base.getType());
			}

			// ������Ա������ע�⣺������static��Ա������static��Ա��������VisitVarDecl�б����ʵ���
			for (CXXRecordDecl::field_iterator itr = r->field_begin(), end = r->field_end(); itr != end; ++itr)
			{
				FieldDecl *field = *itr;
				m_main->UseVar(r->getLocStart(), field->getType());
			}

			// ��Ա��������Ҫ�������������ΪVisitFunctionDecl������ʳ�Ա����
			return true;
		}

		bool ModifyVarDecl(VarDecl *var)
		{
			/*
				ע�⣺�������Ѿ���������������class Aģ����ĳ�Ա����color�������

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

			// ��ǰ�����ע��
			std::stringstream before;
			before << "// ";

			// ��ĳ�Ա����������ģ����ͬ�����Դ����úܺã�
			if (var->isCXXClassMember())
			{
				/*
					��Ϊstatic��Ա������ʵ�֣���������������ע��Ҳ������isStaticDataMember�������ж�var�Ƿ�Ϊstatic��Ա������
					���磺
							1. class Hello
							2.	{
							3.		static int g_num;
							4.	}
							5. static int Hello::g_num;

					���5�е�����λ�ڵ�3�д�
				*/
				const VarDecl *prevVar = var->getPreviousDecl();
				if (prevVar)
				{
					m_main->Use(var->getLocStart(), prevVar->getLocStart());
					before << "static ";
				}
			}

			before << "variable <" << var->getNameAsString() << "> type is " << var->getType().getAsString() << "\n";
			SourceLocation location = var->getSourceRange().getBegin();
			m_rewriter.InsertText(location, before.str(), true, true);

			return true;
		}

		// �����ֱ�������ʱ�ýӿڱ�����
		bool VisitVarDecl(VarDecl *var)
		{
			/*
				ע�⣺
					������������
						1. �����β�
						2. �����Ա�ı�������
						3. ���Ա������static���η�

					��:int a�� A a�ȣ����Ѿ���������������class Aģ����ĳ�Ա����color�������

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

			// ���ñ���������
			m_main->UseVar(var->getLocStart(), var->getType());

			// ���static��Ա������֧��ģ����ĳ�Ա��
			if (var->isCXXClassMember())
			{
				/*
					��Ϊstatic��Ա������ʵ�֣���������������ע��Ҳ������isStaticDataMember�������ж�var�Ƿ�Ϊstatic��Ա������
					���磺
							1. class Hello
							2.	{
							3.		static int g_num;
							4.	}
							5. static int Hello::g_num;

					���5�е�����λ�ڵ�3�д�
				*/
				const VarDecl *prevVar = var->getPreviousDecl();
				if (prevVar)
				{
					m_main->OnUseDecl(var->getLocStart(), prevVar);
				}
			}

			return true;
		}

		bool VisitTypedefDecl(clang::TypedefDecl *d)
		{
			// llvm::errs() << "Visiting " << d->getDeclKindName() << " " << d->getName() << "\n";

			m_main->UseType(d->getLocStart(), d->getUnderlyingType());
			return true; // returning false aborts the traversal
		}

		// ���ʳ�Ա����
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

	// ����������ʵ��ASTConsumer�ӿ����ڶ�ȡclang���������ɵ�ast�﷨��
	class ListDeclASTConsumer : public ASTConsumer
	{
	public:
		ListDeclASTConsumer(Rewriter &r, ParsingFile *mainFile) : m_main(mainFile), m_visitor(r, mainFile) {}

		// ���ǣ�������������ķ���
		bool HandleTopLevelDecl(DeclGroupRef declgroup) override
		{
			//			for (DeclGroupRef::iterator itr = declgroup.begin(), end = declgroup.end(); itr != end; ++itr) {
			//				Decl *decl = *itr;
			//
			//				// ʹ��ast��������������
			//				m_visitor.TraverseDecl(decl);
			//			}

			return true;
		}

		// �����������ÿ��Դ�ļ���������һ�Σ����磬����һ��hello.cpp��#include������ͷ�ļ���Ҳֻ�����һ�α�����
		void HandleTranslationUnit(ASTContext& context) override
		{
			m_visitor.TraverseDecl(context.getTranslationUnitDecl());
		}

	public:
		ParsingFile*	m_main;
		DeclASTVisitor	m_visitor;
	};

	// ����tool���յ���ÿ��Դ�ļ�������newһ��ListDeclAction
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

			ParsingFile *mainFile	= new ParsingFile;
			mainFile->m_rewriter	= &m_rewriter;
			//mainFile->m_compiler	= &compiler;

			mainFile->InitHeaderSearchPath(&compiler.getSourceManager(), compiler.getPreprocessor().getHeaderSearchInfo());

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

	filetool::cd(vs.m_project_dir.c_str());
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

	static FixedCompilationDatabase *CxxCleanOptionsParser::loadFromCommandLine
	(int &Argc, const char *const *Argv, Twine Directory = ".")
	{
		const char *const *DoubleDash = std::find(Argv, Argv + Argc, StringRef("--"));
		if (DoubleDash == Argv + Argc)
			return nullptr;
		std::vector<const char *> CommandLine(DoubleDash + 1, Argv + Argc);
		Argc = DoubleDash - Argv;

		std::vector<std::string> StrippedArgs;
		StrippedArgs.reserve(CommandLine.size());

		for (const char * arg : CommandLine)
		{
			StrippedArgs.push_back(arg);
		}

		return new FixedCompilationDatabase(Directory, StrippedArgs);
	}


	CxxCleanOptionsParser(int &argc, const char **argv,
	                      llvm::cl::OptionCategory &category,
	                      llvm::cl::NumOccurrencesFlag occurrencesFlag,
	                      const char *Overview = nullptr)
	{
		static cl::opt<bool>		Help("h",			cl::desc("Alias for -help"), cl::NotHidden);
		static cl::opt<std::string> g_source("src",		cl::desc("c++ source file"), cl::NotHidden);

		// static cl::list<std::string> SourcePaths(
		//    cl::NormalFormatting, cl::desc("<source0> [... <sourceN>]"), occurrencesFlag,
		//    cl::cat(category));

		// cl::HideUnrelatedOptions(Category);

		m_compilations.reset(CxxCleanOptionsParser::loadFromCommandLine(argc, argv));
		cl::ParseCommandLineOptions(argc, argv, Overview);

		std::string src = g_source;
		if (!src.empty())
		{
			if (filetool::exist(src))
			{
				m_sourcePathList.push_back(src);
			}
			else
			{
				bool ok = filetool::ls(src, m_sourcePathList);
				if (!ok)
				{
					return;
				}
			}
		}

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

static cl::opt<bool>	g_helpOption	("h1",				cl::desc("Alias for -help"),				cl::NotHidden);
static cl::opt<string>	g_cleanOption	("clean",			cl::desc("clean <directory(eg. ./hello/)> or <vs 2005 project: ../hello.vcproj> or <visual studio 2005 and upper version: ../hello.vcxproj>\n"), cl::NotHidden);
static cl::opt<bool>	g_noWriteOption	("no",				cl::desc("means no overwrite, will not overwrite the original c++ file"),	cl::NotHidden);
static cl::opt<bool>	g_onlyCleanCpp	("onlycpp",			cl::desc("only allow clean cpp"),	cl::NotHidden);
static cl::opt<bool>	g_printVsConfig	("print-vs",		cl::desc("print vs configuration"),	cl::NotHidden);
static cl::opt<bool>	g_printProject	("print-project",	cl::desc("print vs configuration"),	cl::NotHidden);

static void PrintVersion()
{
	raw_ostream &OS = outs();
	OS << clang::getClangToolFullVersion("clang-format") << '\n';
}

int main(int argc, const char **argv)
{
	llvm::InitializeNativeTarget();
	llvm::InitializeNativeTargetAsmParser();

	CxxCleanOptionsParser optionParser(argc, argv, cxxcleantool::g_optionCategory);
	if (g_helpOption)
	{
		cl::PrintHelpMessage();
	}

	Vsproject &vs		= Vsproject::instance;
	Project &project	= Project::instance;

	project.m_workingDir = filetool::get_current_path();
	Project::instance.m_isDeepClean = !g_onlyCleanCpp;

	std::string clean_option = g_cleanOption;

	project.m_cpps = optionParser.getSourcePathList();

	if (!clean_option.empty())
	{
		const string ext = strtool::get_ext(clean_option);
		if (ext == "vcproj" || ext == "vcxproj")
		{
			if (!ParseVs(clean_option, vs))
			{
				llvm::errs() << "parse vs project<" << clean_option << "> failed!\n";
				return 0;
			}

			llvm::outs() << "parse vs project<" << clean_option << "> succeed!\n";

			vs.TakeSourceListTo(project);
		}
		else if (llvm::sys::fs::is_directory(clean_option))
		{
			std::string folder = ParsingFile::GetAbsoluteFileName(clean_option.c_str());

			if (!strtool::end_with(folder, "/"))
			{
				folder += "/";
			}

			Project::instance.m_allowCleanDir = folder;

			if (project.m_cpps.empty())
			{
				bool ok = filetool::ls(folder, project.m_cpps);
				if (!ok)
				{
					llvm::errs() << "error: -clean " << folder << " failed!\n";
					return 0;
				}
			}
		}
		else
		{
			llvm::errs() << "unsupport parsed <" << clean_option << ">!\n";
			return 0;
		}
	}
	else
	{
		project.GenerateAllowCleanList();
	}

	project.Fix();

	if (project.m_cpps.size() == 1)
	{
		Project::instance.m_onlyHas1File = true;
	}


	if (g_printVsConfig)
	{
		vs.Print();
		return 0;
	}

	if (g_printProject)
	{
		project.Print();
		return 0;
	}

	if (project.m_cpps.empty())
	{
		llvm::errs() << "error: parse argument failed, please check there is c++ file to parse!\n";
		project.Print();
		return 0;
	}

	ClangTool tool(optionParser.getCompilations(), project.m_cpps);
	tool.clearArgumentsAdjusters();
	tool.appendArgumentsAdjuster(getClangSyntaxOnlyAdjuster());

	if (!clean_option.empty())
	{
		AddCleanVsArgument(vs, tool);
	}

	//std::vector<CompileCommand> allCompileArgs = getAllCompileCommands();


	cxxcleantool::ParsingFile::m_isOverWriteOption = !g_noWriteOption;

	tool.appendArgumentsAdjuster(getInsertArgumentAdjuster("-fcxx-exceptions",			ArgumentInsertPosition::BEGIN));
	tool.appendArgumentsAdjuster(getInsertArgumentAdjuster("-Winvalid-source-encoding", ArgumentInsertPosition::BEGIN));
	tool.appendArgumentsAdjuster(getInsertArgumentAdjuster("-Wdeprecated-declarations", ArgumentInsertPosition::BEGIN));

	std::unique_ptr<FrontendActionFactory> factory = newFrontendActionFactory<cxxcleantool::ListDeclAction>();
	tool.run(factory.get());

	if (Project::instance.m_isDeepClean && !Project::instance.m_onlyHas1File)
	{
		ProjectHistory::instance.m_isFirst = false;

		llvm::outs() << "\n\n////////////////////////////////////////////////////////////////\n";
		llvm::outs() << "[ 2. second times parse" << "]";
		llvm::outs() << "\n////////////////////////////////////////////////////////////////\n";

		tool.run(factory.get());
	}

	ProjectHistory::instance.Print();
	return 0;
}