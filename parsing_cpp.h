///<------------------------------------------------------------------------------
//< @file:   parsing_cpp.h
//< @author: 洪坤安
//< @date:   2016年2月22日
//< @brief:
//< Copyright (c) 2015 game. All rights reserved.
///<------------------------------------------------------------------------------

#ifndef _parsing_cpp_h_
#define _parsing_cpp_h_

#include <string>
#include <vector>
#include <set>
#include <map>

#include <clang/Basic/SourceLocation.h>
#include <clang/Basic/SourceManager.h>

#include "whole_project.h"

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

	string to_linux_path(const char *path);

	string fix_path(const string& path);

	// 当前正在解析的c++文件的内部#include信息
	class ParsingFile
	{
	public:
		struct UseNameInfo
		{
			void add_name(const char* name)
			{
				if (names.find(name) != names.end())
				{
					return;
				}

				names.insert(name);
				nameVec.push_back(name);
			}

			FileID				file;
			std::vector<string>	nameVec;
			std::set<string>	names;
		};

		std::vector<HeaderSearchPath> ComputeHeaderSearchPaths(clang::HeaderSearch &headerSearch);

		std::vector<HeaderSearchPath> SortHeaderSearchPath(const std::map<string, SrcMgr::CharacteristicKind>& include_dirs_map);

		std::string FindFileInSearchPath(const vector<HeaderSearchPath>& searchPaths, const std::string& fileName);

		void generate_unused_top_include();

		inline bool is_loc_be_used(SourceLocation loc) const;

		void direct_use_include(FileID ancestor, FileID beusedFile);

		void generate_result();

		void generate_root_cycle_use();

		void generate_all_cycle_use();

		bool try_add_ancestor_of_cycle_use();

		void generate_root_cycle_used_children();

		// 新增依赖文件，返回结果表示是否新增了一些待处理的文件，是true、否false
		bool expand_all_cycle_use(FileID newCycleUse);

		FileID get_can_replace_to(FileID top);

		void get_remain_node(std::set<FileID> &remain, const std::set<FileID> &history);

		void generate_direct_use_include();

		void generate_unused_include();

		// 从指定的文件列表中找到属于传入文件的后代
		std::set<FileID> get_children(FileID ancestor, std::set<FileID> all_children/* 包括非ancestor孩子的文件 */);

		// 从文件后代中找到被指定文件使用的所有文件
		std::set<FileID> get_depend_on_file(FileID ancestor, FileID child);

		void get_cycle_use_file(FileID top, std::set<FileID> &out);

		int get_depth(FileID child);

		// 获取离孩子们最近的共同祖先
		FileID get_common_ancestor(const std::set<FileID> &children);

		// 获取2个孩子们最近的共同祖先
		FileID get_common_ancestor(FileID child_1, FileID child_2);

		// 找到目标文件中位于before前的最后一个#include的位置
		SourceLocation get_last_include_loc_before(FileID at, SourceLocation before);

		// 找到目标文件中最后一个#include的位置
		SourceLocation get_last_include_loc(FileID at);

		// 获取top的所有被依赖的孩子文件
		// 获取top的所有孩子文件中child所依赖的部分
		std::set<FileID> get_family_of(FileID top);

		void generate_used_children();

		void generate_can_replace();

		void generate_can_replace_include_in_root();

		void generate_can_forward_declaration();

		void generate_count();

		std::string get_source_of_range(SourceRange range);

		std::string get_source_of_line(SourceLocation loc);

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
		SourceRange get_cur_line(SourceLocation loc);

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
		SourceRange get_cur_line_with_linefeed(SourceLocation loc);

		// 根据传入的代码位置返回下一行的范围
		SourceRange get_next_line(SourceLocation loc);

		bool is_empty_line(SourceRange line);

		int get_line_no(SourceLocation loc);

		// 是否是换行符
		bool is_new_line_word(SourceLocation loc);

		/*
			获取对应于传入文件ID的#include文本
			例如，假设有b.cpp，内容如下：
				1. #include "./a.h"
				2. #include "a.h"

			其中第1行和第2行包含的a.h是虽然同一个文件，但FileID是不一样的

			现传入第一行a.h的FileID，则结果将返回
			#include "./a.h"
		*/
		std::string get_raw_include_str(FileID file);

		// cur位置的代码使用src位置的代码
		void use(SourceLocation cur, SourceLocation src, const char* name = nullptr);

		void use_name(FileID file, FileID beusedFile, const char* name = nullptr);

		// 根据当前文件，查找第2层的祖先（令root为第一层），若当前文件即处于第2层，则返回当前文件
		FileID get_lvl_2_ancestor(FileID file, FileID root);

		// 获取该文件的与祖先兄弟同级的祖先
		FileID get_same_lvl_ancestor(FileID child, FileID ancestor_brother);

		// 文件2是否是文件1的祖先
		bool is_ancestor(FileID yound, FileID old) const;

		// 文件2是否是文件1的祖先
		bool is_ancestor(FileID yound, const char* old) const;

		// 文件2是否是文件1的祖先
		bool is_ancestor(const char* yound, FileID old) const;

		bool is_common_ancestor(const std::set<FileID> &children, FileID old);

		bool has_parent(FileID child);

		FileID get_parent(FileID child);

		void direct_use_include_up_by_up(FileID file, FileID beusedFile);

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
		void direct_use_include_by_family(FileID file, FileID beusedFile);

		// 当前文件使用目标文件
		void use_include(FileID file, FileID beusedFile, const char* name = nullptr);

		void use_macro(SourceLocation loc, const MacroDefinition &macro, const Token &macroName);

		// 当前位置使用目标类型
		void use_type(SourceLocation loc, const QualType &t);

		string get_cxxrecord_name(const CXXRecordDecl &cxxRecordDecl);

		bool LexSkipComment(SourceLocation Loc, Token &Result);

		// 返回插入前置声明所在行的开头
		SourceLocation get_insert_forward_line(FileID at, const CXXRecordDecl &cxxRecord);

		void use_forward(SourceLocation loc, const CXXRecordDecl *cxxRecordDecl);

		void use_var(SourceLocation loc, const QualType &var);

		void on_use_decl(SourceLocation loc, const NamedDecl *nameDecl);

		void on_use_record(SourceLocation loc, const CXXRecordDecl *record);

		void print_parents_by_id();

		string debug_file_include_text(FileID file, bool is_absolute_name = false);

		string debug_file_direct_use_text(FileID file);

		string debug_loc_text(SourceLocation loc);

		string debug_loc_include_text(SourceLocation loc);

		void print_use();

		void print_used_names();

		void print_root_cycle_used_names();

		void print_used_children();

		void print_all_file();

		void print_direct_used_include();

		void print_used_top_include();

		void print_top_include_by_id();

		static void print_unused_include_of_files(FileMap &files);

		static void print_can_forwarddecl_of_files(FileMap &files);

		static void print_can_replace_of_files(FileMap &files);

		void print_unused_include();

		void print_can_forwarddecl();

		void print_unused_top_include();

		const char* get_file_name(FileID file) const;

		static string get_absolute_file_name(const char *raw_file_name);

		string get_absolute_file_name(FileID file) const;

		inline bool is_blank(char c)
		{
			return (c == ' ' || c == '\t');
		}

		const char* get_include_value(const char* include_str);

		std::string get_relative_include_str(FileID f1, FileID f2);

		bool is_absolute_path(const string& path);

		// 简化路径
		// d:/a/b/c/../../d/ -> d:/d/
		static std::string simplify_path(const char* path);

		static string get_absolute_path(const char *path);

		static string get_absolute_path(const char *base_path, const char* relative_path);

		string get_parent_path(const char* path);

		// 根据头文件搜索路径，将绝对路径转换为双引号包围的文本串，
		// 例如：假设有头文件搜索路径"d:/a/b/c" 则"d:/a/b/c/d/e.h" -> "d/e.h"
		string get_quoted_include_str(const string& absoluteFilePath);

		// 删除指定位置所在的整行代码
		void erase_line_by_loc(SourceLocation loc);

		void clean_unused_include_by_loc(SourceLocation unused_loc);

		void clean_unused_include_in_all_file();

		void clean_unused_include_in_root();

		void clean_can_replace_in_root();

		void add_forward_declaration(FileID file);

		void clean_by_forwarddecl_in_all_file();

		void clean();

		void replace_text(FileID file, int beg, int end, string text);

		void insert_text(FileID file, int loc, string text);

		void remove_text(FileID file, int beg, int end);

		void clean_by_unused_line(const EachFile &eachFile, FileID file);

		void clean_by_forward(const EachFile &eachFile, FileID file);

		void clean_by_replace(const EachFile &eachFile, FileID file);

		void clean_by(const FileMap &files);

		void clean_all_file();

		void clean_main_file();

		void print_can_replace();

		void print_header_search_path();

		void print_relative_include();

		void print_range_to_filename();

		// 用于调试跟踪
		void print_not_found_include_loc();

		// 用于调试跟踪
		void print_same_line();

		void print();

		bool init_header_search_path(clang::SourceManager* srcMgr, clang::HeaderSearch &header_search);

		// 文件格式是否是windows格式，换行符为[\r\n]，类Unix下为[\n]
		bool IsWindowsFormat(FileID);

		void merge_to(FileMap &old);

		void merge_unused_line_to(const EachFile &newFile, EachFile &oldFile) const;

		void merge_forward_line_to(const EachFile &newFile, EachFile &oldFile) const;

		void merge_replace_line_to(const EachFile &newFile, EachFile &oldFile) const;

		void merge_count_to(FileMap &oldFiles) const;

		void take_all_info_to(FileMap &out);

		// 将可清除的行按文件进行存放
		void take_unused_line_by_file(FileMap &out);

		// 将新增的前置声明按文件进行存放
		void take_new_forwarddecl_by_file(FileMap &out);

		bool is_force_included(FileID file);

		typedef std::map<FileID, std::set<FileID>> ChildrenReplaceMap;
		typedef std::map<FileID, ChildrenReplaceMap> ReplaceFileMap;
		void split_replace_by_file(ReplaceFileMap &replaces);

		void take_be_replace_of_file(EachFile &eachFile, FileID top, const ChildrenReplaceMap &childernReplaces);

		// 取出各文件的#include替换信息
		void take_replace_by_file(FileMap &out);

		bool is_cycly_used(FileID file);

		bool is_replaced(FileID file);

	public:
		// 循环引用列表
		std::map<FileID, std::set<FileID>>			m_uses;					// 1.  use列表
		std::set<FileID>							m_rootCycleUse;			// 1.  主文件循环引用的文件列表
		std::set<FileID>							m_allCycleUse;			// 1.  主文件循环引用的文件列表
		std::map<FileID, std::set<FileID>>			m_cycleUsedChildren;	// 1.  主文件循环引用全部有用的孩子

		//
		std::map<FileID, std::set<FileID>>			m_direct_uses;			// 2.  直接use列表，如a.cpp使用所#include的b.h中#include的c.h，则a.cpp直接使用了b.h
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
		std::map<FileID, std::set<const CXXRecordDecl*>>	m_forwardDecls;			// 15. 文件 -> 所使用的前置声明
		// std::map<FileID, std::set<CXXRecordDecl*>>	m_f;
		std::map<FileID, std::vector<UseNameInfo>>	m_useNames;					// 1.  use列表

		Rewriter*								m_rewriter;
		SourceManager*							m_srcMgr;

		static bool								m_isOverWriteOption;	// 是否覆盖原来的c++文件
		int										m_i;					// 当前打印索引，仅用于日志调试
	};
}

using namespace cxxcleantool;

#endif // _parsing_cpp_h_