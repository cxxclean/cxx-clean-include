///<------------------------------------------------------------------------------
//< @file:   parsing_cpp.h
//< @author: 洪坤安
//< @date:   2016年2月22日
//< @brief:
//< Copyright (c) 2016. All rights reserved.
///<------------------------------------------------------------------------------

#ifndef _parsing_cpp_h_
#define _parsing_cpp_h_

#include <string>
#include <vector>
#include <set>
#include <map>

#include <clang/Basic/SourceLocation.h>
#include <clang/Basic/SourceManager.h>

#include "project_history.h"

using namespace std;
using namespace clang;

namespace clang
{
	class HeaderSearch;
	class MacroDefinition;
	class Token;
	class QualType;
	class CXXRecordDecl;
	class NamedDecl;
	class Rewriter;
	class CompilerInstance;
	class NestedNameSpecifier;
	class Type;
	class NamespaceDecl;
	class UsingDirectiveDecl;
	class FunctionDecl;
	class MacroArgs;
	class VarDecl;
	class ValueDecl;
	class RecordDecl;
	class UsingDecl;
}

namespace cxxcleantool
{
	// 当前正在解析的c++文件信息
	class ParsingFile
	{
		// [文件名] -> [路径类别：系统路径或用户路径]
		typedef std::map<string, SrcMgr::CharacteristicKind> IncludeDirMap;

		struct UseNameInfo
		{
			void AddName(const char* name, int line)
			{
				if (nameMap.find(name) == nameMap.end())
				{
					nameVec.push_back(name);
				}

				nameMap[name].insert(line);
			}

			FileID									file;
			std::vector<string>						nameVec;
			std::map<string, std::set<int>>			nameMap;
		};

		struct NamespaceInfo
		{
			std::string ns_decl;		// 命名空间的声明，如：namespace A{ namespace B { namespace C {} } }
			std::string ns_name;		// 命名空间的名称，如：A::B::C
		};

	public:
		struct HeaderSearchDir
		{
			HeaderSearchDir(const string& dir, SrcMgr::CharacteristicKind dirType)
				: m_dir(dir)
				, m_dirType(dirType)
			{
			}

			string						m_dir;
			SrcMgr::CharacteristicKind	m_dirType;
		};

	public:
		ParsingFile(clang::Rewriter &rewriter, clang::CompilerInstance &compiler);

		// 打印单个文件的清理历史
		static void PrintFileHistory(const FileHistory&);

		// 打印单个文件内的可被删#include记录
		static void PrintUnusedIncludeOfFile(const FileHistory&);

		// 打印单个文件内的可新增前置声明记录
		static void PrintCanForwarddeclOfFile(const FileHistory&);

		// 打印单个文件内的可被替换#include记录
		static void PrintCanReplaceOfFile(const FileHistory&);

		// 打印该文件产生的编译错误
		static void PrintCompileError(const CompileErrorHistory&);

		// 初始化本对象
		bool Init();

		// 添加父文件关系
		void AddParent(FileID child, FileID parent) { m_parentIDs[child] = parent; }

		// 添加包含文件记录
		void AddInclude(FileID file, FileID beInclude) { m_includes[file].insert(beInclude); }

		// 添加#include的位置记录
		void AddIncludeLoc(SourceLocation loc, SourceRange range);

		// 添加成员文件
		void AddFile(FileID file);

		inline clang::SourceManager& GetSrcMgr() const { return *m_srcMgr; }

		// 生成结果前的准备
		void PrepareResult();

		// 生成各文件的待清理记录
		void GenerateResult();

		// 是否为可前置声明的类型
		bool IsForwardType(const QualType &var);

		// 当前文件使用目标文件
		void UseInclude(FileID file, FileID beusedFile, const char* name = nullptr, int line = 0);

		// 文件a使用指定名称的目标文件
		void UseByFileName(FileID a, const char* filename);

		// 当前位置使用指定的宏
		void UseMacro(SourceLocation loc, const MacroDefinition &macro, const Token &macroName, const MacroArgs *args = nullptr);

		// 新增使用变量记录
		void UseVarType(SourceLocation loc, const QualType &var);

		// 引用变量声明
		void UseVarDecl(SourceLocation loc, const VarDecl *var);

		// 引用变量声明（为左值）、函数表示、enum常量
		void UseValueDecl(SourceLocation loc, const ValueDecl *valueDecl);

		// 引用带有名称的声明
		void UseNameDecl(SourceLocation loc, const NamedDecl *nameDecl);

		// 新增使用函数声明记录
		void UseFuncDecl(SourceLocation loc, const FunctionDecl *funcDecl);

		// 新增使用class、struct、union记录
		void UseRecord(SourceLocation loc, const RecordDecl *record);

		// cur位置的代码使用src位置的代码
		void Use(SourceLocation cur, SourceLocation src, const char* name = nullptr);

		// 当前位置使用目标类型（注：QualType包含对某个类型的const、volatile、static等的修饰）
		void UseQualType(SourceLocation loc, const QualType &t);

		// 当前位置使用目标类型（注：Type代表某个类型，但不含const、volatile、static等的修饰）
		void UseType(SourceLocation loc, const Type *t);

		// 当前位置使用定位
		void UseQualifier(SourceLocation loc, const NestedNameSpecifier*);

		// 声明了命名空间
		void DeclareNamespace(const NamespaceDecl *d);

		// using了命名空间，比如：using namespace std;
		void UsingNamespace(const UsingDirectiveDecl *d);

		// using了命名空间下的某类，比如：using std::string;
		void UsingXXX(const UsingDecl *d);

		// 获取可能缺失的using namespace
		bool GetMissingNamespace(SourceLocation loc, std::map<std::string, std::string> &miss) const;

		// 获取可能缺失的using namespace
		bool GetMissingNamespace(SourceLocation topLoc, SourceLocation oldLoc,
		                         std::map<std::string, std::string> &frontMiss, std::map<std::string, std::string> &backMiss) const;

		// 获取命名空间的全部路径，例如，返回namespace A{ namespace B{ class C; }}
		std::string GetNestedNamespace(const NamespaceDecl *d);

		// 文件b是否直接#include文件a
		bool IsIncludedBy(FileID a, FileID b);

		// 获取文件a所直接包含的第一个指定文件名的文件实例id
		FileID GetIncludeFileOfName(FileID a, const char* filename);

		// 开始清理文件（将改动c++源文件）
		void Clean();

		// 打印索引 + 1
		std::string AddPrintIdx() const;

		// 打印信息
		void Print();

		// 将当前cpp文件产生的待清理记录与之前其他cpp文件产生的待清理记录合并
		void MergeTo(FileHistoryMap &old) const;

		// 获取本文件的编译错误历史
		CompileErrorHistory& GetCompileErrorHistory() { return m_compileErrorHistory; }

		// 获取指定范围的文本
		std::string GetSourceOfRange(SourceRange range) const;

		// 获取该范围源码的信息：文本、所在文件名、行号
		std::string DebugRangeText(SourceRange range) const;

		// 该文件是否是被-include强制包含
		bool IsForceIncluded(FileID file) const;

		// 获取文件名（通过clang库接口，文件名未经过处理，可能是绝对路径，也可能是相对路径）
		// 例如：
		//     可能返回：d:/hello.h
		//     也可能返回：./hello.h
		const char* GetFileName(FileID file) const;

		// 获取文件的绝对路径
		string GetAbsoluteFileName(FileID file) const;

	private:
		// 获取头文件搜索路径
		std::vector<HeaderSearchDir> TakeHeaderSearchPaths(clang::HeaderSearch &headerSearch) const;

		// 将头文件搜索路径根据长度由长到短排列
		std::vector<HeaderSearchDir> SortHeaderSearchPath(const IncludeDirMap& include_dirs_map) const;

		// 指定位置的对应的#include是否被用到
		inline bool IsLocBeUsed(SourceLocation loc) const;

		/*
			生成主文件直接依赖和间接依赖的文件集

			例如：
				假设当前正在解析hello.cpp，hello.cpp对其他文件的包含关系如下（令-->表示包含）：
				hello.cpp --> a.h  --> b.h
				          |
						  --> c.h  --> d.h --> e.h

				如果hello.cpp依赖b.h，而b.h又依赖e.h，则在本例中，最终生成的依赖文件列表为：
					hello.cpp、b.h、e.h

		*/
		void GenerateRootCycleUse();

		// 根据主文件的依赖关系，生成相关文件的依赖文件集
		void GenerateAllCycleUse();

		// 尝试添加各个被依赖文件的祖先文件，返回值：true表示依赖文件集被扩张、false表示依赖文件夹不变
		bool TryAddAncestor();

		// 记录各文件的被依赖后代文件
		void GenerateCycleUsedChildren();

		// 新增依赖文件，返回结果表示是否新增了一些待处理的文件，是true、否false
		bool ExpandAllCycleUse(FileID newCycleUse);

		// 获取该文件可被替换到的文件，若无法被替换，则返回空文件id
		FileID GetCanReplaceTo(FileID top) const;

		// 生成无用#include的记录
		void GenerateUnusedInclude();

		// 从指定的文件列表中找到属于传入文件的后代
		std::set<FileID> GetChildren(FileID ancestor, std::set<FileID> all_children/* 包括非ancestor孩子的文件 */);

		// 获取指定文件直接依赖和间接依赖的文件集
		// 计算过程是：
		//     获取top所依赖的文件，并循环获取这些依赖文件所依赖的其他文件，直到所有的被依赖文件均已被处理
		void GetCycleUseFile(FileID top, std::set<FileID> &out) const;

		// 获取文件的深度（令主文件的高度为0）
		int GetDepth(FileID child) const;

		// 获取离孩子们最近的共同祖先
		FileID GetCommonAncestor(const std::set<FileID> &children) const;

		// 获取2个孩子们最近的共同祖先
		FileID GetCommonAncestor(FileID child_1, FileID child_2) const;

		// 当前文件之前是否已有文件声明了该class、struct、union
		bool HasRecordBefore(FileID cur, const CXXRecordDecl &cxxRecord) const;

		// 生成文件替换列表
		void GenerateCanReplace();

		// 生成应保留的using namespace
		void GenerateRemainUsingNamespace();

		// 生成新增前置声明列表
		void GenerateCanForwardDeclaration();

		// 获取指定位置所在行的文本
		std::string GetSourceOfLine(SourceLocation loc) const;

		/*
			根据传入的代码位置返回该行的正文范围（不含换行符）：[该行开头，该行末（不含换行符） + 1]
			例如：
				windows格式：
					int			a		=	100;\r\nint b = 0;
					^			^				^
					行首			传入的位置		行末

				linux格式：
					int			a		=	100;\n
					^			^				^
					行首			传入的位置		行末
		*/
		SourceRange GetCurLine(SourceLocation loc) const;

		/*
			根据传入的代码位置返回该行的范围（包括换行符）：[该行开头，下一行开头]
			例如：
				windows格式：
					int			a		=	100;\r\nint b = 0;
					^			^				    ^
					行首			传入的位置		    行末

				linux格式：
					int			a		=	100;\n
					^			^				  ^
					行首			传入的位置		  行末
		*/
		SourceRange GetCurLineWithLinefeed(SourceLocation loc) const;

		// 根据传入的代码位置返回下一行的范围
		SourceRange GetNextLine(SourceLocation loc) const;

		// 是否为空行
		bool IsEmptyLine(SourceRange line);

		// 获取行号
		int GetLineNo(SourceLocation loc) const;

		// 是否是换行符
		bool IsNewLineWord(SourceLocation loc) const;

		/*
			获取对应于传入文件ID的#include文本
			例如：
				假设有b.cpp，内容如下
					1. #include "./a.h"
					2. #include "a.h"

				其中第1行和第2行包含的a.h是虽然同一个文件，但FileID是不一样的

				现传入第一行a.h的FileID，则结果将返回
					#include "./a.h"
		*/
		std::string GetRawIncludeStr(FileID file) const;

		void UseName(FileID file, FileID beusedFile, const char* name = nullptr, int line = 0);

		// 根据当前文件，查找第2层的祖先（令root为第一层），若当前文件的父文件即为主文件，则返回当前文件
		FileID GetLvl2Ancestor(FileID file, FileID root) const;

		// 第2个文件是否是第1个文件的祖先（主文件是所有其他文件的祖先）
		bool IsAncestor(FileID yound, FileID old) const;

		// 第2个文件是否是第1个文件的祖先
		bool IsAncestor(FileID yound, const char* old) const;

		// 第2个文件是否是第1个文件的祖先
		bool IsAncestor(const char* yound, FileID old) const;

		// 是否为孩子文件的共同祖先
		bool IsCommonAncestor(const std::set<FileID> &children, FileID old) const;

		// 获取父文件（主文件没有父文件）
		FileID GetParent(FileID child) const;

		// 获取c++中class、struct、union的全名，结果将包含命名空间
		// 例如：
		//     传入类C，C属于命名空间A中的命名空间B，则结果将返回：namespace A{ namespace B{ class C; }}
		string GetRecordName(const RecordDecl &recordDecl) const;

		// 从指定位置起，跳过之后的注释，直到获得下一个token
		bool LexSkipComment(SourceLocation Loc, Token &Result);

		// 返回插入前置声明所在行的开头
		SourceLocation GetInsertForwardLine(FileID at, const CXXRecordDecl &cxxRecord) const;

		// 新增使用前置声明记录
		void UseForward(SourceLocation loc, const CXXRecordDecl *cxxRecordDecl);

		// 打印各文件的父文件
		void PrintParentsById();

		// 是否允许清理该c++文件（若不允许清理，则文件内容不会有任何变化）
		bool CanClean(FileID file) const;

		// 获取该文件的被包含信息，返回内容包括：该文件名、父文件名、被父文件#include的行号、被父文件#include的原始文本串
		string DebugBeIncludeText(FileID file, bool isAbsoluteName = false) const;

		// 获取该文件的被直接包含信息，返回内容包括：该文件名、被父文件#include的行号、被父文件#include的原始文本串
		string DebugBeDirectIncludeText(FileID file) const;

		// 获取该位置所在行的信息：所在行的文本、所在文件名、行号
		string DebugLocText(SourceLocation loc) const;

		// 获取文件所使用名称信息：文件名、所使用的类名、函数名、宏名等以及对应行号
		void DebugUsedNames(FileID file, const std::vector<UseNameInfo> &useNames) const;

		// 是否有必要打印该文件
		bool IsNeedPrintFile(FileID) const;

		// 获取属于本项目的允许被清理的文件数
		int GetCanCleanFileCount() const;

		// 打印引用记录
		void PrintUse() const;

		// 打印#include记录
		void PrintInclude() const;

		// 打印引用类名、函数名、宏名等的记录
		void PrintUsedNames() const;

		// 打印主文件循环引用的名称记录
		void PrintRootCycleUsedNames() const;

		// 打印循环引用的名称记录
		void PrintAllCycleUsedNames() const;

		// 打印主文件循环引用的文件列表
		void PrintRootCycleUse() const;

		// 打印循环引用的文件列表
		void PrintAllCycleUse() const;

		// 打印各文件对应的有用孩子文件记录
		void PrintCycleUsedChildren() const;

		// 打印允许被清理的所有文件列表
		void PrintAllFile() const;

		// 打印清理日志
		void PrintCleanLog() const;

		// 打印各文件内的命名空间
		void PrintNamespace() const;

		// 打印各文件内的using namespace
		void PrintUsingNamespace() const;

		// 打印各文件内应保留的using namespace
		void PrintRemainUsingNamespace() const;

		// 获取拼写位置
		SourceLocation GetSpellingLoc(SourceLocation loc) const;

		// 获取经过宏扩展后的位置
		SourceLocation GetExpasionLoc(SourceLocation loc) const;

		// 获取文件ID
		FileID GetFileID(SourceLocation loc) const;

		// 获取第1个文件#include第2个文件的文本串
		std::string GetRelativeIncludeStr(FileID f1, FileID f2) const;

		// 根据头文件搜索路径，将绝对路径转换为双引号包围的文本串，
		// 例如：假设有头文件搜索路径"d:/a/b/c" 则"d:/a/b/c/d/e.h" -> "d/e.h"
		string GetQuotedIncludeStr(const string& absoluteFilePath) const;

		// 替换指定范围文本
		void ReplaceText(FileID file, int beg, int end, string text);

		// 将文本插入到指定位置之前
		// 例如：假设有"abcdefg"文本，则在c处插入123的结果将是："ab123cdefg"
		void InsertText(FileID file, int loc, string text);

		// 移除指定范围文本，若移除文本后该行变为空行，则将该空行一并移除
		void RemoveText(FileID file, int beg, int end);

		// 移除指定文件内的无用#include
		void CleanByUnusedLine(const FileHistory &eachFile, FileID file);

		// 在指定文件内添加前置声明
		void CleanByForward(const FileHistory &eachFile, FileID file);

		// 在指定文件内替换#include
		void CleanByReplace(const FileHistory &eachFile, FileID file);

		// 清理指定文件
		void CleanBy(const FileHistoryMap &files);

		// 清理所有有必要清理的文件
		void CleanAllFile();

		// 清理主文件
		void CleanMainFile();

		// 打印头文件搜索路径
		void PrintHeaderSearchPath() const;

		// 用于调试：打印各文件引用的文件集相对于该文件的#include文本
		void PrintRelativeInclude() const;

		// 用于调试跟踪：打印是否有文件的被包含串被遗漏
		void PrintNotFoundIncludeLocForDebug();

		// 用于调试跟踪：打印各文件中是否同一行出现了2个#include
		void PrintSameLineForDebug();

		// 文件格式是否是windows格式，换行符为[\r\n]，类Unix下为[\n]
		bool IsWindowsFormat(FileID) const;

		// 合并可被移除的#include行
		void MergeUnusedLineTo(const FileHistory &newFile, FileHistory &oldFile) const;

		// 合并可新增的前置声明
		void MergeForwardLineTo(const FileHistory &newFile, FileHistory &oldFile) const;

		// 合并可替换的#include
		void MergeReplaceLineTo(const FileHistory &newFile, FileHistory &oldFile) const;

		void MergeCountTo(FileHistoryMap &oldFiles) const;

		// 取出当前cpp文件产生的待清理记录
		void TakeAllInfoTo(FileHistoryMap &out) const;

		// 将可清除的行按文件进行存放
		void TakeUnusedLineByFile(FileHistoryMap &out) const;

		// 将新增的前置声明按文件进行存放
		void TakeNewForwarddeclByFile(FileHistoryMap &out) const;

		// 该文件是否是预编译头文件
		bool IsPrecompileHeader(FileID file) const;

		// 将文件替换记录按父文件进行归类
		typedef std::map<FileID, std::set<FileID>> ChildrenReplaceMap;
		typedef std::map<FileID, ChildrenReplaceMap> ReplaceFileMap;
		void SplitReplaceByFile(ReplaceFileMap &replaces) const;

		// 取出指定文件的#include替换信息
		void TakeBeReplaceOfFile(FileHistory &eachFile, FileID top, const ChildrenReplaceMap &childernReplaces) const;

		// 取出各文件的#include替换信息
		void TakeReplaceByFile(FileHistoryMap &out) const;

		// 取出本文件的编译错误历史
		void TakeCompileErrorHistory(FileHistoryMap &out) const;

		// 该文件是否被循环引用到
		bool IsCyclyUsed(FileID file) const;

		// 该文件是否被主文件循环引用到
		bool IsRootCyclyUsed(FileID file) const;

		// 该文件是否可被替换
		bool IsReplaced(FileID file) const;

	public:
		// 当前正在解析的文件
		static ParsingFile *g_atFile;

	private:
		// 1. 各文件引用其他文件的记录列表：[文件] -> [引用的其他文件列表]
		std::map<FileID, std::set<FileID>>			m_uses;

		// 2. 主文件循环引用的文件集（即主文件直接引用或间接引用的文件列表，对于其中的任意文件，所引用的任一文件仍然在该文件集内)
		std::set<FileID>							m_rootCycleUse;

		// 3. 扩充后的循环引用文件集（对于其中的任意文件，所引用的任一文件仍然在该文件集内）
		std::set<FileID>							m_allCycleUse;

		// 4. 各文件的被引用的后代文件：[文件] -> [后代文件中被循环引用的部分]
		std::map<FileID, std::set<FileID>>			m_cycleUsedChildren;

		// 5. 各文件所包含的文件列表：[文件] -> [所include的文件]
		std::map<FileID, std::set<FileID>>			m_includes;

		// 6. 当前c++文件以及所包含的所有文件
		std::set<FileID>							m_files;

		// 7. 当前c++文件所涉及到的所有#include文本的位置以及所对应的范围： [#include "xxxx.h"中第一个双引号或<符号的位置] -> [#include整个文本串的范围]
		std::map<SourceLocation, SourceRange>		m_includeLocs;

		// 8. 全部有用的#include位置（用于查询某#include是否有用）
		std::set<SourceLocation>					m_usedLocs;

		// 9. 全部没用到的#include位置（用于查询某#include是否应被清除，之所以不使用FileID因为某些重复#include会被自动跳过，导致并未生成FileID）
		std::set<SourceLocation>					m_unusedLocs;

		// 10. 父文件ID映射表：[文件] -> [父文件]
		std::map<FileID, FileID>					m_parentIDs;

		// 11. 可被置换的#include列表：[文件] -> [该文件可被替换为的文件]
		std::map<FileID, std::set<FileID>>			m_replaces;

		// 12. 各文件中可被置换的#include列表：[文件] -> [该文件中哪些文件可被替换成为其他的哪些文件]
		ReplaceFileMap								m_splitReplaces;

		// 13. 头文件搜索路径列表
		std::vector<HeaderSearchDir>				m_headerSearchPaths;

		// 14. 各文件所使用的class、struct、union指针或引用的记录：[文件] -> [该文件所使用的class、struct、union指针或引用]
		std::map<FileID, set<const CXXRecordDecl*>>	m_forwardDecls;

		// 15. 各文件所使用的类名、函数名、宏名等的名称记录：[文件] -> [该文件所使用的类名、函数名、宏名等]
		std::map<FileID, std::vector<UseNameInfo>>	m_useNames;

		// 16. 各文件内声明的命名空间记录：[文件] -> [该文件内的命名空间记录]
		std::map<FileID, std::set<std::string>>		m_namespaces;

		// 17. 各文件内声明的using namespace记录：[文件] -> [该文件内的using namespace记录]
		std::map<FileID, std::set<std::string>>		m_usingNamespaces;

		// 18. 应保留的using namespace记录：[using namespace的位置] -> [using namespace的全称]
		std::map<SourceLocation, NamespaceInfo>		m_remainUsingNamespaces;

	private:
		clang::Rewriter*							m_rewriter;
		clang::SourceManager*						m_srcMgr;
		clang::CompilerInstance*					m_compiler;

		// 本文件的编译错误历史
		CompileErrorHistory							m_compileErrorHistory;

		// 当前打印索引，仅用于日志打印
		mutable int									m_printIdx;
	};
}

using namespace cxxcleantool;

#endif // _parsing_cpp_h_