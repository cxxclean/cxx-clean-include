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
}

namespace cxxcleantool
{
	struct HeaderSearchPath
	{
		HeaderSearchPath(const string& dir, SrcMgr::CharacteristicKind pathType)
			: path(dir)
			, path_type(pathType)
		{
		}

		string						path;
		SrcMgr::CharacteristicKind	path_type;
	};

	string ToLinuxPath(const char *path);

	string FixPath(const string& path);

	// 当前正在解析的c++文件的内部#include信息
	class ParsingFile
	{
	public:
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

		std::vector<HeaderSearchPath> ComputeHeaderSearchPaths(clang::HeaderSearch &headerSearch);

		std::vector<HeaderSearchPath> SortHeaderSearchPath(const std::map<string, SrcMgr::CharacteristicKind>& include_dirs_map);

		std::string FindFileInSearchPath(const vector<HeaderSearchPath>& searchPaths, const std::string& fileName);

		void GenerateUnusedTopInclude();

		inline bool IsLocBeUsed(SourceLocation loc) const;

		void GenerateResult();

		/*
			根据家族建立直接引用关系，若a.h包含b.h，且a.h引用了b.h中的内容，则称a.h直接引用b.h，注意，直接引用的双方必须是父子关系
			某些情况会比较特殊，例如，现有如下文件图：
			（令->表示#include，如：b.h-->c.h表示b.h包含c.h，令->>表示直接引用）：

			特殊情况1：
			    a.h --> b1.h --> c1.h                                     a.h --> b1.h -->  c1.h
					|                     [左图中若a.h引用c.h，则有右图]       |
					--> b.h  --> c.h                                          ->> b.h  ->> c.h

				即：a.h直接引用b.h，b.h直接引用c.h

			特殊情况2：
			    a.h --> b1.h --> c1.h --> d1.h                                        a.h ->> b1.h ->> c1.h ->> d1.h
				    |                     ^		    [左图中若d.h引用d1.h，则有右图]        |                      ^
					--> b.h  --> c.h  --> d.h	                                     	  ->> b.h  ->> c.h  ->> d.h
				（^表示引用关系，此图中表示d.h引用到d1.h中的内容）

			    即：a.h直接引用b1.h和b.h，b1.h直接引用c1.h，c1.h直接引用d1.h，依次类推
		*/
		void GenerateRootCycleUse();

		void GenerateAllCycleUse();

		bool TryAddAncestorOfCycleUse();

		void GenerateCycleUsedChildren();

		// 新增依赖文件，返回结果表示是否新增了一些待处理的文件，是true、否false
		bool ExpandAllCycleUse(FileID newCycleUse);

		FileID GetCanReplaceTo(FileID top);

		void GenerateUnusedInclude();

		// 从指定的文件列表中找到属于传入文件的后代
		std::set<FileID> GetChildren(FileID ancestor, std::set<FileID> all_children/* 包括非ancestor孩子的文件 */);

		// 从文件后代中找到被指定文件使用的所有文件
		std::set<FileID> GetDependOnFile(FileID ancestor, FileID child);

		void GetCycleUseFile(FileID top, std::set<FileID> &out);

		int GetDepth(FileID child);

		// 获取离孩子们最近的共同祖先
		FileID GetCommonAncestor(const std::set<FileID> &children);

		// 获取2个孩子们最近的共同祖先
		FileID GetCommonAncestor(FileID child_1, FileID child_2);

		// 当前文件之前是否已有文件声明了该class、struct、union
		bool HasRecordBefore(FileID cur, const CXXRecordDecl &cxxRecord);

		void GenerateCanReplace();

		void GenerateCanForwardDeclaration();

		void GenerateCount();

		std::string GetSourceOfRange(SourceRange range);

		std::string GetSourceOfLine(SourceLocation loc);

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
		SourceRange GetCurLine(SourceLocation loc);

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
		SourceRange GetCurLineWithLinefeed(SourceLocation loc);

		// 根据传入的代码位置返回下一行的范围
		SourceRange GetNextLine(SourceLocation loc);

		bool IsEmptyLine(SourceRange line);

		int GetLineNo(SourceLocation loc);

		// 是否是换行符
		bool IsNewLineWord(SourceLocation loc);

		/*
			获取对应于传入文件ID的#include文本
			例如，假设有b.cpp，内容如下：
				1. #include "./a.h"
				2. #include "a.h"

			其中第1行和第2行包含的a.h是虽然同一个文件，但FileID是不一样的

			现传入第一行a.h的FileID，则结果将返回
			#include "./a.h"
		*/
		std::string GetRawIncludeStr(FileID file);

		// cur位置的代码使用src位置的代码
		void Use(SourceLocation cur, SourceLocation src, const char* name = nullptr);

		void UseName(FileID file, FileID beusedFile, const char* name = nullptr, int line = 0);

		// 根据当前文件，查找第2层的祖先（令root为第一层），若当前文件即处于第2层，则返回当前文件
		FileID GetLvl2Ancestor(FileID file, FileID root);

		// 第2个文件是否是第1个文件的祖先
		bool IsAncestor(FileID yound, FileID old) const;

		// 第2个文件是否是第1个文件的祖先
		bool IsAncestor(FileID yound, const char* old) const;

		// 第2个文件是否是第1个文件的祖先
		bool IsAncestor(const char* yound, FileID old) const;

		bool IsCommonAncestor(const std::set<FileID> &children, FileID old);

		bool HasParent(FileID child);

		FileID GetParent(FileID child);

		// 当前文件使用目标文件
		void UseIinclude(FileID file, FileID beusedFile, const char* name = nullptr, int line = 0);

		void UseMacro(SourceLocation loc, const MacroDefinition &macro, const Token &macroName);

		// 当前位置使用目标类型
		void UseType(SourceLocation loc, const QualType &t);

		string GetCxxrecordName(const CXXRecordDecl &cxxRecordDecl);

		// 从指定位置起，跳过之后的注释，直到获得下一个token
		bool LexSkipComment(SourceLocation Loc, Token &Result);

		// 返回插入前置声明所在行的开头
		SourceLocation GetInsertForwardLine(FileID at, const CXXRecordDecl &cxxRecord);

		void UseForward(SourceLocation loc, const CXXRecordDecl *cxxRecordDecl);

		void UseVar(SourceLocation loc, const QualType &var);

		void OnUseDecl(SourceLocation loc, const NamedDecl *nameDecl);

		void OnUseRecord(SourceLocation loc, const CXXRecordDecl *record);

		void PrintParentsById();

		bool CanClean(FileID file);

		string DebugFileIncludeText(FileID file, bool is_absolute_name = false);

		string DebugFileDirectUseText(FileID file);

		string DebugLocText(SourceLocation loc);

		string DebugLocIncludeText(SourceLocation loc);

		void DebugUsedNames(FileID file, const std::vector<UseNameInfo> &useNames);

		bool IsNeedPrintFile(FileID);

		void PrintUse();

		void PrintUsedNames();

		void PrintRootCycleUsedNames();

		void PrintAllCycleUsedNames();

		void PrintRootCycleUse();

		void PrintAllCycleUse();

		void PrintUsedChildren();

		void PrintAllFile();

		void PrintUsedTopInclude();

		void PrintTopIncludeById();

		static void PrintUnusedIncludeOfFiles(FileHistoryMap &files);

		static void PrintCanForwarddeclOfFiles(FileHistoryMap &files);

		static void PrintCanReplaceOfFiles(FileHistoryMap &files);

		void PrintUnusedInclude();

		void PrintCanForwarddecl();

		void PrintUnusedTopInclude();

		const char* GetFileName(FileID file) const;

		static string GetAbsoluteFileName(const char *raw_file_name);

		string GetAbsoluteFileName(FileID file) const;

		inline bool IsBlank(char c)
		{
			return (c == ' ' || c == '\t');
		}

		const char* GetIncludeValue(const char* include_str);

		std::string GetRelativeIncludeStr(FileID f1, FileID f2);

		bool IsAbsolutePath(const string& path);

		// 简化路径
		// d:/a/b/c/../../d/ -> d:/d/
		static std::string SimplifyPath(const char* path);

		static string GetAbsolutePath(const char *path);

		static string GetAbsolutePath(const char *base_path, const char* relative_path);

		string GetParentPath(const char* path);

		// 根据头文件搜索路径，将绝对路径转换为双引号包围的文本串，
		// 例如：假设有头文件搜索路径"d:/a/b/c" 则"d:/a/b/c/d/e.h" -> "d/e.h"
		string GetQuotedIncludeStr(const string& absoluteFilePath);

		void Clean();

		void ReplaceText(FileID file, int beg, int end, string text);

		void InsertText(FileID file, int loc, string text);

		void RemoveText(FileID file, int beg, int end);

		void CleanByUnusedLine(const FileHistory &eachFile, FileID file);

		void CleanByForward(const FileHistory &eachFile, FileID file);

		void CleanByReplace(const FileHistory &eachFile, FileID file);

		void CleanBy(const FileHistoryMap &files);

		void CleanAllFile();

		void CleanMainFile();

		void PrintCanReplace();

		void PrintHeaderSearchPath();

		void PrintRelativeInclude();

		void PrintRangeToFileName();

		// 用于调试跟踪
		void PrintNotFoundIncludeLoc();

		// 用于调试跟踪
		void PrintSameLine();

		void Print();

		bool InitHeaderSearchPath(clang::SourceManager* srcMgr, clang::HeaderSearch &header_search);

		// 文件格式是否是windows格式，换行符为[\r\n]，类Unix下为[\n]
		bool IsWindowsFormat(FileID);

		void MergeTo(FileHistoryMap &old);

		void MergeUnusedLineTo(const FileHistory &newFile, FileHistory &oldFile) const;

		void MergeForwardLineTo(const FileHistory &newFile, FileHistory &oldFile) const;

		void MergeReplaceLineTo(const FileHistory &newFile, FileHistory &oldFile) const;

		void MergeCountTo(FileHistoryMap &oldFiles) const;

		void TakeAllInfoTo(FileHistoryMap &out);

		// 将可清除的行按文件进行存放
		void TakeUnusedLineByFile(FileHistoryMap &out);

		// 将新增的前置声明按文件进行存放
		void TakeNewForwarddeclByFile(FileHistoryMap &out);

		bool IsForceIncluded(FileID file);

		typedef std::map<FileID, std::set<FileID>> ChildrenReplaceMap;
		typedef std::map<FileID, ChildrenReplaceMap> ReplaceFileMap;
		void SplitReplaceByFile(ReplaceFileMap &replaces);

		void TakeBeReplaceOfFile(FileHistory &eachFile, FileID top, const ChildrenReplaceMap &childernReplaces);

		// 取出各文件的#include替换信息
		void TakeReplaceByFile(FileHistoryMap &out);

		bool IsCyclyUsed(FileID file);

		bool IsReplaced(FileID file);

	public:
		// 循环引用列表
		std::map<FileID, std::set<FileID>>			m_uses;					// 1.  use列表
		std::set<FileID>							m_rootCycleUse;			// 1.  主文件循环引用的文件列表
		std::set<FileID>							m_allCycleUse;			// 1.  主文件循环引用的文件列表
		std::map<FileID, std::set<FileID>>			m_cycleUsedChildren;	// 1.  主文件循环引用全部有用的孩子

		//
		std::set<FileID>							m_topUsedIncludes;		// 3.  顶层有用的include FileID列表
		std::vector<FileID>							m_topIncludeIDs;		// 4.  顶层全部的include FileID列表
		std::map<string, FileID>					m_files;				// 4.  全部的include FileID列表
		std::map<SourceLocation, const char*>		m_locToFileName;		// 5.  [#include "xxxx.h"的代码位置] -> [对应的xxxx.h文件路径]
		std::map<SourceLocation, SourceRange>		m_locToRange;			// 6.  [#include "xxxx.h"中"xxxx.h"的位置] -> [#到最后一个双引号的位置]
		std::vector<SourceLocation>					m_topUnusedIncludeLocs;	// 7.  主文件中全部没用到的include位置（之所以不使用FileID因为某些重复#include会被自动跳过，导致并未生成FileID）
		std::set<FileID>							m_usedFiles;			// 8.  全部有用的文件
		std::map<FileID, std::set<FileID>>			m_usedChildren;			// 8.  全部有用的孩子
		std::set<SourceLocation>					m_usedLocs;				// 9.  全部有用的include位置（用于查询某位置是否有用）
		std::set<SourceLocation>					m_unusedLocs;			// 10. 全部没用到的include位置（之所以不使用FileID因为某些重复#include会被自动跳过，导致并未生成FileID）
		std::map<FileID, FileID>					m_parentIDs;			// 11. 父文件ID映射表
		std::map<FileID, std::set<FileID>>			m_replaces;				// 12. 可被置换的#include列表
		ReplaceFileMap								m_splitReplaces;		//
		std::vector<HeaderSearchPath>				m_headerSearchPaths;	// 13.
		std::map<FileID, set<const CXXRecordDecl*>>	m_forwardDecls;			// 15. 文件 -> 所使用的前置声明
		std::map<FileID, std::vector<UseNameInfo>>	m_useNames;				// 1.  use列表

		Rewriter*									m_rewriter;
		SourceManager*								m_srcMgr;
		// CompilerInstance*							m_compiler;

		// 当前打印索引，仅用于日志调试
		int											m_i;

		// 是否覆盖原来的c++文件
		static bool									m_isOverWriteOption;
	};
}

using namespace cxxcleantool;

#endif // _parsing_cpp_h_