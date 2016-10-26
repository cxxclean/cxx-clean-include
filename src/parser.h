//------------------------------------------------------------------------------
// 文件: parser.h
// 作者: 洪坤安
// 说明: 解析当前cpp文件
// Copyright (c) 2016 game. All rights reserved.
//------------------------------------------------------------------------------

#ifndef _parser_h_
#define _parser_h_

#include <string>
#include <vector>
#include <set>
#include <map>

#include <clang/Basic/SourceLocation.h>
#include <clang/Basic/SourceManager.h>

#include "history.h"
#include <unordered_set>

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
	class NamespaceAliasDecl;
	class UsingDirectiveDecl;
	class FunctionDecl;
	class MacroArgs;
	class VarDecl;
	class ValueDecl;
	class RecordDecl;
	class UsingDecl;
	class TemplateArgument;
	class TemplateArgumentList;
	class TemplateDecl;
	class CXXConstructorDecl;
	class DeclContext;
}

namespace cxxclean
{
	// 当前正在解析的c++文件信息
	class ParsingFile
	{
		// [文件名] -> [路径类别：系统路径或用户路径]
		typedef std::map<string, SrcMgr::CharacteristicKind> IncludeDirMap;

		// [文件] -> [可被替换到的文件]
		typedef std::map<FileID, FileID> ChildrenReplaceMap;

		// [文件] -> [该文件中哪些文件可被替换]
		typedef std::map<FileID, ChildrenReplaceMap> ReplaceFileMap;

		// class、struct、union集合
		typedef std::set<const CXXRecordDecl*> RecordSet;

		// [位置] -> [使用的class、struct引用或指针]
		typedef std::map<SourceLocation, RecordSet> LocUseRecordsMap;

		// [文件] -> [使用的class、struct引用或指针]
		typedef std::map<FileID, RecordSet> FileUseRecordsMap;

		// [文件] -> [该文件所使用的class、struct、union指针或引用]
		typedef std::map<FileID, LocUseRecordsMap> UseRecordsByFileMap;

		struct NodeHash
		{
			std::size_t operator () (const FileID &file) const
			{
				return file.getHashValue();
			}
		};

		// 文件集
		//typedef std::unordered_set<FileID, NodeHash> FileSet;
		typedef std::set<FileID> FileSet;

		// 用于调试：引用的名称
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

		// 命令空间信息
		struct NamespaceInfo
		{
			NamespaceInfo()
				: ns(nullptr)
			{
			}

			std::string			name;		// 命名空间的声明，如：namespace A{ namespace B { namespace C {} } }
			const NamespaceDecl	*ns;		// 命名空间的定义
		};

	public:
		// 头文件搜索路径
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

		~ParsingFile();

		inline clang::SourceManager& GetSrcMgr() const { return *m_srcMgr; }

		// 添加父文件关系
		void AddParent(FileID child, FileID parent);

		// 添加包含文件记录
		void AddInclude(FileID file, FileID beInclude);

		// 添加#include的位置记录
		void AddIncludeLoc(SourceLocation loc, SourceRange range);

		// 添加成员文件
		void AddFile(FileID file);

		// 为当前cpp文件的清理作前期准备
		void InitCpp();

		// 开始分析
		void Analyze();

		// 是否包含了任意文件
		inline bool HasAnyInclude(FileID a) const;

		// 计算文件深度
		int GetDeepth(FileID file) const;

		// 是否为可前置声明的类型
		bool IsForwardType(const QualType &var);

		// a文件使用b文件
		inline void UseInclude(FileID a, FileID b, const char* name = nullptr, int line = 0);

		// 当前位置使用指定的宏
		void UseMacro(SourceLocation loc, const MacroDefinition &macro, const Token &macroName, const MacroArgs *args = nullptr);

		// 新增使用变量记录
		void UseVarType(SourceLocation loc, const QualType &var);

		// 引用构造函数
		void UseConstructor(SourceLocation loc, const CXXConstructorDecl *constructor);

		// 引用变量声明
		void UseVarDecl(SourceLocation loc, const VarDecl *var);

		// 引用变量声明（为左值）、函数表示、enum常量
		void UseValueDecl(SourceLocation loc, const ValueDecl *valueDecl);

		// 引用带有名称的声明
		void UseNameDecl(SourceLocation loc, const NamedDecl *nameDecl);

		// 新增使用函数声明记录
		void UseFuncDecl(SourceLocation loc, const FunctionDecl *funcDecl);

		// 引用模板参数
		void UseTemplateArgument(SourceLocation loc, const TemplateArgument &arg);

		// 引用模板参数列表
		void UseTemplateArgumentList(SourceLocation loc, const TemplateArgumentList *args);

		// 引用模板定义
		void UseTemplateDecl(SourceLocation loc, const TemplateDecl *decl);

		// 新增使用class、struct、union记录
		void UseRecord(SourceLocation loc, const RecordDecl *record);

		// cur位置的代码使用src位置的代码
		inline void Use(SourceLocation cur, SourceLocation src, const char* name = nullptr);

		// 当前位置使用目标类型（注：QualType包含对某个类型的const、volatile、static等的修饰）
		void UseQualType(SourceLocation loc, const QualType &t);

		// 当前位置使用目标类型（注：Type代表某个类型，但不含const、volatile、static等的修饰）
		void UseType(SourceLocation loc, const Type *t);

		// 引用上下文，如命名空间
		void UseContext(SourceLocation loc, const DeclContext*);

		// 引用嵌套名字修饰符
		void UseQualifier(SourceLocation loc, const NestedNameSpecifier*);

		// 引用命名空间声明
		void UseNamespaceDecl(SourceLocation loc, const NamespaceDecl*);

		// 引用命名空间别名
		void UseNamespaceAliasDecl(SourceLocation loc, const NamespaceAliasDecl*);

		// 声明了命名空间
		void DeclareNamespace(const NamespaceDecl *d);

		// using了命名空间，比如：using namespace std;
		void UsingNamespace(const UsingDirectiveDecl *d);

		// using了命名空间下的某类，比如：using std::string;
		void UsingXXX(const UsingDecl *d);

		// 获取命名空间的全部路径，例如，返回namespace A{ namespace B{ class C; }}
		std::string GetNestedNamespace(const NamespaceDecl *d);

		// 文件b是否直接#include文件a
		bool IsIncludedBy(FileID a, FileID b);

		// 获取文件a的指定名称的直接包含文件
		FileID GetIncludeByName(FileID a, const char* includeName) const;

		// 获取文件a的指定名称的后代文件
		FileID GetChildByName(FileID a, const char* childFileName) const;

		// 当a使用b时，如果b对应的文件被包含多次，从b的同名文件中选取一个最好的文件
		FileID GetBestSameFile(FileID a, FileID b) const;

		// 开始清理文件（将改动c++源文件）
		void Clean();

		// 将清理结果回写到c++源文件，返回：true回写文件时发生错误 false回写成功
		// （本接口拷贝自Rewriter::overwriteChangedFiles，唯一的不同是回写成功时会删除文件缓存）
		bool Overwrite();

		// 打印索引 + 1
		std::string AddPrintIdx() const;

		// 打印信息
		void Print();

		// 获取本文件的编译错误历史
		CompileErrorHistory& GetCompileErrorHistory() { return m_compileErrorHistory; }

		// 获取指定范围的文本
		std::string GetSourceOfRange(SourceRange range) const;

		// 获取指定位置的文本
		const char* GetSourceAtLoc(SourceLocation loc) const;

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

		// 用于调试：获取文件的绝对路径和相关信息
		string GetDebugFileName(FileID file) const;

	private:
		// 获取头文件搜索路径
		std::vector<HeaderSearchDir> TakeHeaderSearchPaths(clang::HeaderSearch &headerSearch) const;

		// 将头文件搜索路径根据长度由长到短排列
		std::vector<HeaderSearchDir> SortHeaderSearchPath(const IncludeDirMap& include_dirs_map) const;

		// 指定位置的对应的#include是否被用到
		inline bool IsLocBeUsed(SourceLocation loc) const;

		// 该文件是否被包含多次
		inline bool HasSameFileByName(const char *file) const;

		// 2个文件是否文件名一样
		inline bool IsSameName(FileID a, FileID b) const;

		// 该文件名是否被包含多次
		inline bool HasSameFile(FileID file) const;

		// 文件a是否使用到文件b
		bool IsUse(FileID a, FileID b) const;

		// 文件a是否直接包含文件b
		bool IsInclude(FileID a, FileID b) const;

		// 从指定的文件列表中找到属于传入文件的后代
		FileSet GetChildren(FileID ancestor, FileSet all_children/* 包括非ancestor孩子的文件 */);

		// 获取文件的深度（令主文件的高度为0）
		int GetDepth(FileID child) const;

		// 获取离孩子们最近的共同祖先
		FileID GetCommonAncestor(const FileSet &children) const;

		// 获取2个孩子们最近的共同祖先
		FileID GetCommonAncestor(FileID child_1, FileID child_2) const;

		FileID GetCommonAncestorBySame(FileID a, FileID b) const;

		// 获取指定位置所在行的文本
		std::string GetSourceOfLine(SourceLocation loc) const;

		/*
			根据传入的代码位置返回该行的正文范围（不含换行符）：[该行开头，该行末]
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
		SourceRange GetCurFullLine(SourceLocation loc) const;

		// 根据传入的代码位置返回下一行的范围
		SourceRange GetNextLine(SourceLocation loc) const;

		// 是否为空行
		bool IsEmptyLine(SourceRange line);

		// 获取行号
		int GetLineNo(SourceLocation loc) const;

		// 获取文件对应的被包含行号
		int GetIncludeLineNo(FileID) const;

		// 获取文件对应的#include含范围
		SourceRange GetIncludeRange(FileID) const;

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
		std::string GetIncludeLine(FileID file) const;

		// 获取文件对应的#include所在的整行
		std::string GetIncludeFullLine(FileID file) const;

		inline void UseName(FileID file, FileID beusedFile, const char* name = nullptr, int line = 0);

		// 根据当前文件，查找第2层的祖先（令root为第一层），若当前文件的父文件即为主文件，则返回当前文件
		FileID GetLvl2Ancestor(FileID file, FileID root) const;

		// 第2个文件是否是第1个文件的祖先（主文件是所有其他文件的祖先）
		bool IsAncestor(FileID yound, FileID old) const;

		// 第2个文件是否是第1个文件的祖先
		bool IsAncestor(FileID yound, const char* old) const;

		// 第2个文件是否是第1个文件的祖先
		bool IsAncestor(const char* yound, FileID old) const;

		// 是否为孩子文件的共同祖先
		bool IsCommonAncestor(const FileSet &children, FileID old) const;

		// 获取父文件（主文件没有父文件）
		FileID GetParent(FileID child) const;

		// 获取c++中class、struct、union的全名，结果将包含命名空间
		// 例如：
		//     传入类C，C属于命名空间A中的命名空间B，则结果将返回：namespace A{ namespace B{ class C; }}
		string GetRecordName(const RecordDecl &recordDecl) const;

		// 从指定位置起，跳过之后的注释，直到获得下一个token
		bool LexSkipComment(SourceLocation Loc, Token &Result);

		// 新增使用前置声明记录（对于不必要添加的前置声明将在之后进行清理）
		void UseForward(SourceLocation loc, const CXXRecordDecl *cxxRecordDecl);

		// 打印各文件的父文件
		void PrintParent();

		// 打印各文件的孩子文件
		void PrintChildren();

		// 是否允许清理该c++文件（若不允许清理，则文件内容不会有任何变化）
		bool CanClean(FileID file) const;

		// 获取该文件的被包含信息，返回内容包括：该文件名、父文件名、被父文件#include的行号、被父文件#include的原始文本串
		string DebugBeIncludeText(FileID file) const;

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
		void ReplaceText(FileID file, int beg, int end, const char* text);

		// 将文本插入到指定位置之前
		// 例如：假设有"abcdefg"文本，则在c处插入123的结果将是："ab123cdefg"
		void InsertText(FileID file, int loc, const char* text);

		// 移除指定范围文本，若移除文本后该行变为空行，则将该空行一并移除
		void RemoveText(FileID file, int beg, int end);

		// 移除指定文件内的无用行
		void CleanByDelLine(const FileHistory &history, FileID file);

		// 在指定文件内添加前置声明
		void CleanByForward(const FileHistory &history, FileID file);

		// 在指定文件内替换#include
		void CleanByReplace(const FileHistory &history, FileID file);

		// 在指定文件内新增行
		void CleanByAdd(const FileHistory &history, FileID file);

		// 根据历史清理指定文件
		void CleanByHistory(const FileHistoryMap &historys);

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

		// 文件格式是否是windows格式，换行符为[\r\n]，类Unix下为[\n]
		bool IsWindowsFormat(FileID) const;

		void InitHistory(FileID file, FileHistory &history) const;

		// 取出对当前cpp文件的分析结果
		void TakeHistorys(FileHistoryMap &out) const;

		// 该文件是否是预编译头文件
		bool IsPrecompileHeader(FileID file) const;

		// 取出本文件的编译错误历史
		void TakeCompileErrorHistory(FileHistoryMap &out) const;

		// a文件是否在b位置之前
		bool IsFileBeforeLoc(FileID a, SourceLocation b) const;

		// 打印引用记录
		void PrintUse() const;

		// 打印#include记录
		void PrintInclude() const;

		// 打印引用类名、函数名、宏名等的记录
		void PrintUsedNames() const;

		// 打印可转为前置声明的类指针或引用记录
		void PrintUseRecord() const;

		// 打印最终的前置声明记录
		void PrintFinalForwardDecl() const;

		// 打印允许被清理的所有文件列表
		void PrintAllFile() const;

		// 打印清理日志
		void PrintCleanLog() const;

		// 打印各文件内的命名空间
		void PrintNamespace() const;

		// 打印各文件内的using namespace
		void PrintUsingNamespace() const;

		//================== 模式2所需的接口（见project.h中的CleanMode） ==================//

		// 根据主文件的依赖关系，生成相关文件的依赖文件集
		void GenerateRely();

		// 尝试添加各个被依赖文件的祖先文件，返回值：true表示依赖文件集被扩张、false表示依赖文件夹不变
		bool TryAddAncestor();

		// 记录各文件的被依赖后代文件
		void GenerateRelyChildren();

		// 新增依赖文件，返回结果表示是否新增了一些待处理的文件，是true、否false
		bool ExpandRely(FileID top);

		// 获取该文件可被替换到的文件，若无法被替换，则返回空文件id
		FileID GetCanReplaceTo(FileID top) const;

		// 生成无用#include的记录
		void GenerateUnusedInclude();

		/*
			获取指定文件直接依赖和间接依赖的文件集

			计算过程是：
				获取top所依赖的文件，并循环获取这些依赖文件所依赖的其他文件，直到所有的被依赖文件均已被处理

			例如（-->表示包含）：
				hello.cpp --> a.h  --> b.h
				          |
						  --> c.h  --> d.h --> e.h

				如果hello.cpp依赖b.h，而b.h又依赖e.h，则在本例中，hello.cpp的依赖文件列表为：
					hello.cpp、b.h、e.h
		*/
		void GetTopRelys(FileID top, FileSet &out) const;

		// 当前文件之前是否已有文件声明了该class、struct、union
		bool HasRecordBefore(FileID cur, const CXXRecordDecl &cxxRecord) const;

		// 是否应保留当前位置引用的class、struct、union的前置声明
		bool IsNeedClass(SourceLocation, const CXXRecordDecl &cxxRecord) const;

		// 生成文件替换列表
		void GenerateReplace();

		// 生成新增前置声明列表
		void GenerateForwardClass();
		
		// 将当前cpp文件产生的待清理记录与之前其他cpp文件产生的待清理记录合并
		void MergeTo(FileHistoryMap &old) const;

		// 合并可被移除的#include行
		void MergeUnusedLine(const FileHistory &newFile, FileHistory &oldFile) const;

		// 合并可新增的前置声明
		void MergeForwardLine(const FileHistory &newFile, FileHistory &oldFile) const;

		// 合并可替换的#include
		void MergeReplaceLine(const FileHistory &newFile, FileHistory &oldFile) const;

		// 返回插入前置声明所在行的开头
		SourceLocation GetInsertForwardLine(FileID at, const CXXRecordDecl &cxxRecord) const;

		// 将某些文件中的一些行标记为不可修改
		void SkipRelyLines(const FileSkipLineMap&) const;

		// 将可清除的行按文件进行存放
		void TakeUnusedLine(FileHistoryMap &out) const;

		// 将新增的前置声明按文件进行存放
		void TakeForwardClass(FileHistoryMap &out) const;

		// 将文件前置声明记录按文件进行归类
		void SplitForwardByFile(UseRecordsByFileMap&) const;

		// 取出指定文件的#include替换信息
		void TakeBeReplaceOfFile(FileHistory &history, FileID top, const ChildrenReplaceMap &childernReplaces) const;

		// 取出各文件的#include替换信息
		void TakeReplace(FileHistoryMap &out) const;

		// 是否禁止改动某文件
		bool IsSkip(FileID file) const;

		// 该文件是否被依赖
		inline bool IsRely(FileID file) const;

		// 该文件的所有同名文件是否被依赖（同一文件可被包含多次）
		bool IsRelyBySameName(FileID file) const;

		// 该文件是否被主文件循环引用到
		bool IsRelyByTop(FileID file) const;

		// 该文件是否可被替换
		bool IsReplaced(FileID file) const;

		// 打印主文件的依赖文件集
		void PrintTopRely() const;

		// 打印依赖文件集
		void PrintRely() const;

		// 打印各文件对应的有用孩子文件记录
		void PrintRelyChildren() const;


		//================== 模式3所需的接口（见project.h中的CleanMode） ==================//

		// 根据当前文件，查找第2层的祖先（令root为第一层），若当前文件的父文件即为主文件，则返回当前文件
		FileID GetLvl2AncestorBySame(FileID kid, FileID top) const;

		// 第2个文件是否是第1个文件的祖先（考虑同名文件）
		bool IsAncestorBySame(FileID yound, FileID old) const;

		// 该文件的所有同名文件是否被依赖（同一文件可被包含多次）
		bool HasMinKidBySameName(FileID top, FileID kid) const;

		// 是否应保留该位置引用的class、struct、union的前置声明
		bool IsNeedMinClass(FileID, const CXXRecordDecl &cxxRecord) const;

		void GetUseChain(FileID top, FileSet &chain) const;

		bool MergeMin();

		inline bool IsMinUse(FileID a, FileID b) const;

		inline bool IsUserFile(FileID file) const;

		inline bool IsOuterFile(FileID file) const;

		inline FileID GetTopOuterFileAncestor(FileID file) const;

		inline FileID SearchOuterFileAncestor(FileID file) const;

		void GenerateKidBySame();

		void GenerateForceIncludes();

		void GenerateOutFileAncestor();

		void GenerateUserUse();

		void GenerateMinUse();

		// 生成新增前置声明列表
		void GenerateMinForwardClass();

		// 裁剪前置声明列表
		void MinimizeForwardClass();

		void GetUseRecordsInKids(FileID top, const FileUseRecordsMap &recordMap, RecordSet &records);

		void GetKidsBySame(FileID top, FileSet &kids) const;

		// 返回插入前置声明所在行的开头
		SourceLocation GetMinInsertForwardLine(FileID at, const CXXRecordDecl &cxxRecord) const;

		bool HasMinKid(FileID top, FileID kid) const;

		void TakeOneReplace(ReplaceLine &replaceLine, FileID from, FileID to) const;

		// 取出记录，使得各文件仅包含自己所需要的头文件
		void TakeNeed(FileID top, FileHistory &out) const;

		// 祖先文件是否被强制包含
		bool IsAncestorForceInclude(FileID file) const;

		// 获取被强制包含祖先文件
		FileID GetAncestorForceInclude(FileID file) const;

		// 打印各文件的孩子文件
		void PrintUserChildren();

		// 打印
		void PrintSameChildren();

		// 打印被包含多次的文件
		void PrintSameFile() const;

		// 打印
		void PrintMinUse() const;

		// 打印
		void PrintMinKid() const;

		// 打印
		void PrintOutFileAncestor() const;

		// 打印
		void PrintUserUse() const;

	public:
		// 当前正在解析的文件
		static ParsingFile *g_atFile;

	private:
		//================== 最终分析结果 ==================//

		// [最终分析结果]. 分析当前cpp文件的结果：[c++文件名] -> [该文件的清理结果]
		FileHistoryMap								m_historys;


		//================== 模式1和2（见project.h中的CleanMode）：仅删除多余头文件（不考虑生成前置声明） ==================//

		// 模式1分析结果：全部没用到的#include位置（用于查询某#include是否应被清除，之所以不使用FileID因为某些重复#include会被自动跳过，导致并未生成FileID）
		std::set<SourceLocation>					m_unusedLocs;

		// 模式2分析结果：可被替换的#include列表：[文件ID] -> [该文件可被替换为的文件ID]
		std::map<FileID, FileID>					m_replaces;

		// 1. 主文件的依赖文件集（该集合是闭包的，即该集合中的任一文件所依赖的文件仍在集合内）
		FileSet										m_topRelys;

		// 2. 依赖文件集（该集合是闭包的，即该集合中的任一文件所依赖的文件仍在集合内）
		FileSet										m_relys;

		// 3. 各文件的被引用的后代文件：[文件ID] -> [后代文件中被循环引用的部分]
		std::map<FileID, FileSet>					m_relyChildren;

		// 4. 全部有用的#include位置（用于查询某#include是否有用）
		std::set<SourceLocation>					m_usedLocs;

		// 5. 各文件中可被置换的#include列表：[文件ID] -> [该文件中哪些文件可被替换成为其他的哪些文件]
		ReplaceFileMap								m_splitReplaces;

		//================== 模式3（见project.h中的CleanMode）：每个文件尽量只包含自己用到的文件（将自动生成前置声明） ==================//

		// 模式3分析结果：各文件的最小包含文件列表：[文件ID] -> [该文件仅应直接包含的文件ID列表]
		std::map<FileID, FileSet>					m_min;

		// 1. 各文件的后代文件（仅用户文件）：[文件ID] -> [该文件包含的全部后代文件ID（仅用户文件）]
		std::map<FileID, FileSet>					m_userChildren;		

		// 2. 各文件的后代文件（已对同名文件进行处理）：[文件名] -> [该文件包含的全部后代文件ID（已对同名文件进行处理）]
		std::map<std::string, FileSet>				m_childrenBySame;

		// 3. 各文件应包含的后代文件列表：[文件ID] -> [该文件应包含的后代文件ID列表]
		std::map<FileID, FileSet>					m_minKids;

		// 4. 各个项目外文件的祖先项目外文件：[文件ID] -> [对应的祖先外部文件ID]（比如，假设项目文件A中有#include <vector>，那么<vector>文件所包含的所有后代文件的祖先都是<vector>文件）
		std::map<FileID, FileID>					m_outFileAncestor;

		// 5. 项目内文件的引用关系：[项目内文件ID] -> [所引用的项目内文件ID列表 + 项目外文件ID列表]
		std::map<FileID, FileSet>					m_userUses;

		// 6. 被强制包含的文件ID列表
		FileSet										m_forceIncludes;
		
		// 7. 每个文件最终应新增的前置声明
		FileUseRecordsMap							m_minUseRecords;


		//================== 一些通用的临时数据 ==================//

		// 1. 各文件引用其他文件的记录：[文件ID] -> [引用的其他文件列表]（例如，假设A.h用到了B.h中的class B，则认为A.h引用了B.h）
		std::map<FileID, FileSet>					m_uses;

		// 2. 各文件所包含的文件列表：[文件ID] -> [所include的文件]
		std::map<FileID, FileSet>					m_includes;

		// 3. 所有文件ID
		FileSet										m_files;

		// 4. 所有#include的位置对应的范围： [#include "xxxx.h"中第一个双引号或<符号的位置] -> [#include整个文本串的范围]
		std::map<SourceLocation, SourceRange>		m_includeLocs;

		// 5. 父文件关系：[文件ID] -> [父文件ID]
		std::map<FileID, FileID>					m_parents;

		// 6. 头文件搜索路径列表
		std::vector<HeaderSearchDir>				m_headerSearchPaths;

		// 7.1 每个位置所使用的class、struct（指针、引用），用于生成前置声明：[位置] -> [所使用的class、struct、union指针或引用]
		LocUseRecordsMap							m_locUseRecords;

		// 7.2 每个文件所使用的class、struct（指针、引用），用于生成前置声明：[位置] -> [所使用的class、struct、union指针或引用]
		FileUseRecordsMap							m_fileUseRecordPointers;

		// 7.3 每个文件所使用的class、struct（非指针、非引用），用于避免生成多余的前置声明
		FileUseRecordsMap							m_fileUseRecords;

		// 8. using namespace记录：[using namespace的位置] -> [对应的namespace定义]
		map<SourceLocation, const NamespaceDecl*>	m_usingNamespaces;

		// 9. 各文件的后代：[文件ID] -> [该文件包含的全部后代文件ID]
		std::map<FileID, FileSet>					m_children;

		// 10. 仅用于打印：各文件所使用的类名、函数名、宏名等的名称记录：[文件ID] -> [该文件所使用的其他文件中的类名、函数名、宏名、变量名等]
		std::map<FileID, std::vector<UseNameInfo>>	m_useNames;

		// 11. 仅用于打印：各文件内声明的命名空间记录：[文件] -> [该文件内的命名空间记录]
		std::map<FileID, std::set<std::string>>		m_namespaces;

		// 12. 同一个文件名对应的不同文件ID：[文件名] -> [同名文件ID列表]
		std::map<std::string, FileSet>				m_sameFiles;

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

using namespace cxxclean;

#endif // _parser_h_