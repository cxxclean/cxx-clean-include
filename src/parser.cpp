//------------------------------------------------------------------------------
// 文件: parser.cpp
// 作者: 洪坤安
// 说明: 解析当前cpp文件
// Copyright (c) 2016 game. All rights reserved.
//------------------------------------------------------------------------------

#include "parser.h"

#include <sstream>
#include <fstream>

// 下面3个#include是_chmod函数需要用的
#include <io.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <clang/AST/DeclTemplate.h>
#include <clang/Lex/HeaderSearch.h>
#include <clang/Lex/MacroInfo.h>
#include <clang/Lex/Lexer.h>
#include <clang/Lex/Preprocessor.h>
#include <clang/Rewrite/Core/Rewriter.h>
#include "clang/Frontend/CompilerInstance.h"

#include "tool.h"
#include "project.h"
#include "html_log.h"

ParsingFile* ParsingFile::g_atFile = nullptr;

namespace cxxclean
{
	ParsingFile::ParsingFile(clang::Rewriter &rewriter, clang::CompilerInstance &compiler)
	{
		m_rewriter	= &rewriter;
		m_compiler	= &compiler;
		m_srcMgr	= &compiler.getSourceManager();
		m_printIdx	= 0;
		g_atFile	= this;

		clang::HeaderSearch &headerSearch = m_compiler->getPreprocessor().getHeaderSearchInfo();
		m_headerSearchPaths = TakeHeaderSearchPaths(headerSearch);
	}

	ParsingFile::~ParsingFile()
	{
		g_atFile = nullptr;
	}

	// 添加#include的位置记录
	void ParsingFile::AddIncludeLoc(SourceLocation loc, SourceRange range)
	{
		SourceRange spellingRange(GetExpasionLoc(range.getBegin()), GetExpasionLoc(range.getEnd()));
		m_includeLocs[GetExpasionLoc(loc)] = range;
	}

	// 添加成员文件
	void ParsingFile::AddFile(FileID file)
	{
		if (file.isValid())
		{
			m_files.insert(file);
			m_sameFiles[GetAbsoluteFileName(file)].insert(file);
		}
	}

	// 获取头文件搜索路径
	vector<ParsingFile::HeaderSearchDir> ParsingFile::TakeHeaderSearchPaths(clang::HeaderSearch &headerSearch) const
	{
		IncludeDirMap dirs;

		// 获取系统头文件搜索路径
		for (clang::HeaderSearch::search_dir_iterator
		        itr = headerSearch.system_dir_begin(), end = headerSearch.system_dir_end();
		        itr != end; ++itr)
		{
			if (const DirectoryEntry* entry = itr->getDir())
			{
				const string path = pathtool::fix_path(entry->getName());
				dirs.insert(make_pair(path, SrcMgr::C_System));
			}
		}

		// 获取用户头文件搜索路径
		for (clang::HeaderSearch::search_dir_iterator
		        itr = headerSearch.search_dir_begin(), end = headerSearch.search_dir_end();
		        itr != end; ++itr)
		{
			if (const DirectoryEntry* entry = itr->getDir())
			{
				const string path = pathtool::fix_path(entry->getName());
				dirs.insert(make_pair(path, SrcMgr::C_User));
			}
		}

		return SortHeaderSearchPath(dirs);
	}

	// 根据长度由长到短排列
	static bool SortByLongestLengthFirst(const ParsingFile::HeaderSearchDir& left, const ParsingFile::HeaderSearchDir& right)
	{
		return left.m_dir.length() > right.m_dir.length();
	}

	// 将头文件搜索路径根据长度由长到短排列
	std::vector<ParsingFile::HeaderSearchDir> ParsingFile::SortHeaderSearchPath(const IncludeDirMap& include_dirs_map) const
	{
		std::vector<HeaderSearchDir> dirs;

		for (auto &itr : include_dirs_map)
		{
			const string &path					= itr.first;
			SrcMgr::CharacteristicKind pathType	= itr.second;

			string absolutePath = pathtool::get_absolute_path(path.c_str());

			dirs.push_back(HeaderSearchDir(absolutePath, pathType));
		}

		sort(dirs.begin(), dirs.end(), &SortByLongestLengthFirst);
		return dirs;
	}

	// 根据头文件搜索路径，将绝对路径转换为双引号包围的文本串，
	// 例如：假设有头文件搜索路径"d:/a/b/c" 则"d:/a/b/c/d/e.h" -> "d/e.h"
	string ParsingFile::GetQuotedIncludeStr(const string& absoluteFilePath) const
	{
		string path = pathtool::simplify_path(absoluteFilePath.c_str());

		for (const HeaderSearchDir &itr :m_headerSearchPaths)
		{
			if (strtool::try_strip_left(path, itr.m_dir))
			{
				if (itr.m_dirType == SrcMgr::C_System)
				{
					return "<" + path + ">";
				}
				else
				{
					return "\"" + path + "\"";
				}
			}
		}

		return "";
	}

	// 指定位置的对应的#include是否被用到
	inline bool ParsingFile::IsLocBeUsed(SourceLocation loc) const
	{
		return m_usedLocs.find(loc) != m_usedLocs.end();
	}

	// 该文件是否被包含多次
	inline bool ParsingFile::HasSameFileByName(const char *file) const
	{
		return m_sameFiles.find(file) != m_sameFiles.end();
	}

	// 该文件名是否被包含多次
	inline bool ParsingFile::HasSameFile(FileID file) const
	{
		return m_sameFiles.find(GetAbsoluteFileName(file)) != m_sameFiles.end();
	}

	// 为当前cpp文件的清理作前期准备
	void ParsingFile::InitCpp()
	{
		// 1. 生成每个文件的后代文件集（分析过程中需要用到）
		for (FileID file : m_files)
		{
			for (FileID child = file, parent; (parent = GetParent(child)).isValid(); child = parent)
			{
				m_children[parent].insert(file);
			}
		}

		// 2. 保留被反复包含的文件
		for (auto itr = m_sameFiles.begin(); itr != m_sameFiles.end();)
		{
			const std::set<FileID> &sameFiles = itr->second;
			if (sameFiles.size() <= 1)
			{
				m_sameFiles.erase(itr++);
			}
			else
			{
				++itr;
			}
		}
	}

	// 开始分析
	void ParsingFile::Analyze()
	{
		if (Project::IsCleanModeOpen(CleanMode_Need))
		{
			GenerateForceIncludes();
			GenerateOutFileAncestor();
			GenerateKidBySame();
			GenerateUserUse();
			GenerateMinUse();
			GenerateMinForwardClass();
		}
		else
		{
			GenerateRely();

			GenerateUnusedInclude();
			GenerateForwardClass();
			GenerateReplace();

			Fix();
		}

		TakeHistorys(m_historys);
		MergeTo(ProjectHistory::instance.m_files);
	}

	bool ParsingFile::ReplaceMin(FileID a, FileID b)
	{
		for (auto &itr : m_min)
		{
			FileID top = itr.first;
			FileSet &kids = itr.second;

			if (top == a || top == b)
			{
				continue;
			}

			if (IsAncestor(top, b))
			{
				continue;
			}

			if (kids.find(a) != kids.end())
			{
				LogInfoByLvl(LogLvl_3, "a = " << GetDebugFileName(a) << ", b = " << GetDebugFileName(b) << " at file = " << GetDebugFileName(top) << ")");

				kids.erase(a);
				kids.insert(b);
			}
		}

		return true;
	}

	inline bool ParsingFile::IsMinUse(FileID a, FileID b) const
	{
		auto &itr = m_min.find(a);
		if (itr == m_min.end())
		{
			return false;
		}

		auto &useList = itr->second;
		return useList.find(b) != useList.end();
	}

	inline bool ParsingFile::HasAnyInclude(FileID a) const
	{
		return m_includes.find(a) != m_includes.end();
	}

	bool ParsingFile::ExpandMin()
	{
		for (auto &itr : m_min)
		{
			FileID top		= itr.first;
			FileSet &kids	= itr.second;

			if (IsAncestorForceInclude(top))
			{
				LogInfoByLvl(LogLvl_3, "IsAncestorForceInclude(" << GetDebugFileName(top) << ")");

				m_min.erase(top);
				return true;
			}

			for (FileID kid : kids)
			{
				if (kid == top)
				{
					kids.erase(kid);
					return true;
				}

				FileID ancestorForceInclude = GetAncestorForceInclude(kid);
				if (ancestorForceInclude.isValid() && ancestorForceInclude != kid)
				{
					LogInfoByLvl(LogLvl_3, "GetAncestorForceInclude(" << GetDebugFileName(kid) << ") = " << GetDebugFileName(ancestorForceInclude) << ")");

					kids.erase(kid);
					kids.insert(ancestorForceInclude);

					return true;
				}

				if (IsAncestorBySame(kid, top))
				{
					continue;
				}
				else if (IsAncestorBySame(top, kid))
				{
					LogInfoByLvl(LogLvl_3, "IsAncestor(kid = " << GetDebugFileName(top) << ", ancestor = " << GetDebugFileName(kid) << ")");

					m_min[kid].insert(top);
					kids.erase(kid);

					ReplaceMin(top, kid);
					return true;
				}
				else
				{
					FileID ancestor = GetCommonAncestor(top, kid);
					kids.erase(kid);
					kids.insert(ancestor);

					LogInfoByLvl(LogLvl_3, "GetCommonAncestor(" << GetDebugFileName(top) << ", " << GetDebugFileName(kid) << ") = " << GetAbsoluteFileName(ancestor));

					m_min[ancestor].insert(kid);
					m_min[ancestor].insert(top);

					ReplaceMin(kid, ancestor);
					ReplaceMin(top, ancestor);
					return true;
				}
			}
		}

		return false;
	}

	bool ParsingFile::MergeMin()
	{
		bool any = false;

		// 合并
		for (auto &itr : m_min)
		{
			FileID top			= itr.first;
			const FileSet &kids	= itr.second;

			FileSet minKids = kids;

			for (FileID kid : kids)
			{
				int deep1 = GetDeepth(kid);

				for (FileID forceInclude : m_forceIncludes)
				{
					FileID same = GetBestSameFile(forceInclude, kid);
					if (same != kid)
					{
						LogInfoByLvl(LogLvl_3, "GetBestSameFile(forceInclude = " << GetDebugFileName(forceInclude) << ", kid = " << GetDebugFileName(kid) << ")");

						minKids.erase(kid);
						break;
					}
				}

				for (FileID other : minKids)
				{
					if (kid == other)
					{
						continue;
					}

					if (HasMinKidBySameName(kid, other))
					{
						LogInfoByLvl(LogLvl_3, "HasMinKidBySameName(top = " << GetDebugFileName(kid) << ", kid = " << GetDebugFileName(other) << ")");

						minKids.erase(other);
						break;
					}
				}
			}

			if (minKids.size() < kids.size())
			{
				itr.second = minKids;
				any = true;
			}
		}

		return any;
	}

	inline bool ParsingFile::IsUserFile(FileID file) const
	{
		if (file.isInvalid())
		{
			return false;
		}

		if (IsAncestorForceInclude(file))
		{
			return false;
		}

		return CanClean(file);
	}

	inline bool ParsingFile::IsOuterFile(FileID file) const
	{
		if (file.isInvalid())
		{
			return false;
		}

		return !IsUserFile(file);
	}

	inline FileID ParsingFile::GetTopOuterFileAncestor(FileID file) const
	{
		auto itr = m_outFileAncestor.find(file);
		if (itr == m_outFileAncestor.end())
		{
			return file;
		}

		return itr->second;
	}

	inline FileID ParsingFile::SearchOuterFileAncestor(FileID file) const
	{
		FileID topSysAncestor = file;

		for (FileID parent = file; IsOuterFile(parent); parent = GetParent(parent))
		{
			topSysAncestor = parent;
		}

		return topSysAncestor;
	}

	void ParsingFile::GenerateKidBySame()
	{
		for (auto &itr : m_children)
		{
			FileID top = itr.first;
			const FileSet &children	= itr.second;

			if (IsOuterFile(top))
			{
				continue;
			}

			FileSet &userChildren	= m_userChildren[top];

			for (FileID child : children)
			{
				userChildren.insert(GetTopOuterFileAncestor(child));
			}
		}

		for (auto &itr : m_userChildren)
		{
			FileID top = itr.first;

			std::set<FileID> &kids	= m_childrenBySame[top];
			GetKidsBySame(top, kids);
		}
	}

	void ParsingFile::GenerateForceIncludes()
	{
		for (FileID file : m_files)
		{
			if (IsForceIncluded(file))
			{
				m_forceIncludes.insert(file);
			}
		}
	}

	void ParsingFile::GenerateOutFileAncestor()
	{
		for (FileID file : m_files)
		{
			FileID outerFileAncestor;

			for (FileID parent = file; IsOuterFile(parent); parent = GetParent(parent))
			{
				outerFileAncestor = parent;
			}

			if (outerFileAncestor.isValid() && outerFileAncestor != file)
			{
				m_outFileAncestor[file] = outerFileAncestor;
			}
		}
	}

	void ParsingFile::GenerateUserUse()
	{
		for (auto &itr : m_uses)
		{
			FileID by				= itr.first;
			const FileSet &useList	= itr.second;

			if (IsOuterFile(by))
			{
				continue;
			}

			FileSet &userUseList	= m_userUses[by];

			for (FileID beUse : useList)
			{
				userUseList.insert(GetTopOuterFileAncestor(beUse));
			}
		}
	}

	void ParsingFile::GenerateMinUse()
	{
		m_min = m_userUses;

		while (ExpandMin()) {}

		for (auto &itr : m_min)
		{
			FileID by				= itr.first;

			std::set<FileID> &kids	= m_minKids[by];
			GetMin(by, kids);
		}

		// 2. 合并
		while (MergeMin()) {}
		//MergeMin();

		// 3. 统计出哪些#include被跳过
		auto includeLocs = m_includeLocs;
		for (FileID file : m_files)
		{
			SourceLocation includeLoc = m_srcMgr->getIncludeLoc(file);
			includeLocs.erase(includeLoc);
		}

		for (auto &itr : includeLocs)
		{
			SourceLocation loc = itr.first;
			FileID file = GetFileID(loc);
			m_skipIncludeLocs[file].insert(loc);
		}
	}

	void ParsingFile::GetKidsBySame(FileID top, std::set<FileID> &kids) const
	{
		FileSet done;
		FileSet todo;

		todo.insert(top);

		while (!todo.empty())
		{
			FileID cur = *todo.begin();
			todo.erase(todo.begin());

			if (done.find(cur) != done.end())
			{
				continue;
			}

			done.insert(cur);

			FileSet sames;

			auto sameItr = m_sameFiles.find(GetAbsoluteFileName(cur));
			if (sameItr != m_sameFiles.end())
			{
				sames = sameItr->second;
			}
			else
			{
				sames.insert(cur);
			}

			for (FileID same : sames)
			{
				todo.insert(same);

				auto childrenItr = m_userChildren.find(same);
				if (childrenItr == m_userChildren.end())
				{
					continue;
				}

				const FileSet &children = childrenItr->second;
				todo.insert(children.begin(), children.end());
			}
		}

		auto sameItr = m_sameFiles.find(GetAbsoluteFileName(top));
		if (sameItr != m_sameFiles.end())
		{
			const FileSet &sames = sameItr->second;
			for (FileID same : sames)
			{
				done.erase(same);
			}
		}
		else
		{
			done.erase(top);
		}

		kids.insert(done.begin(), done.end());
	}

	void ParsingFile::GetMin(FileID top, std::set<FileID> &kids) const
	{
		// 查找top文件的引用记录
		auto &topUseItr = m_min.find(top);
		if (topUseItr == m_min.end())
		{
			return;
		}

		std::set<FileID> todo;
		std::set<FileID> done;

		// 获取top文件所依赖的文件集
		const std::set<FileID> &topUseFiles = topUseItr->second;
		todo.insert(topUseFiles.begin(), topUseFiles.end());

		//------------------------------- 循环获取被依赖文件所依赖的其他文件 -------------------------------//
		while (!todo.empty())
		{
			FileID cur = *todo.begin();
			todo.erase(todo.begin());

			if (done.find(cur) != done.end())
			{
				continue;
			}

			done.insert(cur);

			// 1. 若当前文件不依赖其他文件，则跳过
			auto & useItr = m_min.find(cur);
			if (useItr == m_min.end())
			{
				continue;
			}

			// 只扩展后代文件
			if (!IsAncestor(cur, top))
			{
				continue;
			}

			// 2. todo集合 += 当前文件依赖的其他文件
			const std::set<FileID> &useFiles = useItr->second;

			for (const FileID &beuse : useFiles)
			{
				if (done.find(beuse) != done.end())
				{
					continue;
				}

				todo.insert(beuse);
			}
		}

		done.erase(top);
		kids.insert(done.begin(), done.end());
	}

	bool ParsingFile::HasMinKid(FileID top, FileID kid) const
	{
		// 查找top文件的引用记录
		auto &kidItr = m_minKids.find(top);
		if (kidItr == m_minKids.end())
		{
			return false;
		}

		const FileSet &kids = kidItr->second;
		return kids.find(kid) != kids.end();
	}

	int ParsingFile::GetDeepth(FileID file) const
	{
		int deepth = 0;

		for (FileID parent; (parent = GetParent(file)).isValid(); file = parent)
		{
			++deepth;
		}

		return deepth;
	}

	// 记录各文件的被依赖后代文件
	void ParsingFile::GenerateRelyChildren()
	{
		for (FileID usedFile : m_relys)
		{
			for (FileID child = usedFile, parent; (parent = GetParent(child)).isValid(); child = parent)
			{
				m_relyChildren[parent].insert(usedFile);
			}
		}
	}

	// 获取该文件可被替换到的文件，若无法被替换，则返回空文件id
	FileID ParsingFile::GetCanReplaceTo(FileID top) const
	{
		// 若文件本身也被使用到，则无法被替换
		if (IsRely(top))
		{
			return FileID();
		}

		auto &itr = m_relyChildren.find(top);
		if (itr == m_relyChildren.end())
		{
			return FileID();
		}

		const std::set<FileID> &children = itr->second;		// 有用的后代文件

		// 1. 无有用的后代文件 -> 直接跳过（正常情况不会发生）
		if (children.empty())
		{
			return FileID();
		}
		// 2. 仅有一个有用的后代文件 -> 当前文件可被该后代替换
		else if (children.size() == 1)
		{
			return *(children.begin());
		}
		// 3. 有多个有用的后代文件（最普遍的情况） -> 可替换为后代文件的共同祖先
		else
		{
			// 获取所有后代文件的最近共同祖先
			FileID ancestor = GetCommonAncestor(children);
			if (ancestor == top)
			{
				return FileID();
			}

			// 若后代文件们的最近共同祖先仍旧是当前祖先的后代，则替换为后代文件们的共同祖先
			if (IsAncestor(ancestor, top))
			{
				return ancestor;
			}
		}

		return FileID();
	}

	// 尝试添加各个被依赖文件的祖先文件，返回值：true表示依赖文件集被扩张、false表示依赖文件集不变
	bool ParsingFile::TryAddAncestor()
	{
		FileID mainFile = m_srcMgr->getMainFileID();

		for (auto &itr = m_relys.rbegin(); itr != m_relys.rend(); ++itr)
		{
			FileID file = *itr;

			for (FileID top = mainFile, lv2Top; top != file; top = lv2Top)
			{
				lv2Top = GetLvl2Ancestor(file, top);

				if (lv2Top == file)
				{
					break;
				}

				if (IsRely(lv2Top))
				{
					continue;
				}

				if (IsReplaced(lv2Top))
				{
					FileID oldReplaceTo = m_replaces[lv2Top];
					FileID canReplaceTo = GetCanReplaceTo(lv2Top);

					if (canReplaceTo != oldReplaceTo)
					{
						m_replaces.erase(lv2Top);
						return true;
					}

					break;
				}

				FileID canReplaceTo;

				// 仅[替换#include]清理选项开启时，才尝试替换
				if (Project::IsCleanModeOpen(CleanMode_Replace))
				{
					canReplaceTo = GetCanReplaceTo(lv2Top);
				}

				bool expand = false;

				// 若不可被替换
				if (canReplaceTo.isInvalid())
				{
					expand = ExpandRely(lv2Top);
				}
				// 若可被替换
				else
				{
					expand = ExpandRely(canReplaceTo);
					if (!expand)
					{
						m_replaces[lv2Top] = canReplaceTo;
					}

					expand = true;
				}

				if (expand)
				{
					return true;
				}
			}
		}

		return false;
	}

	// 根据主文件的依赖关系，生成相关文件的依赖文件集
	void ParsingFile::GenerateRely()
	{
		/*
			下面这段代码是本工具的主要处理思路
		*/

		// 1. 获取主文件的依赖文件集
		FileID top = m_srcMgr->getMainFileID();
		GetTopRelys(top, m_topRelys);

		m_relys = m_topRelys;

		// 2. 记录各文件的后代文件中被依赖的部分
		GenerateRelyChildren();

		// 3. 尝试添加各个被依赖文件的祖先文件
		while (TryAddAncestor())
		{
			// 4. 若依赖文件集被扩张，则重新生成后代依赖文件集
			GenerateRelyChildren();
		};

		// 5. 从被使用文件的父节点，层层往上建立引用父子引用关系
		for (FileID beusedFile : m_relys)
		{
			for (FileID child = beusedFile, parent; (parent = GetParent(child)).isValid(); child = parent)
			{
				m_usedLocs.insert(m_srcMgr->getIncludeLoc(child));
			}
		}
	}

	bool ParsingFile::ExpandRely(FileID top)
	{
		std::set<FileID> topRelys;
		GetTopRelys(top, topRelys);

		int oldSize = m_relys.size();

		m_relys.insert(topRelys.begin(), topRelys.end());

		int newSize = m_relys.size();

		return newSize > oldSize;
	}

	// 是否禁止改动某文件
	bool ParsingFile::IsSkip(FileID file) const
	{
		return IsForceIncluded(file) || IsPrecompileHeader(file);
	}

	// 该文件是否被依赖
	bool ParsingFile::IsRely(FileID file) const
	{
		return m_relys.find(file) != m_relys.end();
	}

	// 该文件的所有同名文件是否被依赖（同一文件可被包含多次）
	bool ParsingFile::IsRelyBySameName(FileID file) const
	{
		if (IsRely(file))
		{
			return true;
		}

		auto itr = m_sameFiles.find(GetAbsoluteFileName(file));
		if (itr == m_sameFiles.end())
		{
			return false;
		}

		const std::set<FileID> &sames = itr->second;
		for (FileID same : sames)
		{
			if (file == same)
			{
				continue;
			}

			if (IsRely(same))
			{
				return true;
			}
		}

		return false;
	}

	// 该文件的所有同名文件是否被依赖（同一文件可被包含多次）
	bool ParsingFile::HasMinKidBySameName(FileID top, FileID kid) const
	{
		if (HasMinKid(top, kid))
		{
			return true;
		}

		auto itr = m_sameFiles.find(GetAbsoluteFileName(kid));
		if (itr == m_sameFiles.end())
		{
			return false;
		}

		const std::set<FileID> &sames = itr->second;
		for (FileID same : sames)
		{
			if (kid == same)
			{
				continue;
			}

			if (HasMinKid(top, same))
			{
				return true;
			}
		}

		return false;
	}

	// 该文件是否被主文件循环引用到
	bool ParsingFile::IsRelyByTop(FileID file) const
	{
		return m_topRelys.find(file) != m_topRelys.end();
	}

	// 该文件是否可被替换
	bool ParsingFile::IsReplaced(FileID file) const
	{
		return m_replaces.find(file) != m_replaces.end();
	}

	// a文件是否在b文件之前
	bool ParsingFile::IsFileBeforeLoc(FileID a, SourceLocation b) const
	{
		SourceLocation aBeg = m_srcMgr->getLocForStartOfFile(a);
		return m_srcMgr->isBeforeInTranslationUnit(aBeg, b);
	}

	// 祖先文件是否被强制包含
	bool ParsingFile::IsAncestorForceInclude(FileID file) const
	{
		return GetAncestorForceInclude(file).isValid();
	}

	// 获取被强制包含祖先文件
	FileID ParsingFile::GetAncestorForceInclude(FileID file) const
	{
		for (FileID parent = file; parent.isValid(); parent = GetParent(parent))
		{
			if (IsForceIncluded(parent))
			{
				return parent;
			}
		}

		return FileID();
	}

	// 生成无用#include的记录
	void ParsingFile::GenerateUnusedInclude()
	{
		// 仅[删除无用#include]清理选项开启时，才允许继续
		if (!Project::IsCleanModeOpen(CleanMode_Unused))
		{
			return;
		}

		// 1. 先生成有效#include列表
		std::map<SourceLocation, SourceRange> validIncludeLocs;
		for (FileID file : m_files)
		{
			SourceLocation beIncludeLoc = m_srcMgr->getIncludeLoc(file);

			auto itr = m_includeLocs.find(beIncludeLoc);
			if (itr != m_includeLocs.end())
			{
				validIncludeLocs.insert(std::make_pair(beIncludeLoc, itr->second));
			}
		}

		// 2. 判断每个#include是否被使用到
		for (auto & locItr : validIncludeLocs)
		{
			SourceLocation loc = locItr.first;
			FileID file = GetFileID(loc);

			if (!IsRely(file))
			{
				continue;
			}

			if (!IsLocBeUsed(loc))
			{
				m_unusedLocs.insert(loc);
			}
		}

		// 3. 有些#include不应被删除或无法被删除，如强制包含、预编译头文件
		for (FileID beusedFile : m_files)
		{
			if (IsSkip(beusedFile))
			{
				m_unusedLocs.erase(m_srcMgr->getIncludeLoc(beusedFile));
			}
		}
	}

	// 文件a是否使用到文件b
	bool ParsingFile::IsUse(FileID a, FileID b) const
	{
		auto &itr = m_uses.find(a);
		if (itr == m_uses.end())
		{
			return false;
		}

		auto &useList = itr->second;
		return useList.find(b) != useList.end();
	}

	// 文件a是否直接包含文件b
	bool ParsingFile::IsInclude(FileID a, FileID b) const
	{
		auto &itr = m_includes.find(a);
		if (itr == m_includes.end())
		{
			return false;
		}

		auto &includeList = itr->second;
		return includeList.find(b) != includeList.end();
	}

	// 从指定的文件列表中找到属于传入文件的后代
	std::set<FileID> ParsingFile::GetChildren(FileID ancestor, std::set<FileID> all_children/* 包括非ancestor孩子的文件 */)
	{
		// 属于ancestor的后代文件列表
		std::set<FileID> children;

		// 获取属于该文件的后代中被主文件使用的一部分
		for (FileID child : all_children)
		{
			if (!IsAncestor(child, ancestor))
			{
				continue;
			}

			children.insert(child);
		}

		return children;
	}

	void ParsingFile::GetTopRelys(FileID top, std::set<FileID> &out) const
	{
		// 查找主文件的引用记录
		auto & topUseItr = m_uses.find(top);
		if (topUseItr == m_uses.end())
		{
			out.insert(top);
			return;
		}

		std::set<FileID> todo;
		std::set<FileID> done;

		done.insert(top);

		// 获取主文件的依赖文件集
		const std::set<FileID> &topUseFiles = topUseItr->second;
		todo.insert(topUseFiles.begin(), topUseFiles.end());

		//------------------------------- 循环获取被依赖文件所依赖的其他文件 -------------------------------//
		while (!todo.empty())
		{
			FileID cur = *todo.begin();
			todo.erase(todo.begin());

			if (done.find(cur) != done.end())
			{
				continue;
			}

			done.insert(cur);

			// 1. 若当前文件不再依赖其他文件，则可以不再扩展当前文件
			auto & useItr = m_uses.find(cur);
			if (useItr == m_uses.end())
			{
				continue;
			}

			// 2. 将当前文件依赖的其他文件加入待处理集合中
			const std::set<FileID> &useFiles = useItr->second;

			for (const FileID &used : useFiles)
			{
				if (done.find(used) != done.end())
				{
					continue;
				}

				todo.insert(used);
			}
		}

		out.insert(done.begin(), done.end());
	}

	// 获取文件的深度（令主文件的深度为0）
	int ParsingFile::GetDepth(FileID child) const
	{
		int depth = 0;

		for (FileID parent; (parent = GetParent(child)).isValid(); child = parent)
		{
			++depth;
		}

		return depth;
	}

	// 获取离孩子们最近的共同祖先
	FileID ParsingFile::GetCommonAncestor(const std::set<FileID> &children) const
	{
		FileID highest_child;
		int min_depth = 0;

		for (const FileID &child : children)
		{
			int depth = GetDepth(child);

			if (min_depth == 0 || depth < min_depth)
			{
				highest_child = child;
				min_depth = depth;
			}
		}

		FileID ancestor = highest_child;
		while (!IsCommonAncestor(children, ancestor))
		{
			FileID parent = GetParent(ancestor);
			if (parent.isInvalid())
			{
				return m_srcMgr->getMainFileID();
			}

			ancestor = parent;
		}

		return ancestor;
	}

	// 获取2个孩子们最近的共同祖先
	FileID ParsingFile::GetCommonAncestor(FileID child_1, FileID child_2) const
	{
		if (child_1 == child_2)
		{
			return child_1;
		}

		int deepth_1	= GetDepth(child_1);
		int deepth_2	= GetDepth(child_2);

		FileID old		= (deepth_1 < deepth_2 ? child_1 : child_2);
		FileID young	= (old == child_1 ? child_2 : child_1);

		// 从较高层的文件往上查找父文件，直到该父文件也为另外一个文件的直系祖先为止
		while (!IsAncestor(young, old))
		{
			FileID parent = GetParent(old);
			if (parent.isInvalid())
			{
				break;
			}

			old = parent;
		}

		return old;
	}

	// 生成文件替换列表
	void ParsingFile::GenerateReplace()
	{
		for (auto &itr : m_replaces)
		{
			FileID from		= itr.first;
			FileID parent	= GetParent(from);

			if (parent.isValid())
			{
				m_splitReplaces[parent].insert(itr);
			}
		}
	}

	// 当前文件之前是否已有文件声明了该class、struct、union
	bool ParsingFile::HasRecordBefore(FileID cur, const CXXRecordDecl &cxxRecord) const
	{
		FileID recordAtFile = GetFileID(cxxRecord.getLocStart());

		// 若类所在的文件被引用，则不需要再加前置声明
		if (IsRely(recordAtFile))
		{
			return true;
		}

		// 否则，说明类所在的文件未被引用
		// 1. 此时，需查找类定义之前的所有文件中，是否已有该类的前置声明
		for (const CXXRecordDecl *prev = cxxRecord.getPreviousDecl(); prev; prev = prev->getPreviousDecl())
		{
			FileID prevFileId = GetFileID(prev->getLocation());
			if (!IsRely(prevFileId))
			{
				continue;
			}

			bool hasFind = (prevFileId <= cur);
			hasFind |= IsAncestor(prevFileId, cur);

			if (hasFind)
			{
				return true;
			}
		}

		// 2. 还需查找使用该类的文件前，是否有类的重定义
		for (CXXRecordDecl::redecl_iterator redeclItr = cxxRecord.redecls_begin(), end = cxxRecord.redecls_end(); redeclItr != end; ++redeclItr)
		{
			const TagDecl *redecl = *redeclItr;

			FileID redeclFileID = GetFileID(redecl->getLocation());
			if (!IsRely(redeclFileID))
			{
				continue;
			}

			bool hasFind = (redeclFileID <= cur);
			hasFind |= IsAncestor(redeclFileID, cur);

			if (hasFind)
			{
				return true;
			}
		}

		return false;
	}

	// 是否应保留当前位置引用的class、struct、union的前置声明
	bool ParsingFile::IsNeedClass(SourceLocation loc, const CXXRecordDecl &cxxRecord) const
	{
		// 使用前置声明的文件
		FileID a = GetFileID(loc);

		// 类所在的文件
		FileID b = GetFileID(cxxRecord.getLocStart());

		// 1. 若b未被引用，则肯定要加前置声明
		if (!IsRelyBySameName(b))
		{
			return true;
		}

		// 2. 若该位置前不存在b的被依赖的同名文件，则需要保留前置声明
		if (HasSameFile(b))
		{
			b = GetBestSameFile(a, b);

			bool findSameFileBefore = false;

			const std::set<FileID> &sames = m_sameFiles.find(GetAbsoluteFileName(b))->second;
			for (FileID same : sames)
			{
				if (IsFileBeforeLoc(same, loc))
				{
					if (IsRely(same))
					{
						findSameFileBefore = true;
						break;
					}
				}
			}

			if (!findSameFileBefore)
			{
				return true;
			}
		}

		return false;
	}

	// 是否应保留该位置引用的class、struct、union的前置声明
	bool ParsingFile::IsNeedMinClass(SourceLocation loc, const CXXRecordDecl &cxxRecord) const
	{
		// 使用前置声明的文件
		FileID useFile		= GetFileID(loc);

		// 类所在的文件
		FileID recordAtFile	= GetFileID(cxxRecord.getLocStart());
		recordAtFile = GetTopOuterFileAncestor(recordAtFile);

		// 1. 若b未被引用，则肯定要加前置声明
		if (HasMinKidBySameName(useFile, recordAtFile))
		{
			return false;
		}

		SourceLocation insertLoc = GetInsertForwardLine(useFile, cxxRecord);
		if (insertLoc.isInvalid())
		{
			return false;
		}

		FileID insertAtFile	= GetFileID(insertLoc);
		if (!CanClean(insertAtFile))
		{
			return false;
		}

		if (HasMinKidBySameName(insertAtFile, recordAtFile))
		{
			return false;
		}

		return true;
	}

	// 生成新增前置声明列表
	void ParsingFile::GenerateForwardClass()
	{
		// 1. 清除一些不必要保留的前置声明
		for (auto &itr = m_useRecords.begin(); itr != m_useRecords.end();)
		{
			SourceLocation loc								= itr->first;
			std::set<const CXXRecordDecl*> &old_forwards	= itr->second;

			FileID file = GetFileID(loc);

			if (!IsRely(file))
			{
				m_useRecords.erase(itr++);
				continue;
			}

			std::set<const CXXRecordDecl*> can_forwards;

			for (const CXXRecordDecl* cxxRecordDecl : old_forwards)
			{
				if (IsNeedClass(loc, *cxxRecordDecl))
				{
					can_forwards.insert(cxxRecordDecl);
				}
			}

			if (can_forwards.empty())
			{
				m_useRecords.erase(itr++);
				continue;
			}

			if (can_forwards.size() < old_forwards.size())
			{
				old_forwards = can_forwards;
			}

			++itr;
		}

		// 2. 不同文件可能添加了同一个前置声明，这里删掉重复的
	}

	// 生成新增前置声明列表
	void ParsingFile::GenerateMinForwardClass()
	{
		// 1. 清除一些不必要保留的前置声明
		for (auto &itr = m_useRecords.begin(); itr != m_useRecords.end();)
		{
			SourceLocation loc						= itr->first;
			std::set<const CXXRecordDecl*> &records	= itr->second;

			for (auto &recordItr = records.begin(); recordItr != records.end();)
			{
				const CXXRecordDecl* cxxRecordDecl = *recordItr;

				if (!IsNeedMinClass(loc, *cxxRecordDecl))
				{
					records.erase(recordItr++);
				}
				else
				{
					++recordItr;
				}
			}

			if (records.empty())
			{
				m_useRecords.erase(itr++);
				continue;
			}

			++itr;
		}
	}

	// 获取指定范围的文本
	std::string ParsingFile::GetSourceOfRange(SourceRange range) const
	{
		if (range.isInvalid())
		{
			return "";
		}

		range.setBegin(GetExpasionLoc(range.getBegin()));
		range.setEnd(GetExpasionLoc(range.getEnd()));

		if (range.getEnd() < range.getBegin())
		{
			LogError("if (range.getEnd() < range.getBegin())");
			return "";
		}

		if (!m_srcMgr->isWrittenInSameFile(range.getBegin(), range.getEnd()))
		{
			LogError("if (!m_srcMgr->isWrittenInSameFile(range.getBegin(), range.getEnd()))");
			return "";
		}

		const char* beg = GetSourceAtLoc(range.getBegin());
		const char* end = GetSourceAtLoc(range.getEnd());

		if (nullptr == beg || nullptr == end)
		{
			return "";
		}

		if (end < beg)
		{
			// 注意：这里如果不做判断遇到宏可能会崩，因为有可能末尾反而在起始字符前面，比如在宏之内
			return "";
		}

		return string(beg, end);
	}

	// 获取指定位置的文本
	const char* ParsingFile::GetSourceAtLoc(SourceLocation loc) const
	{
		bool err = true;

		const char* beg = m_srcMgr->getCharacterData(loc,	&err);
		if (err)
		{
			return nullptr;
		}

		return beg;
	}

	// 获取该范围源码的信息：文本、所在文件名、行号
	std::string ParsingFile::DebugRangeText(SourceRange range) const
	{
		string rangeText = GetSourceOfRange(range);
		std::stringstream text;
		text << "[" << htmltool::get_include_html(rangeText) << "] in [";
		text << htmltool::get_file_html(GetAbsoluteFileName(GetFileID(range.getBegin())));
		text << "] line = " << htmltool::get_number_html(GetLineNo(range.getBegin()));
		return text.str();
	}

	// 获取指定位置所在行的文本
	std::string ParsingFile::GetSourceOfLine(SourceLocation loc) const
	{
		return GetSourceOfRange(GetCurLine(loc));
	}

	SourceRange ParsingFile::GetCurLine(SourceLocation loc) const
	{
		SourceLocation fileBeginLoc = m_srcMgr->getLocForStartOfFile(GetFileID(loc));
		SourceLocation fileEndLoc	= m_srcMgr->getLocForEndOfFile(GetFileID(loc));

		const char* character	= GetSourceAtLoc(loc);
		const char* fileStart	= GetSourceAtLoc(fileBeginLoc);
		const char* fileEnd		= GetSourceAtLoc(fileEndLoc);

		if (nullptr == character || nullptr == fileStart || nullptr == fileEnd)
		{
			return SourceRange();
		}

		int left = 0;
		int right = 0;

		for (const char* c = character - 1; c >= fileStart	&& *c && *c != '\n' && *c != '\r'; --c, ++left) {}
		for (const char* c = character;		c < fileEnd		&& *c && *c != '\n' && *c != '\r'; ++c, ++right) {}

		SourceLocation lineBeg = loc.getLocWithOffset(-left);
		SourceLocation lineEnd = loc.getLocWithOffset(right);

		return SourceRange(lineBeg, lineEnd);
	}

	SourceRange ParsingFile::GetCurFullLine(SourceLocation loc) const
	{
		SourceRange curLine		= GetCurLine(loc);
		SourceRange nextLine	= GetNextLine(loc);

		return SourceRange(curLine.getBegin(), nextLine.getBegin());
	}

	// 根据传入的代码位置返回下一行的范围
	SourceRange ParsingFile::GetNextLine(SourceLocation loc) const
	{
		SourceRange curLine			= GetCurLine(loc);
		SourceLocation lineEnd		= curLine.getEnd();
		SourceLocation fileEndLoc	= m_srcMgr->getLocForEndOfFile(GetFileID(loc));

		if (m_srcMgr->isBeforeInTranslationUnit(fileEndLoc, lineEnd) || fileEndLoc == lineEnd)
		{
			return SourceRange(fileEndLoc, fileEndLoc);
		}

		const char* c1			= GetSourceAtLoc(lineEnd);
		const char* c2			= GetSourceAtLoc(lineEnd.getLocWithOffset(1));

		if (nullptr == c1 || nullptr == c2)
		{
			LogErrorByLvl(LogLvl_2, "GetNextLine = null");
			return SourceRange(fileEndLoc, fileEndLoc);
		}

		int skip = 0;

		// windows换行格式处理
		if (*c1 == '\r' && *c2 == '\n')
		{
			skip = 2;
		}
		// 类Unix换行格式处理
		else if (*c1 == '\n')
		{
			skip = 1;
		}

		SourceRange nextLine	= GetCurLine(lineEnd.getLocWithOffset(skip));
		return nextLine;
	}

	// 是否为空行
	bool ParsingFile::IsEmptyLine(SourceRange line)
	{
		string lineText = GetSourceOfRange(line);
		for (int i = 0, len = lineText.size(); i < len; ++i)
		{
			char c = lineText[i];

			if (c != '\n' && c != '\t' && c != ' ' && c != '\f' && c != '\v' && c != '\r')
			{
				return false;
			}
		}

		return true;
	}

	// 获取行号
	inline int ParsingFile::GetLineNo(SourceLocation loc) const
	{
		bool invalid = false;

		int line = m_srcMgr->getSpellingLineNumber(loc, &invalid);
		if (invalid)
		{
			line = m_srcMgr->getExpansionLineNumber(loc, &invalid);
		}

		return invalid ? 0 : line;
	}

	// 获取文件对应的被包含行号
	int ParsingFile::GetIncludeLineNo(FileID file) const
	{
		if (IsForceIncluded(file))
		{
			return 0;
		}

		return GetLineNo(m_srcMgr->getIncludeLoc(file));
	}

	// 获取文件对应的#include含范围
	SourceRange ParsingFile::GetIncludeRange(FileID file) const
	{
		SourceLocation include_loc	= m_srcMgr->getIncludeLoc(file);
		return GetCurFullLine(include_loc);
	}

	// 是否是换行符
	bool ParsingFile::IsNewLineWord(SourceLocation loc) const
	{
		string text = GetSourceOfRange(SourceRange(loc, loc.getLocWithOffset(1)));
		return text == "\r" || text == "";
	}

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
	std::string ParsingFile::GetIncludeLine(FileID file) const
	{
		SourceLocation loc = m_srcMgr->getIncludeLoc(file);
		return GetSourceOfLine(loc);
	}

	// 获取文件对应的#include所在的整行
	std::string ParsingFile::GetIncludeFullLine(FileID file) const
	{
		SourceLocation loc = m_srcMgr->getIncludeLoc(file);

		auto &itr = m_includeLocs.find(loc);
		if (itr == m_includeLocs.end())
		{
			return "";
		}

		return GetSourceOfLine(loc);
	}

	// cur位置的代码使用src位置的代码
	inline void ParsingFile::Use(SourceLocation cur, SourceLocation src, const char* name /* = nullptr */)
	{
		cur = GetExpasionLoc(cur);
		src = GetExpasionLoc(src);

		FileID curFileID = GetFileID(cur);
		FileID srcFileID = GetFileID(src);

		UseInclude(curFileID, srcFileID, name, GetLineNo(cur));
	}

	inline void ParsingFile::UseName(FileID file, FileID beusedFile, const char* name /* = nullptr */, int line)
	{
		if (Project::instance.m_logLvl < LogLvl_3)
		{
			return;
		}

		if (nullptr == name)
		{
			return;
		}

		// 找到本文件的使用名称历史记录，这些记录按文件名分类
		std::vector<UseNameInfo> &useNames = m_useNames[file];

		bool found = false;

		// 根据文件名找到对应文件的被使用名称历史，在其中新增使用记录
		for (UseNameInfo &info : useNames)
		{
			if (info.file == beusedFile)
			{
				found = true;
				info.AddName(name, line);
				break;
			}
		}

		if (!found)
		{
			UseNameInfo info;
			info.file = beusedFile;
			info.AddName(name, line);

			useNames.push_back(info);
		}
	}

	// 根据当前文件，查找第2层的祖先（令root为第一层），若当前文件的父文件即为主文件，则返回当前文件
	FileID ParsingFile::GetLvl2Ancestor(FileID file, FileID root) const
	{
		FileID ancestor;

		for (FileID parent; (parent = GetParent(file)).isValid(); file = parent)
		{
			// 要求父结点是root
			if (parent == root)
			{
				ancestor = file;
				break;
			}
		}

		return ancestor;
	}

	// 根据当前文件，查找第2层的祖先（令root为第一层），若当前文件的父文件即为主文件，则返回当前文件
	FileID ParsingFile::GetLvl2AncestorBySame(FileID kid, FileID top) const
	{
		auto itr = m_includes.find(top);
		if (itr == m_includes.end())
		{
			return FileID();
		}

		string kidFileName = GetAbsoluteFileName(kid);

		const FileSet &includes = itr->second;
		for (FileID beInclude : includes)
		{
			string beIncludeFileName = GetAbsoluteFileName(beInclude);
			if (beIncludeFileName == kidFileName)
			{
				return beInclude;
			}

			auto sameItr = m_sameFiles.find(beIncludeFileName);
			if (sameItr == m_sameFiles.end())
			{
				if (IsAncestorBySame(kid, beInclude))
				{
					return beInclude;
				}
			}
			else
			{
				const FileSet &sames = sameItr->second;
				for (FileID same : sames)
				{
					if (IsAncestorBySame(kid, same))
					{
						return beInclude;
					}
				}
			}
		}

		return FileID();
	}

	// 第2个文件是否是第1个文件的祖先
	bool ParsingFile::IsAncestor(FileID young, FileID old) const
	{
		for (FileID parent = GetParent(young); parent.isValid(); parent = GetParent(parent))
		{
			if (parent == old)
			{
				return true;
			}
		}

		return false;
	}

	// 第2个文件是否是第1个文件的祖先
	bool ParsingFile::IsAncestor(FileID young, const char* old) const
	{
		for (FileID parent = GetParent(young); parent.isValid(); parent = GetParent(parent))
		{
			if (GetAbsoluteFileName(parent) == old)
			{
				return true;
			}
		}

		return false;
	}

	// 第2个文件是否是第1个文件的祖先
	bool ParsingFile::IsAncestor(const char* young, FileID old) const
	{
		// 在父子关系表查找与后代文件同名的FileID（若多次#include同一文件，则会为该文件分配多个不同的FileID）
		for (auto &itr : m_parents)
		{
			FileID child = itr.first;

			if (GetAbsoluteFileName(child) == young)
			{
				if (IsAncestor(child, old))
				{
					return true;
				}
			}
		}

		return false;
	}

	// 第2个文件是否是第1个文件的祖先（考虑同名文件）
	bool ParsingFile::IsAncestorBySame(FileID young, FileID old) const
	{
		auto itr = m_childrenBySame.find(old);
		if (itr == m_childrenBySame.end())
		{
			return false;
		}

		const FileSet &kids = itr->second;
		return kids.find(young) != kids.end();
	}

	// 是否为孩子文件的共同祖先
	bool ParsingFile::IsCommonAncestor(const std::set<FileID> &children, FileID old) const
	{
		for (const FileID &child : children)
		{
			if (child == old)
			{
				continue;
			}

			if (!IsAncestor(child, old))
			{
				return false;
			}
		}

		return true;
	}

	// 获取父文件（主文件没有父文件）
	inline FileID ParsingFile::GetParent(FileID child) const
	{
		if (child == m_srcMgr->getMainFileID())
		{
			return FileID();
		}

		auto &itr = m_parents.find(child);
		if (itr == m_parents.end())
		{
			return FileID();
		}

		return itr->second;
	}

	// a文件使用b文件
	inline void ParsingFile::UseInclude(FileID a, FileID b, const char* name /* = nullptr */, int line)
	{
		if (a == b)
		{
			return;
		}

		if (a.isInvalid() || b.isInvalid())
		{
			return;
		}

		if (nullptr == m_srcMgr->getFileEntryForID(a) || nullptr == m_srcMgr->getFileEntryForID(b))
		{
			if (Project::instance.m_logLvl >= LogLvl_3)
			{
				// 这段注释保留
				//LogError("m_srcMgr->getFileEntryForID(a) failed!" << m_srcMgr->getFilename(m_srcMgr->getLocForStartOfFile(a)) << ":" << m_srcMgr->getFilename(m_srcMgr->getLocForStartOfFile(b)));
				//LogError("m_srcMgr->getFileEntryForID(b) failed!" << GetSourceOfLine(m_srcMgr->getLocForStartOfFile(a)) << ":" << GetSourceOfLine(m_srcMgr->getLocForStartOfFile(b)));
			}

			return;
		}

		std::string bFileName = GetAbsoluteFileName(b);

		if (GetAbsoluteFileName(a) == bFileName)
		{
			return;
		}

		b = GetBestSameFile(a, b);

		m_uses[a].insert(b);
		UseName(a, b, name, line);
	}

	// 当前位置使用指定的宏
	void ParsingFile::UseMacro(SourceLocation loc, const MacroDefinition &macro, const Token &macroNameTok, const MacroArgs *args /* = nullptr */)
	{
		MacroInfo *info = macro.getMacroInfo();
		if (nullptr == info)
		{
			return;
		}

		string macroName = macroNameTok.getIdentifierInfo()->getName().str() + "[macro]";
		Use(loc, info->getDefinitionLoc(), macroName.c_str());
	}

	void ParsingFile::UseContext(SourceLocation loc, const DeclContext *context)
	{
		while (context && context->isNamespace())
		{
			const NamespaceDecl *ns = cast<NamespaceDecl>(context);

			UseNamespaceDecl(loc, ns);
			context = context->getParent();
		}
	}

	// 引用嵌套名字修饰符
	void ParsingFile::UseQualifier(SourceLocation loc, const NestedNameSpecifier *specifier)
	{
		while (specifier)
		{
			NestedNameSpecifier::SpecifierKind kind = specifier->getKind();
			switch (kind)
			{
			case NestedNameSpecifier::Namespace:
				UseNamespaceDecl(loc, specifier->getAsNamespace());
				break;

			case NestedNameSpecifier::NamespaceAlias:
				UseNamespaceAliasDecl(loc, specifier->getAsNamespaceAlias());
				break;

			default:
				UseType(loc, specifier->getAsType());
				break;
			}

			specifier = specifier->getPrefix();
		}
	}

	// 引用命名空间声明
	void ParsingFile::UseNamespaceDecl(SourceLocation loc, const NamespaceDecl *ns)
	{
		UseNameDecl(loc, ns);

		for (auto itr : m_usingNamespaces)
		{
			SourceLocation usingLoc		= itr.first;
			const NamespaceDecl	*ns		= itr.second;

			if (m_srcMgr->isBeforeInTranslationUnit(usingLoc, loc))
			{
				if (ns->getQualifiedNameAsString() == ns->getQualifiedNameAsString())
				{
					Use(loc, usingLoc, GetNestedNamespace(ns).c_str());
				}
			}
		}
	}

	// 引用命名空间别名
	void ParsingFile::UseNamespaceAliasDecl(SourceLocation loc, const NamespaceAliasDecl *ns)
	{
		UseNameDecl(loc, ns);
		UseNamespaceDecl(ns->getAliasLoc(), ns->getNamespace());
	}

	// 声明了命名空间
	void ParsingFile::DeclareNamespace(const NamespaceDecl *d)
	{
		if (Project::instance.m_logLvl < LogLvl_3)
		{
			return;
		}

		SourceLocation loc = GetSpellingLoc(d->getLocation());

		FileID file = GetFileID(loc);
		if (file.isInvalid())
		{
			return;
		}

		m_namespaces[file].insert(d->getQualifiedNameAsString());
	}

	// using了命名空间，比如：using namespace std;
	void ParsingFile::UsingNamespace(const UsingDirectiveDecl *d)
	{
		const NamespaceDecl *nominatedNs = d->getNominatedNamespace();
		if (nullptr == nominatedNs)
		{
			return;
		}

		const NamespaceDecl *firstNs = nominatedNs->getOriginalNamespace();
		if (nullptr == firstNs)
		{
			return;
		}

		SourceLocation usingLoc = GetSpellingLoc(d->getUsingLoc());

		m_usingNamespaces[usingLoc] = firstNs;

		for (const NamespaceDecl *ns : firstNs->redecls())
		{
			SourceLocation nsLoc	= GetSpellingLoc(ns->getLocStart());

			if (m_srcMgr->isBeforeInTranslationUnit(nsLoc, usingLoc))
			{
				// 引用命名空间所在的文件（注意：using namespace时必须能找到对应的namespace声明，比如，using namespace A前一定要有namespace A{}否则编译会报错）
				Use(usingLoc, nsLoc, GetNestedNamespace(ns).c_str());
				break;
			}
		}
	}

	// using了命名空间下的某类，比如：using std::string;
	void ParsingFile::UsingXXX(const UsingDecl *d)
	{
		SourceLocation usingLoc		= d->getUsingLoc();

		for (auto &itr = d->shadow_begin(); itr != d->shadow_end(); ++itr)
		{
			UsingShadowDecl *shadowDecl = *itr;

			NamedDecl *nameDecl = shadowDecl->getTargetDecl();
			if (nullptr == nameDecl)
			{
				continue;
			}

			std::stringstream name;
			name << "using " << shadowDecl->getQualifiedNameAsString() << "[" << nameDecl->getDeclKindName() << "]";

			Use(usingLoc, nameDecl->getLocEnd(), name.str().c_str());

			// 注意：这里要反向引用，因为比如我们在a文件中#include <string>，然后在b文件using std::string，那么b文件也是有用的
			Use(nameDecl->getLocEnd(), usingLoc);
		}
	}

	// 获取命名空间的全部路径，例如，返回namespace A{ namespace B{ class C; }}
	std::string ParsingFile::GetNestedNamespace(const NamespaceDecl *d)
	{
		if (nullptr == d)
		{
			return "";
		}

		string name;

		while (d)
		{
			string namespaceName = "namespace " + d->getNameAsString();
			name = namespaceName + "{" + name + "}";

			const DeclContext *parent = d->getParent();
			if (parent && parent->isNamespace())
			{
				d = cast<NamespaceDecl>(parent);
				continue;
			}

			break;
		}

		return name;
	}

	// 文件b是否直接#include文件a
	bool ParsingFile::IsIncludedBy(FileID a, FileID b)
	{
		auto &itr = m_includes.find(b);
		if (itr == m_includes.end())
		{
			return false;
		}

		const std::set<FileID> &includes = itr->second;
		return includes.find(a) != includes.end();
	}

	// 获取文件a的指定名称的直接包含文件
	FileID ParsingFile::GetIncludeByName(FileID a, const char* includeName) const
	{
		auto &itr = m_includes.find(a);
		if (itr != m_includes.end())
		{
			for (FileID beInclude : itr->second)
			{
				if (GetAbsoluteFileName(beInclude) == includeName)
				{
					return beInclude;
				}
			}
		}

		return FileID();
	}

	// 获取文件a的指定名称的后代文件
	FileID ParsingFile::GetChildByName(FileID a, const char* childFileName) const
	{
		// 1. 优先返回直接孩子文件
		FileID directChild = GetIncludeByName(a, childFileName);
		if (directChild.isValid())
		{
			return directChild;
		}

		// 2. 否则，搜索后代文件
		auto &itr = m_children.find(a);
		if (itr == m_children.end())
		{
			for (FileID child : itr->second)
			{
				if (GetAbsoluteFileName(child) == childFileName)
				{
					return child;
				}
			}
		}

		return FileID();
	}

	// 当a使用b时，如果b对应的文件被包含多次，从b的同名文件中选取一个最好的文件
	FileID ParsingFile::GetBestSameFile(FileID a, FileID b) const
	{
		std::string bFileName = GetAbsoluteFileName(b);

		// 如果b文件被包含多次，则在其中选择一个较合适的（注意：每个文件优先保留自己及后代文件中的#include语句）
		if (HasSameFileByName(bFileName.c_str()))
		{
			// 先找到同名文件列表
			const std::set<FileID> &sames = m_sameFiles.find(bFileName)->second;

			// 1. 优先返回直接孩子文件
			auto &includeItr = m_includes.find(a);
			if (includeItr != m_includes.end())
			{
				const std::set<FileID> &includes = includeItr->second;

				for (FileID same :sames)
				{
					if (includes.find(same) != includes.end())
					{
						return same;
					}
				}
			}

			// 2. 否则，搜索后代文件
			auto &childrenItr = m_children.find(a);
			if (childrenItr != m_children.end())
			{
				const std::set<FileID> &children = childrenItr->second;

				for (FileID same :sames)
				{
					if (children.find(same) != children.end())
					{
						return same;
					}
				}
			}
		}

		return b;
	}

	// 当前位置使用目标类型（注：Type代表某个类型，但不含const、volatile、static等的修饰）
	void ParsingFile::UseType(SourceLocation loc, const Type *t)
	{
		if (nullptr == t)
		{
			return;
		}

		// 使用到typedef类型，比如：typedef int dword其中dword就是TypedefType
		if (isa<TypedefType>(t))
		{
			const TypedefType *typedefType = cast<TypedefType>(t);
			const TypedefNameDecl *typedefNameDecl = typedefType->getDecl();

			UseNameDecl(loc, typedefNameDecl);

			// 注：若该typedef的原型仍然是由其他的typedef声明而成，不需要继续分解
		}
		// 某个类型的代号，如：struct S或N::M::type
		else if (isa<ElaboratedType>(t))
		{
			const ElaboratedType *elaboratedType = cast<ElaboratedType>(t);
			UseQualType(loc, elaboratedType->getNamedType());

			// 嵌套名称修饰
			if (elaboratedType->getQualifier())
			{
				UseQualifier(loc, elaboratedType->getQualifier());
			}
		}
		else if (isa<TemplateSpecializationType>(t))
		{
			const TemplateSpecializationType *templateType = cast<TemplateSpecializationType>(t);
			UseTemplateDecl(loc, templateType->getTemplateName().getAsTemplateDecl());

			for (int i = 0, size = templateType->getNumArgs(); i < size; ++i)
			{
				const TemplateArgument &arg = templateType->getArg((unsigned)i);
				UseTemplateArgument(loc, arg);
			}
		}
		// 代表被取代后的模板类型参数
		else if (isa<SubstTemplateTypeParmType>(t))
		{
			const SubstTemplateTypeParmType *substTemplateTypeParmType = cast<SubstTemplateTypeParmType>(t);
			UseType(loc, substTemplateTypeParmType->getReplacedParameter());
		}
		else if (isa<ElaboratedType>(t))
		{
			const ElaboratedType *elaboratedType = cast<ElaboratedType>(t);
			UseQualType(loc, elaboratedType->getNamedType());
		}
		else if (isa<AttributedType>(t))
		{
			const AttributedType *attributedType = cast<AttributedType>(t);
			UseQualType(loc, attributedType->getModifiedType());
		}
		else if (isa<FunctionType>(t))
		{
			const FunctionType *functionType = cast<FunctionType>(t);

			// 识别返回值类型
			{
				// 函数的返回值
				QualType returnType = functionType->getReturnType();
				UseQualType(loc, returnType);
			}
		}
		else if (isa<MemberPointerType>(t))
		{
			const MemberPointerType *memberPointerType = cast<MemberPointerType>(t);
			UseQualType(loc, memberPointerType->getPointeeType());
		}
		// 模板参数类型，比如：template <typename T>里面的T
		else if (isa<TemplateTypeParmType>(t))
		{
			const TemplateTypeParmType *templateTypeParmType = cast<TemplateTypeParmType>(t);

			TemplateTypeParmDecl *decl = templateTypeParmType->getDecl();
			if (nullptr == decl)
			{
				return;
			}

			// 该模板参数的默认参数
			if (decl->hasDefaultArgument())
			{
				UseQualType(loc, decl->getDefaultArgument());
			}

			UseNameDecl(loc, decl);
		}
		else if (isa<ParenType>(t))
		{
			const ParenType *parenType = cast<ParenType>(t);
			UseQualType(loc, parenType->getInnerType());
		}
		else if (isa<InjectedClassNameType>(t))
		{
			const InjectedClassNameType *injectedClassNameType = cast<InjectedClassNameType>(t);
			UseQualType(loc, injectedClassNameType->getInjectedSpecializationType());
		}
		else if (isa<PackExpansionType>(t))
		{
			const PackExpansionType *packExpansionType = cast<PackExpansionType>(t);
			UseQualType(loc, packExpansionType->getPattern());
		}
		else if (isa<DecltypeType>(t))
		{
			const DecltypeType *decltypeType = cast<DecltypeType>(t);
			UseQualType(loc, decltypeType->getUnderlyingType());
		}
		else if (isa<DependentNameType>(t))
		{
		}
		else if (isa<DependentTemplateSpecializationType>(t))
		{
		}
		else if (isa<AutoType>(t))
		{
		}
		else if (isa<UnaryTransformType>(t))
		{
			// t->dump();
		}
		// 类、struct、union
		else if (isa<RecordType>(t))
		{
			const RecordType *recordType = cast<RecordType>(t);

			const RecordDecl *recordDecl = recordType->getDecl();
			if (nullptr == recordDecl)
			{
				return;
			}

			UseRecord(loc, recordDecl);
		}
		else if (t->isArrayType())
		{
			const ArrayType *arrayType = cast<ArrayType>(t);
			UseQualType(loc, arrayType->getElementType());
		}
		else if (t->isVectorType())
		{
			const VectorType *vectorType = cast<VectorType>(t);
			UseQualType(loc, vectorType->getElementType());
		}
		else if (t->isBuiltinType())
		{
		}
		else if (t->isPointerType() || t->isReferenceType())
		{
			QualType pointeeType = t->getPointeeType();

			// 如果是指针类型就获取其指向类型(PointeeType)
			while (pointeeType->isPointerType() || pointeeType->isReferenceType())
			{
				pointeeType = pointeeType->getPointeeType();
			}

			UseQualType(loc, pointeeType);
		}
		else if (t->isEnumeralType())
		{
			TagDecl *decl = t->getAsTagDecl();
			if (nullptr == decl)
			{
				return;
			}

			UseNameDecl(loc, decl);
		}
		else
		{
			// LogInfo(""-------------- haven't support type --------------");
			// t->dump();
		}
	}

	// 当前位置使用目标类型（注：QualType包含对某个类型的const、volatile、static等的修饰）
	void ParsingFile::UseQualType(SourceLocation loc, const QualType &t)
	{
		if (t.isNull())
		{
			return;
		}

		const Type *pType = t.getTypePtr();
		UseType(loc, pType);
	}

	// 获取c++中class、struct、union的全名，结果将包含命名空间
	// 例如：
	//     传入类C，C属于命名空间A中的命名空间B，则结果将返回：namespace A{ namespace B{ class C; }}
	string ParsingFile::GetRecordName(const RecordDecl &recordDecl) const
	{
		string name;
		name += recordDecl.getKindName();
		name += " " + recordDecl.getNameAsString();
		name += ";";

		bool inNameSpace = false;

		const DeclContext *curDeclContext = recordDecl.getDeclContext();
		while (curDeclContext && curDeclContext->isNamespace())
		{
			const NamespaceDecl *namespaceDecl = cast<NamespaceDecl>(curDeclContext);
			if (nullptr == namespaceDecl)
			{
				break;
			}

			inNameSpace = true;

			string namespaceName = "namespace " + namespaceDecl->getNameAsString();
			name = namespaceName + "{" + name + "}";

			curDeclContext = curDeclContext->getParent();
		}

		if (inNameSpace)
		{
			name += ";";
		}

		return name;
	}

	// 从指定位置起，跳过之后的注释，直到获得下一个token
	bool ParsingFile::LexSkipComment(SourceLocation Loc, Token &Result)
	{
		const SourceManager &SM = *m_srcMgr;
		const LangOptions LangOpts;

		Loc = SM.getExpansionLoc(Loc);
		std::pair<FileID, unsigned> LocInfo = SM.getDecomposedLoc(Loc);
		bool Invalid = false;
		StringRef Buffer = SM.getBufferData(LocInfo.first, &Invalid);
		if (Invalid)
		{
			cxx::log() << "LexSkipComment Invalid = true";
			return true;
		}

		const char *StrData = Buffer.data()+LocInfo.second;

		// Create a lexer starting at the beginning of this token.
		Lexer TheLexer(SM.getLocForStartOfFile(LocInfo.first), LangOpts,
		               Buffer.begin(), StrData, Buffer.end());
		TheLexer.SetCommentRetentionState(false);
		TheLexer.LexFromRawLexer(Result);
		return false;
	}

	// 返回插入前置声明所在行的开头
	SourceLocation ParsingFile::GetInsertForwardLine(FileID at, const CXXRecordDecl &cxxRecord) const
	{
		// 该类所在的文件
		FileID recordAtFile	= GetFileID(cxxRecord.getLocStart());

		// 优先以更好的同名文件来取代
		recordAtFile = GetBestSameFile(at, recordAtFile);

		/*
			计算出应在哪个文件哪个行生成前置声明：

			假设文件B使用了文件A中的class a，根据B和A的关系可分三种情况处理

			1. B是A的祖先
				找到B中引入A的#include，可在该行末插入前置声明

			2. A是B的祖先
				不需要处理

			3. 否则，说明A和B没有直系关系
				以下图为例（->表示#include）：
					C -> D -> F -> A
					  -> E -> B

				此时，找到A和B的共同祖先C，找到C中引入A的#include所在行，即#include D，可在该行末插入前置声明
		*/
		// 1. 当前文件是类所在文件的祖先
		if (IsAncestor(recordAtFile, at))
		{
			// 找到A的祖先，要求该祖先必须是B的儿子
			FileID ancestor = GetLvl2Ancestor(recordAtFile, at);
			if (GetParent(ancestor) != at)
			{
				return SourceLocation();
			}

			// 找到祖先文件所对应的#include语句的位置
			SourceLocation includeLoc	= m_srcMgr->getIncludeLoc(ancestor);
			SourceRange line			= GetCurLine(includeLoc);
			return line.getBegin();
		}
		// 2. 类所在文件是当前文件的祖先
		else if (IsAncestor(at, recordAtFile))
		{
			// 不需要处理
		}
		// 3. 类所在文件和当前文件没有直系关系
		else
		{
			// 找到这2个文件的共同祖先
			FileID common_ancestor = GetCommonAncestor(recordAtFile, at);
			if (common_ancestor.isInvalid())
			{
				return SourceLocation();
			}

			// 找到类所在文件的祖先，要求该祖先必须是共同祖先的儿子
			FileID ancestor = GetLvl2Ancestor(recordAtFile, common_ancestor);
			if (GetParent(ancestor) != common_ancestor)
			{
				return SourceLocation();
			}

			// 找到祖先文件所对应的#include语句的位置
			SourceLocation includeLoc	= m_srcMgr->getIncludeLoc(ancestor);
			SourceRange line			= GetCurLine(includeLoc);
			return line.getBegin();
		}

		// 不可能执行到这里
		return SourceLocation();
	}

	// 新增使用前置声明记录（对于不必要添加的前置声明将在之后进行清理）
	void ParsingFile::UseForward(SourceLocation loc, const CXXRecordDecl *cxxRecord)
	{
		if (nullptr == cxxRecord)
		{
			return;
		}

		FileID file = GetFileID(loc);
		if (file.isInvalid())
		{
			return;
		}

		if (Project::IsCleanModeOpen(CleanMode_Need))
		{
			// 如果本文件内该位置之前已有前置声明则不再处理
			const TagDecl *first = cxxRecord->getFirstDecl();
			for (const TagDecl *next : first->redecls())
			{
				if (m_srcMgr->isBeforeInTranslationUnit(loc, next->getLocation()))
				{
					break;
				}

				FileID recordAtFile = GetFileID(next->getLocation());

				if (file == recordAtFile && !next->isThisDeclarationADefinition())
				{
					LogInfoByLvl(LogLvl_3, "skip record = [" <<  GetRecordName(*cxxRecord) << "], record file = " << GetDebugFileName(file));

					return;
				}
			}

			// 添加文件所使用的前置声明记录
			m_useRecords[loc].insert(cxxRecord);
		}
		else
		{
			// 如果该位置之前已有前置声明则不再加前置声明，尽量避免生成额外的前置声明
			const TagDecl *first = cxxRecord->getFirstDecl();
			for (const TagDecl *next : first->redecls())
			{
				if (m_srcMgr->isBeforeInTranslationUnit(loc, next->getLocation()))
				{
					break;
				}

				if (!next->isThisDeclarationADefinition())
				{
					UseNameDecl(loc, next);
					return;
				}
			}

			UseRecord(loc, cxxRecord);
		}
	}

	// 是否为可前置声明的类型
	bool ParsingFile::IsForwardType(const QualType &var)
	{
		if (!var->isPointerType() && !var->isReferenceType())
		{
			return false;
		}

		QualType pointeeType = var->getPointeeType();

		// 如果是指针类型就获取其指向类型(PointeeType)
		while (isa<PointerType>(pointeeType) || isa<ReferenceType>(pointeeType))
		{
			pointeeType = pointeeType->getPointeeType();
		}

		if (!isa<RecordType>(pointeeType))
		{
			return false;
		}

		const CXXRecordDecl *cxxRecordDecl = pointeeType->getAsCXXRecordDecl();
		if (nullptr == cxxRecordDecl)
		{
			return false;
		}

		// 模板特化
		if (isa<ClassTemplateSpecializationDecl>(cxxRecordDecl))
		{
			return false;
		}

		return true;
	}

	// 新增使用变量记录
	void ParsingFile::UseVarType(SourceLocation loc, const QualType &var)
	{
		if (!IsForwardType(var))
		{
			UseQualType(loc, var);
			return;
		}

		if (!IsUserFile(GetFileID(loc)))
		{
			UseQualType(loc, var);
			return;
		}

		QualType pointeeType = var->getPointeeType();
		while (pointeeType->isPointerType() || pointeeType->isReferenceType())
		{
			pointeeType = pointeeType->getPointeeType();
		}

		const CXXRecordDecl *cxxRecordDecl = pointeeType->getAsCXXRecordDecl();
		UseForward(loc, cxxRecordDecl);
	}

	// 引用构造函数
	void ParsingFile::UseConstructor(SourceLocation loc, const CXXConstructorDecl *constructor)
	{
		for (const CXXCtorInitializer *initializer : constructor->inits())
		{
			if (initializer->isAnyMemberInitializer())
			{
				UseValueDecl(initializer->getSourceLocation(), initializer->getAnyMember());
			}
			else if (initializer->isBaseInitializer())
			{
				UseType(initializer->getSourceLocation(), initializer->getBaseClass());
			}
			else if (initializer->isDelegatingInitializer())
			{
				if (initializer->getTypeSourceInfo())
				{
					UseQualType(initializer->getSourceLocation(), initializer->getTypeSourceInfo()->getType());
				}
			}
			else
			{
				// decl->dump();
			}
		}

		UseValueDecl(loc, constructor);
	}

	// 引用变量声明
	void ParsingFile::UseVarDecl(SourceLocation loc, const VarDecl *var)
	{
		if (nullptr == var)
		{
			return;
		}

		UseValueDecl(loc, var);
	}

	// 引用：变量声明（为左值）、函数标识、enum常量
	void ParsingFile::UseValueDecl(SourceLocation loc, const ValueDecl *valueDecl)
	{
		if (nullptr == valueDecl)
		{
			return;
		}

		UseContext(loc, valueDecl->getDeclContext());

		if (isa<TemplateDecl>(valueDecl))
		{
			const TemplateDecl *t = cast<TemplateDecl>(valueDecl);
			if (nullptr == t)
			{
				return;
			}

			UseTemplateDecl(loc, t);
		}

		if (isa<FunctionDecl>(valueDecl))
		{
			const FunctionDecl *f = cast<FunctionDecl>(valueDecl);
			if (nullptr == f)
			{
				return;
			}

			UseFuncDecl(loc, f);
		}

		if (!IsForwardType(valueDecl->getType()))
		{
			std::stringstream name;
			name << valueDecl->getQualifiedNameAsString() << "[" << valueDecl->getDeclKindName() << "]";

			Use(loc, valueDecl->getLocEnd(), name.str().c_str());
		}

		UseVarType(loc, valueDecl->getType());
	}

	// 引用带有名称的声明
	void ParsingFile::UseNameDecl(SourceLocation loc, const NamedDecl *nameDecl)
	{
		if (nullptr == nameDecl)
		{
			return;
		}

		std::stringstream name;
		name << nameDecl->getQualifiedNameAsString() << "[" << nameDecl->getDeclKindName() << "]";

		Use(loc, nameDecl->getLocEnd(), name.str().c_str());
		UseContext(loc, nameDecl->getDeclContext());
	}

	// 新增使用函数声明记录
	void ParsingFile::UseFuncDecl(SourceLocation loc, const FunctionDecl *f)
	{
		if (nullptr == f)
		{
			return;
		}

		// 嵌套名称修饰
		if (f->getQualifier())
		{
			UseQualifier(loc, f->getQualifier());
		}

		// 函数的返回值
		QualType returnType = f->getReturnType();
		UseVarType(loc, returnType);

		// 依次遍历参数，建立引用关系
		for (FunctionDecl::param_const_iterator itr = f->param_begin(), end = f->param_end(); itr != end; ++itr)
		{
			ParmVarDecl *vardecl = *itr;
			UseVarDecl(loc, vardecl);
		}

		if (f->getTemplateSpecializationArgs())
		{
			const TemplateArgumentList *args = f->getTemplateSpecializationArgs();
			UseTemplateArgumentList(loc, args);
		}

		std::stringstream name;
		name << f->getQualifiedNameAsString() << "[" << f->clang::Decl::getDeclKindName() << "]";

		Use(loc, f->getTypeSpecStartLoc(), name.str().c_str());

		// 若本函数与模板有关
		FunctionDecl::TemplatedKind templatedKind = f->getTemplatedKind();
		if (templatedKind != FunctionDecl::TK_NonTemplate)
		{
			// [调用模板处] 引用 [模板定义处]
			Use(f->getLocStart(), f->getLocation(), name.str().c_str());
		}
	}

	// 引用模板参数
	void ParsingFile::UseTemplateArgument(SourceLocation loc, const TemplateArgument &arg)
	{
		TemplateArgument::ArgKind argKind = arg.getKind();
		switch (argKind)
		{
		case TemplateArgument::Type:
			UseVarType(loc, arg.getAsType());
			break;

		case TemplateArgument::Declaration:
			UseValueDecl(loc, arg.getAsDecl());
			break;

		case TemplateArgument::Expression:
			Use(loc, arg.getAsExpr()->getLocStart());
			break;

		case TemplateArgument::Template:
			UseNameDecl(loc, arg.getAsTemplate().getAsTemplateDecl());
			break;

		default:
			break;
		}
	}

	// 引用模板参数列表
	void ParsingFile::UseTemplateArgumentList(SourceLocation loc, const TemplateArgumentList *args)
	{
		if (nullptr == args)
		{
			return;
		}

		for (unsigned i = 0; i < args->size(); ++i)
		{
			const TemplateArgument &arg = args->get(i);
			UseTemplateArgument(loc, arg);
		}
	}

	// 引用模板定义
	void ParsingFile::UseTemplateDecl(SourceLocation loc, const TemplateDecl *decl)
	{
		if (nullptr == decl)
		{
			return;
		}

		UseNameDecl(loc, decl);

		TemplateParameterList *params = decl->getTemplateParameters();

		for (int i = 0, n = params->size(); i < n; ++i)
		{
			NamedDecl *param = params->getParam(i);
			UseNameDecl(loc, param);
		}
	}

	// 新增使用class、struct、union记录
	void ParsingFile::UseRecord(SourceLocation loc, const RecordDecl *record)
	{
		if (nullptr == record)
		{
			return;
		}

		if (isa<ClassTemplateSpecializationDecl>(record))
		{
			const ClassTemplateSpecializationDecl *d = cast<ClassTemplateSpecializationDecl>(record);
			UseTemplateArgumentList(loc, &d->getTemplateArgs());
		}

		Use(loc, record->getLocStart(), GetRecordName(*record).c_str());
		UseContext(loc, record->getDeclContext());
	}

	// 是否允许清理该c++文件（若不允许清理，则文件内容不会有任何变化）
	bool ParsingFile::CanClean(FileID file) const
	{
		return Project::CanClean(GetAbsoluteFileName(file));
	}

	// 打印各文件的父文件
	void ParsingFile::PrintParent()
	{
		int num = 0;
		for (auto &itr : m_parents)
		{
			FileID child	= itr.first;
			FileID parent	= itr.second;

			// 仅打印跟项目内文件有直接关联的文件
			if (!CanClean(child) && !CanClean(parent))
			{
				continue;
			}

			++num;
		}

		HtmlDiv &div = HtmlLog::instance.m_newDiv;
		div.AddRow(AddPrintIdx() + ". list of parent id: has parent file count = " + htmltool::get_number_html(num), 1);

		for (auto &itr : m_parents)
		{
			FileID child	= itr.first;
			FileID parent	= itr.second;

			// 仅打印跟项目内文件有直接关联的文件
			if (!CanClean(child) && !CanClean(parent))
			{
				continue;
			}

			div.AddRow("kid = " + DebugBeIncludeText(child), 2);
			div.AddRow("parent = " + DebugBeIncludeText(parent), 3);
			div.AddRow("");
		}

		div.AddRow("");
	}

	// 打印各文件的孩子文件
	void ParsingFile::PrintChildren()
	{
		int num = 0;
		for (auto &itr : m_children)
		{
			FileID parent = itr.first;

			// 仅打印跟项目内文件有直接关联的文件
			if (!CanClean(parent))
			{
				continue;
			}

			++num;
		}

		HtmlDiv &div = HtmlLog::instance.m_newDiv;
		div.AddRow(AddPrintIdx() + ". list of children : file count = " + strtool::itoa(num), 1);

		for (auto &itr : m_children)
		{
			FileID parent = itr.first;

			if (!CanClean(parent))
			{
				continue;
			}

			div.AddRow("file = " + DebugBeIncludeText(parent), 2);

			for (FileID child : itr.second)
			{
				if (!CanClean(child))
				{
					continue;
				}

				div.AddRow("child = " + DebugBeIncludeText(child), 3);
			}

			div.AddRow("");
		}
	}

	// 打印各文件的孩子文件
	void ParsingFile::PrintUserChildren()
	{
		HtmlDiv &div = HtmlLog::instance.m_newDiv;
		div.AddRow(AddPrintIdx() + ". list of user children : file count = " + strtool::itoa(m_userChildren.size()), 1);

		for (auto &itr : m_userChildren)
		{
			FileID parent = itr.first;

			div.AddRow("file = " + DebugBeIncludeText(parent), 2);

			for (FileID child : itr.second)
			{
				div.AddRow("user child = " + DebugBeIncludeText(child), 3);
			}

			div.AddRow("");
		}
	}

	void ParsingFile::PrintSameChildren()
	{
		HtmlDiv &div = HtmlLog::instance.m_newDiv;
		div.AddRow(AddPrintIdx() + ". list of children by same name : file count = " + strtool::itoa(m_childrenBySame.size()), 1);

		for (auto &itr : m_childrenBySame)
		{
			FileID parent = itr.first;

			div.AddRow("file = " + DebugBeIncludeText(parent), 2);

			for (FileID child : itr.second)
			{
				if (!CanClean(child))
				{
					continue;
				}

				div.AddRow("child = " + DebugBeIncludeText(child), 3);
			}

			div.AddRow("");
		}
	}

	// 获取该文件的被包含信息，返回内容包括：该文件名、父文件名、被父文件#include的行号、被父文件#include的原始文本串
	string ParsingFile::DebugBeIncludeText(FileID file) const
	{
		string fileName = GetAbsoluteFileName(file);
		std::string text;

		if (file == m_srcMgr->getMainFileID())
		{
			text = strtool::get_text(cn_main_file_debug_text,
			                         htmltool::get_file_html(fileName).c_str(),
			                         file.getHashValue(),
			                         htmltool::get_number_html(GetDeepth(file)).c_str());
		}
		else
		{
			SourceLocation loc				= m_srcMgr->getIncludeLoc(file);
			PresumedLoc parentPresumedLoc	= m_srcMgr->getPresumedLoc(loc);
			string includeToken				= GetIncludeLine(file);
			string parentFileName			= pathtool::get_absolute_path(parentPresumedLoc.getFilename());

			stringstream ancestors;

			for (FileID parent = GetParent(file); parent.isValid();)
			{
				ancestors << htmltool::get_min_file_name_html(GetAbsoluteFileName(parent));

				parent = GetParent(parent);
				if (parent.isValid())
				{
					ancestors << "<-";
				}
			}

			if (includeToken.empty())
			{
				includeToken = "empty";
			}

			text = strtool::get_text(cn_file_debug_text,
			                         htmltool::get_file_html(fileName).c_str(),
			                         file.getHashValue(),
			                         htmltool::get_number_html(GetDeepth(file)).c_str(),
			                         htmltool::get_file_html(parentFileName).c_str(),
			                         htmltool::get_number_html(GetLineNo(loc)).c_str(),
			                         htmltool::get_include_html(includeToken).c_str(),
			                         ancestors.str().c_str()
			                        );
		}

		return text;
	}

	// 获取该文件的被直接包含信息，返回内容包括：该文件名、被父文件#include的行号、被父文件#include的原始文本串
	string ParsingFile::DebugBeDirectIncludeText(FileID file) const
	{
		string fileName			= GetFileName(file);
		string includeToken		= GetIncludeLine(file);

		if (includeToken.empty())
		{
			includeToken = "empty";
		}

		std::stringstream text;
		text << "{line = " << htmltool::get_number_html(GetIncludeLineNo(file)) << " [" << htmltool::get_include_html(includeToken) << "]";
		text << "} -> ";
		text << "[" << htmltool::get_file_html(fileName) << "]";

		return text.str();
	}

	// 获取该位置所在行的信息：所在行的文本、所在文件名、行号
	string ParsingFile::DebugLocText(SourceLocation loc) const
	{
		string lineText = GetSourceOfLine(loc);
		std::stringstream text;
		text << "[" << htmltool::get_include_html(lineText) << "] in [";
		text << htmltool::get_file_html(GetAbsoluteFileName(GetFileID(loc)));
		text << "] line = " << htmltool::get_number_html(GetLineNo(loc)) << " col = " << htmltool::get_number_html(m_srcMgr->getSpellingColumnNumber(loc));
		return text.str();
	}

	// 获取文件名（通过clang库接口，文件名未经过处理，可能是绝对路径，也可能是相对路径）
	// 例如：
	//     可能返回绝对路径：d:/a/b/hello.h
	//     也可能返回：./../hello.h
	const char* ParsingFile::GetFileName(FileID file) const
	{
		const FileEntry *fileEntry = m_srcMgr->getFileEntryForID(file);
		if (nullptr == fileEntry)
		{
			return "";
		}

		return fileEntry->getName();
	}

	// 获取拼写位置
	inline SourceLocation ParsingFile::GetSpellingLoc(SourceLocation loc) const
	{
		return m_srcMgr->getSpellingLoc(loc);
	}

	// 获取经过宏扩展后的位置
	inline SourceLocation ParsingFile::GetExpasionLoc(SourceLocation loc) const
	{
		return m_srcMgr->getExpansionLoc(loc);
	}

	// 获取文件ID
	inline FileID ParsingFile::GetFileID(SourceLocation loc) const
	{
		FileID fileID = m_srcMgr->getFileID(loc);
		if (fileID.isInvalid())
		{
			fileID = m_srcMgr->getFileID(GetSpellingLoc(loc));

			if (fileID.isInvalid())
			{
				fileID = m_srcMgr->getFileID(GetExpasionLoc(loc));
			}
		}

		return fileID;
	}

	// 获取文件的绝对路径
	string ParsingFile::GetAbsoluteFileName(FileID file) const
	{
		const char* raw_file_name = GetFileName(file);
		return pathtool::get_absolute_path(raw_file_name);
	}

	// 用于调试：获取文件的绝对路径和相关信息
	string ParsingFile::GetDebugFileName(FileID file) const
	{
		stringstream name;
		stringstream ancestors;

		name << "[[" << GetAbsoluteFileName(file) << "(ID = " << file.getHashValue() << ")";

		for (FileID parent = GetParent(file); parent.isValid(); )
		{
			ancestors << pathtool::get_file_name(GetAbsoluteFileName(parent));

			parent = GetParent(parent);
			if (parent.isValid())
			{
				ancestors << ",";
			}
		}

		if (!ancestors.str().empty())
		{
			name << "(" << ancestors.str() << ")";
		}

		name << "]]";

		return name.str();
	}

	// 获取第1个文件#include第2个文件的文本串
	std::string ParsingFile::GetRelativeIncludeStr(FileID f1, FileID f2) const
	{
		// 若第2个文件的被包含串原本就是#include <xxx>的格式，则返回原本的#include串
		{
			string rawInclude2 = GetIncludeLine(f2);
			if (rawInclude2.empty())
			{
				return "";
			}

			// 是否被尖括号包含，如：<stdio.h>
			bool isAngled = strtool::contain(rawInclude2.c_str(), '<');
			if (isAngled)
			{
				return rawInclude2;
			}
		}

		string absolutePath1 = GetAbsoluteFileName(f1);
		string absolutePath2 = GetAbsoluteFileName(f2);

		string include2;

		// 优先判断2个文件是否在同一文件夹下
		if (get_dir(absolutePath1) == get_dir(absolutePath2))
		{
			include2 = "\"" + strip_dir(absolutePath2) + "\"";
		}
		else
		{
			// 在头文件搜索路径中搜寻第2个文件，若成功找到，则返回基于头文件搜索路径的相对路径
			include2 = GetQuotedIncludeStr(absolutePath2);

			// 若未在头文件搜索路径搜寻到第2个文件，则返回基于第1个文件的相对路径
			if (include2.empty())
			{
				include2 = "\"" + pathtool::get_relative_path(absolutePath1.c_str(), absolutePath2.c_str()) + "\"";
			}
		}

		return "#include " + include2;
	}

	// 开始清理文件（将改动c++源文件）
	void ParsingFile::Clean()
	{
		// 是否覆盖c++源文件
		if (!Project::instance.m_isOverWrite)
		{
			return;
		}

		if (Project::instance.m_isCleanAll)
		{
			// 清理所有c++文件
			CleanAllFile();
		}
		else
		{
			// 仅清理当前cpp文件
			CleanMainFile();
		}

		// 将变动回写到c++文件
		bool err = Overwrite();
		if (err)
		{
			LogError("overwrite some changed files failed!");
		}
	}

	// 将清理结果回写到c++源文件，返回：true回写文件时发生错误 false回写成功
	// （本接口拷贝自Rewriter::overwriteChangedFiles，唯一的不同是回写成功时会删除文件缓存）
	bool ParsingFile::Overwrite()
	{
		class CxxCleanReWriter
		{
		public:
			// 给文件添加可写权限
			static bool EnableWrite(const char *file_name)
			{
				struct stat s;
				int err = stat(file_name, &s);
				if (err > 0)
				{
					return false;
				}

				err = _chmod(file_name, s.st_mode|S_IWRITE);
				return err == 0;
			}

			// 覆盖文件
			static bool WriteToFile(const RewriteBuffer &rewriteBuffer, const FileEntry &entry)
			{
				if (!EnableWrite(entry.getName()))
				{
					return false;
				}

				std::ofstream fout;
				fout.open(entry.getName(), ios_base::out | ios_base::binary);

				if (!fout.is_open())
				{
					return false;
				}

				for (RopePieceBTreeIterator itr = rewriteBuffer.begin(), end = rewriteBuffer.end(); itr != end; itr.MoveToNextPiece())
				{
					fout << itr.piece().str();
				}

				fout.close();
				return true;
			}
		};

		SourceManager &srcMgr	= m_rewriter->getSourceMgr();
		FileManager &fileMgr	= srcMgr.getFileManager();

		bool AllWritten = true;
		for (Rewriter::buffer_iterator I = m_rewriter->buffer_begin(), E = m_rewriter->buffer_end(); I != E; ++I)
		{
			const FileEntry *entry				= srcMgr.getFileEntryForID(I->first);
			const RewriteBuffer &rewriteBuffer	= I->second;

			LogInfoByLvl(LogLvl_2, "overwriting [" << entry->getName() << "]...");

			bool ok = CxxCleanReWriter::WriteToFile(rewriteBuffer, *entry);
			if (!ok)
			{
				LogError("overwrite file[" << entry->getName() << "] failed!");
				AllWritten = false;
			}
			else
			{
				LogInfoByLvl(LogLvl_2, "overwriting [" << entry->getName() << "] success!");
			}

			/*
			AtomicallyMovedFile File(srcMgr.getDiagnostics(), Entry->getName(), AllWritten);
			if (File.ok())
			{
				rewriteBuffer.write(File.getStream());
				fileMgr.modifyFileEntry(const_cast<FileEntry*>(Entry), rewriteBuffer.size(), Entry->getModificationTime());
			}

			*/
			//fileMgr.invalidateCache(Entry);
		}

		return !AllWritten;
	}

	// 替换指定范围文本
	void ParsingFile::ReplaceText(FileID file, int beg, int end, string text)
	{
		SourceLocation fileBegLoc	= m_srcMgr->getLocForStartOfFile(file);
		SourceLocation begLoc		= fileBegLoc.getLocWithOffset(beg);
		SourceLocation endLoc		= fileBegLoc.getLocWithOffset(end);

		if (nullptr == GetSourceAtLoc(begLoc) || nullptr == GetSourceAtLoc(endLoc))
		{
			LogError("nullptr == GetSourceAtLoc(begLoc) || nullptr == GetSourceAtLoc(endLoc)");
			return;
		}

		LogInfoByLvl(LogLvl_2, "replace [" << GetAbsoluteFileName(file) << "]: [" << beg << "," << end << "] to text = [" << text << "]");

		bool err = m_rewriter->ReplaceText(begLoc, end - beg, text);
		if (err)
		{
			LogError("replace [" << GetAbsoluteFileName(file) << "]: [" << beg << "," << end << "] to text = [" << text << "] failed");
		}
	}

	// 将文本插入到指定位置之前
	// 例如：假设有"abcdefg"文本，则在c处插入123的结果将是："ab123cdefg"
	void ParsingFile::InsertText(FileID file, int loc, string text)
	{
		if (text.empty())
		{
			return;
		}

		SourceLocation fileBegLoc	= m_srcMgr->getLocForStartOfFile(file);
		SourceLocation insertLoc	= fileBegLoc.getLocWithOffset(loc);

		if (nullptr == GetSourceAtLoc(insertLoc))
		{
			LogError("nullptr == GetSourceAtLoc(insertLoc)");
			return;
		}

		LogInfoByLvl(LogLvl_2, "insert [" << GetAbsoluteFileName(file) << "]: [" << loc << "] to text = [" << text << "]");

		bool err = m_rewriter->InsertText(insertLoc, text, false, false);
		if (err)
		{
			LogError("insert [" << GetAbsoluteFileName(file) << "]: [" << loc << "] to text = [" << text << "] failed");
		}
	}

	// 移除指定范围文本，若移除文本后该行变为空行，则将该空行一并移除
	void ParsingFile::RemoveText(FileID file, int beg, int end)
	{
		SourceLocation fileBegLoc	= m_srcMgr->getLocForStartOfFile(file);
		if (fileBegLoc.isInvalid())
		{
			LogError("if (fileBegLoc.isInvalid()), remove text in [" << GetAbsoluteFileName(file) << "] failed!");
			return;
		}

		SourceLocation begLoc		= fileBegLoc.getLocWithOffset(beg);
		SourceLocation endLoc		= fileBegLoc.getLocWithOffset(end);

		if (nullptr == GetSourceAtLoc(begLoc) || nullptr == GetSourceAtLoc(endLoc))
		{
			LogError("nullptr == GetSourceAtLoc(begLoc) || nullptr == GetSourceAtLoc(endLoc)");
			return;
		}

		SourceRange range(begLoc, endLoc);

		Rewriter::RewriteOptions rewriteOption;
		rewriteOption.IncludeInsertsAtBeginOfRange	= false;
		rewriteOption.IncludeInsertsAtEndOfRange	= false;
		rewriteOption.RemoveLineIfEmpty				= false;

		LogInfoByLvl(LogLvl_2, "remove [" << GetAbsoluteFileName(file) << "]: [" << beg << "," << end << "], text = [" << GetSourceOfRange(range) << "]");

		bool err = m_rewriter->RemoveText(range.getBegin(), end - beg, rewriteOption);
		if (err)
		{
			LogError("remove [" << GetAbsoluteFileName(file) << "]: [" << beg << "," << end << "], text = [" << GetSourceOfRange(range) << "] failed");
		}
	}

	// 移除指定文件内的无用行
	void ParsingFile::CleanByDelLine(const FileHistory &history, FileID file)
	{
		if (history.m_delLines.empty())
		{
			return;
		}

		for (auto &itr : history.m_delLines)
		{
			int line = itr.first;
			const DelLine &delLine = itr.second;

			if (line > 0)
			{
				RemoveText(file, delLine.beg, delLine.end);
			}
		}
	}

	// 在指定文件内添加前置声明
	void ParsingFile::CleanByForward(const FileHistory &history, FileID file)
	{
		if (history.m_forwards.empty())
		{
			return;
		}

		for (auto &itr : history.m_forwards)
		{
			int line = itr.first;
			const ForwardLine &forwardLine	= itr.second;

			if (line > 0)
			{
				std::stringstream text;

				for (const string &cxxRecord : forwardLine.classes)
				{
					text << cxxRecord;
					text << history.GetNewLineWord();
				}

				InsertText(file, forwardLine.offset, text.str());
			}
		}
	}

	// 在指定文件内替换#include
	void ParsingFile::CleanByReplace(const FileHistory &history, FileID file)
	{
		if (history.m_replaces.empty())
		{
			return;
		}

		const std::string newLineWord = history.GetNewLineWord();

		for (auto &itr : history.m_replaces)
		{
			int line = itr.first;
			const ReplaceLine &replaceLine	= itr.second;

			// 若是被-include参数强制引入，则跳过，因为替换并没有效果
			if (replaceLine.isSkip || line <= 0)
			{
				continue;
			}

			// 替换#include
			std::stringstream text;

			const ReplaceTo &replaceInfo = replaceLine.replaceTo;
			text << replaceInfo.newText;
			text << newLineWord;

			ReplaceText(file, replaceLine.beg, replaceLine.end, text.str());
		}
	}

	// 在指定文件内新增行
	void ParsingFile::CleanByAdd(const FileHistory &history, FileID file)
	{
		if (history.m_adds.empty())
		{
			return;
		}

		for (auto & addItr : history.m_adds)
		{
			int line				= addItr.first;
			const AddLine &addLine	= addItr.second;

			if (line > 0)
			{
				std::stringstream text;

				for (const BeAdd &beAdd : addLine.adds)
				{
					text << beAdd.text;
					text << history.GetNewLineWord();
				}

				InsertText(file, addLine.offset, text.str());
			}
		}
	}

	// 根据历史清理指定文件
	void ParsingFile::CleanByHistory(const FileHistoryMap &historys)
	{
		std::map<std::string, FileID> nameToFileIDMap;

		// 建立当前cpp中文件名到文件FileID的map映射（注意：同一个文件可能被包含多次，FileID是不一样的，这里只存入最小的FileID）
		for (FileID file : m_files)
		{
			const string &name = GetAbsoluteFileName(file);

			if (!Project::CanClean(name))
			{
				continue;
			}

			if (nameToFileIDMap.find(name) == nameToFileIDMap.end())
			{
				nameToFileIDMap.insert(std::make_pair(name, file));
			}
		}

		for (auto &itr : historys)
		{
			const string &fileName		= itr.first;
			const FileHistory &history	= itr.second;

			if (!ProjectHistory::instance.HasFile(fileName))
			{
				continue;
			}

			if (ProjectHistory::instance.HasCleaned(fileName))
			{
				continue;
			}

			if (!Project::CanClean(fileName))
			{
				continue;
			}

			if (history.m_isSkip || history.HaveFatalError())
			{
				continue;
			}

			// 根据名称在当前cpp各文件中找到对应的文件ID（注意：同一个文件可能被包含多次，FileID是不一样的，这里取出来的是最小的FileID）
			auto & findItr = nameToFileIDMap.find(fileName);
			if (findItr == nameToFileIDMap.end())
			{
				continue;
			}

			FileID file	= findItr->second;

			CleanByReplace(history, file);
			CleanByForward(history, file);
			CleanByDelLine(history, file);
			CleanByAdd(history, file);

			ProjectHistory::instance.OnCleaned(fileName);
		}
	}

	// 清理所有有必要清理的文件
	void ParsingFile::CleanAllFile()
	{
		CleanByHistory(ProjectHistory::instance.m_files);
	}

	// 清理主文件
	void ParsingFile::CleanMainFile()
	{
		FileHistoryMap root;

		// 仅取出主文件的待清理记录
		{
			string rootFileName = GetAbsoluteFileName(m_srcMgr->getMainFileID());

			auto & rootItr = ProjectHistory::instance.m_files.find(rootFileName);
			if (rootItr != ProjectHistory::instance.m_files.end())
			{
				root.insert(*rootItr);
			}
		}

		CleanByHistory(root);
	}

	// 修正
	void ParsingFile::Fix()
	{
		/*
		auto fixes = m_includes;

		for (auto &itr : m_moves)
		{
			FileID at = itr.first;

			for (FileID beMove : itr.second)
			{
				fixes[at].insert(beMove);

				FileID parent = GetParent(beMove);
				if (parent)
				{
					fixes[parent].erase(beMove);
				}
			}
		}

		for (auto &itr : m_replaces)
		{
			FileID from	= itr.first;
			FileID to	= itr.second;

			FileID parent = GetParent(from);
			if (parent)
			{
				fixes[parent].erase(from);
				fixes[parent].insert(to);
			}
		}

		std::set<FileID> beUseList;
		for (auto &itr : m_uses)
		{
			for (FileID beUse : itr.second)
			{
				beUseList.insert(beUse);
			}
		}

		for (auto &itr : fixes)
		{
			std::set<FileID> &includes = itr.second;
			auto copy = includes;

			for (FileID beInclude : copy)
			{
				if (beUseList.find(beInclude) == beUseList.end())
				{
					includes.erase(beInclude);
				}
			}
		}

		std::map<FileID, FileID> parents;

		for (auto &itr : fixes)
		{
			FileID by = itr.first;
			std::set<FileID> &includes = itr.second;

			for (FileID beInclude : includes)
			{
				parents[beInclude] = by;
			}
		}

		std::map<FileID, std::set<FileID>>	children;
		for (auto &itr : fixes)
		{
			FileID by = itr.first;
			std::set<FileID> &includes = itr.second;

			for (FileID beInclude : includes)
			{
				for (FileID child = beInclude, parent; ; child = parent)
				{
					auto parentItr = parents.find(child);
					if (parentItr == parents.end())
					{
						break;
					}

					parent = parentItr->second;
					children[parent].insert(beInclude);
				}
			}
		}

		for (auto &itr : m_moves)
		{
			FileID at = itr.first;

			auto childItr = children.find(at);
			if (childItr == children.end())
			{
				continue;
			}

			auto &childList = childItr->second;

			for (FileID beMove : itr.second)
			{
				std::string name = GetAbsoluteFileName(move)

			}
		}
		*/
	}

	// 打印头文件搜索路径
	void ParsingFile::PrintHeaderSearchPath() const
	{
		if (m_headerSearchPaths.empty())
		{
			return;
		}

		HtmlDiv &div = HtmlLog::instance.m_newDiv;
		div.AddRow(AddPrintIdx() + ". header search path list : path count = " + htmltool::get_number_html(m_headerSearchPaths.size()), 1);

		for (const HeaderSearchDir &path : m_headerSearchPaths)
		{
			div.AddRow("search path = " + htmltool::get_file_html(path.m_dir), 2);
		}

		div.AddRow("");
	}

	// 用于调试：打印各文件引用的文件集相对于该文件的#include文本
	void ParsingFile::PrintRelativeInclude() const
	{
		int num = 0;
		for (auto &itr : m_uses)
		{
			FileID file = itr.first;

			if (!CanClean(file))
			{
				continue;
			}

			++num;
		}

		HtmlDiv &div = HtmlLog::instance.m_newDiv;
		div.AddRow(AddPrintIdx() + ". relative include list : use = " + htmltool::get_number_html(num), 1);

		for (auto &itr : m_uses)
		{
			FileID file = itr.first;

			if (!CanClean(file))
			{
				continue;
			}

			const std::set<FileID> &be_uses = itr.second;

			div.AddRow("file = " + DebugBeIncludeText(file), 2);

			for (FileID be_used_file : be_uses)
			{
				div.AddRow("old include = " + htmltool::get_include_html(GetIncludeFullLine(be_used_file)), 3, 45);
				div.AddGrid("-> relative include = " + htmltool::get_include_html(GetRelativeIncludeStr(file, be_used_file)));
			}

			div.AddRow("");
		}
	}

	// 用于调试跟踪：打印是否有文件的被包含串被遗漏
	void ParsingFile::PrintNotFoundIncludeLocForDebug()
	{
		HtmlDiv &div = HtmlLog::instance.m_newDiv;
		div.AddRow(AddPrintIdx() + ". not found include loc:", 1);

		for (auto &itr : m_uses)
		{
			const std::set<FileID> &beuse_files = itr.second;

			for (FileID beuse_file : beuse_files)
			{
				if (beuse_file == m_srcMgr->getMainFileID())
				{
					continue;
				}

				SourceLocation loc = m_srcMgr->getIncludeLoc(beuse_file);
				if (m_includeLocs.find(loc) == m_includeLocs.end())
				{
					div.AddRow("not found = " + DebugBeIncludeText(beuse_file), 2);
					div.AddRow("old text = " + DebugLocText(loc), 2);
					continue;
				}
			}
		}

		div.AddRow("");
	}

	// 打印索引 + 1
	std::string ParsingFile::AddPrintIdx() const
	{
		return strtool::itoa(++m_printIdx);
	}

	// 打印信息
	void ParsingFile::Print()
	{
		LogLvl verbose = Project::instance.m_logLvl;
		if (verbose <= LogLvl_0)
		{
			return;
		}

		HtmlDiv &div = HtmlLog::instance.m_newDiv;

		std::string rootFileName	= GetAbsoluteFileName(m_srcMgr->getMainFileID());
		div.AddTitle(strtool::get_text(cn_file_history_title,
		                               htmltool::get_number_html(ProjectHistory::instance.g_fileNum).c_str(),
		                               htmltool::get_number_html(Project::instance.m_cpps.size()).c_str(),
		                               htmltool::get_file_html(rootFileName).c_str()));

		m_printIdx = 0;

		if (verbose >= LogLvl_1)
		{
			PrintCleanLog();
		}

		if (verbose >= LogLvl_3)
		{
			PrintMinUse();
			PrintMinKid();
			PrintUserUse();
			PrintUse();
			PrintUsedNames();
			PrintSameFile();
			PrintForwardDecl();
		}

		if (verbose >= LogLvl_4)
		{
			if (Project::IsCleanModeOpen(CleanMode_Need))
			{
				PrintOutFileAncestor();
				PrintUserChildren();
				PrintSameChildren();
			}
			else
			{
				PrintTopRely();
				PrintRely();
				PrintRelyChildren();
			}

			PrintAllFile();
			PrintInclude();
			PrintHeaderSearchPath();
			PrintRelativeInclude();
			PrintParent();
			PrintChildren();
			PrintNamespace();
			PrintUsingNamespace();
		}

		if (verbose >= LogLvl_5)
		{
			PrintNotFoundIncludeLocForDebug();
		}

		HtmlLog::instance.AddDiv(div);
	}

	// 合并可被移除的#include行
	void ParsingFile::MergeUnusedLine(const FileHistory &newFile, FileHistory &oldFile) const
	{
		FileHistory::DelLineMap &oldLines = oldFile.m_delLines;

		for (FileHistory::DelLineMap::iterator oldLineItr = oldLines.begin(); oldLineItr != oldLines.end(); )
		{
			int line			= oldLineItr->first;
			DelLine &oldLine	= oldLineItr->second;

			if (newFile.IsLineUnused(line))
			{
				LogInfoByLvl(LogLvl_3, "merge unused at [" << oldFile.m_filename << "]: new line unused, old line unused at line = " << line << " -> " << oldLine.text );
				++oldLineItr;
			}
			else
			{
				// 若在新文件中该行应被替换，而在旧文件中该行应被删除，则在旧文件中新增替换记录
				if (newFile.IsLineBeReplaced(line))
				{
					auto & newReplaceLineItr = newFile.m_replaces.find(line);
					const ReplaceLine &newReplaceLine = newReplaceLineItr->second;

					oldFile.m_replaces[line] = newReplaceLine;

					LogInfoByLvl(LogLvl_3, "merge unused at [" << oldFile.m_filename << "]: new line replace, old line unused at line = " << line << " -> " << oldLine.text );
				}
				else
				{
					LogInfoByLvl(LogLvl_3, "merge unused at [" << oldFile.m_filename << "]: new line do nothing, old line unused at line = " << line << " -> " << oldLine.text );
				}

				oldLines.erase(oldLineItr++);
			}
		}
	}

	// 合并可新增的前置声明
	void ParsingFile::MergeForwardLine(const FileHistory &newFile, FileHistory &oldFile) const
	{
		for (auto & newLineItr : newFile.m_forwards)
		{
			int line					= newLineItr.first;
			const ForwardLine &newLine	= newLineItr.second;

			auto & oldLineItr = oldFile.m_forwards.find(line);
			if (oldLineItr != oldFile.m_forwards.end())
			{
				ForwardLine &oldLine	= oldFile.m_forwards[line];
				oldLine.classes.insert(newLine.classes.begin(), newLine.classes.end());
			}
			else
			{
				oldFile.m_forwards[line] = newLine;
			}
		}
	}

	// 合并可替换的#include
	void ParsingFile::MergeReplaceLine(const FileHistory &newFile, FileHistory &oldFile) const
	{
		if (oldFile.m_replaces.empty() && newFile.m_replaces.empty())
		{
			// 本文件内没有需要替换的#include
			return;
		}

		FileID newFileID;
		auto & newFileItr = m_splitReplaces.begin();

		for (auto &itr = m_splitReplaces.begin(); itr != m_splitReplaces.end(); ++itr)
		{
			FileID top = itr->first;

			if (GetAbsoluteFileName(top) == oldFile.m_filename)
			{
				newFileID	= top;
				newFileItr	= itr;
				break;
			}
		}

		// 1. 合并新增替换记录到历史记录中
		for (auto & oldLineItr = oldFile.m_replaces.begin(), end = oldFile.m_replaces.end(); oldLineItr != end; )
		{
			int line				= oldLineItr->first;
			ReplaceLine &oldLine	= oldLineItr->second;

			// 该行是否发生冲突了
			auto &conflictItr	= newFile.m_replaces.find(line);
			bool is_conflict	= (conflictItr != newFile.m_replaces.end());

			// 若发生冲突，则分析被新的取代文件与旧的取代文件是否有直系后代关系
			if (is_conflict)
			{
				const ReplaceLine &newLine	= conflictItr->second;

				// 若2个替换一致则保留
				if (newLine.replaceTo.newText == oldLine.replaceTo.newText)
				{
					++oldLineItr;
					continue;
				}

				// 说明该文件未新生成任何替换记录，此时应将该文件内所有旧替换记录全部删除
				if (newFileID.isInvalid())
				{
					LogInfoByLvl(LogLvl_3, "merge repalce at [" << oldFile.m_filename << "]: error, not found newFileID at line = " << line << " -> " << oldLine.oldText );

					++oldLineItr;
					continue;
				}

				const ChildrenReplaceMap &newReplaceMap	= newFileItr->second;

				// 找到该行旧的#include对应的FileID
				FileID beReplaceFileID;

				for (auto & childItr : newReplaceMap)
				{
					FileID child = childItr.first;
					if (GetAbsoluteFileName(child) == newLine.oldFile)
					{
						beReplaceFileID = child;
						break;
					}
				}

				if (beReplaceFileID.isInvalid())
				{
					++oldLineItr;
					continue;
				}

				// 若旧的取代文件是新的取代文件的祖先，则保留旧的替换信息
				if (IsAncestor(beReplaceFileID, oldLine.oldFile.c_str()))
				{
					LogInfoByLvl(LogLvl_3, "merge repalce at [" << oldFile.m_filename << "]: old line replace is new line replace ancestorat line = " << line << " -> " << oldLine.oldText );
					++oldLineItr;
				}
				// 若新的取代文件是旧的取代文件的祖先，则改为使用新的替换信息
				else if(IsAncestor(oldLine.oldFile.c_str(), beReplaceFileID))
				{
					LogInfoByLvl(LogLvl_3, "merge repalce at [" << oldFile.m_filename << "]: new line replace is old line replace ancestor at line = " << line << " -> " << oldLine.oldText );

					oldLine.replaceTo = newLine.replaceTo;
					++oldLineItr;
				}
				// 否则，若没有直系关系，则该行无法被替换，删除该行原有的替换记录
				else
				{
					LogInfoByLvl(LogLvl_3, "merge repalce at [" << oldFile.m_filename << "]: old line replace, new line replace at line = " << line << " -> " << oldLine.oldText );

					SkipRelyLines(oldLine.replaceTo.m_rely);
					oldFile.m_replaces.erase(oldLineItr++);
				}
			}
			else
			{
				// 若在新文件中该行应被删除，而在旧文件中该行应被替换，则保留旧文件的替换记录
				if (newFile.IsLineUnused(line))
				{
					LogInfoByLvl(LogLvl_3, "merge repalce at [" << oldFile.m_filename << "]: old line replace, new line delete at line = " << line << " -> " << oldLine.oldText );

					++oldLineItr;
					continue;
				}
				// 若该行没有新的替换记录，说明该行无法被替换，删除该行旧的替换记录
				else
				{
					LogInfoByLvl(LogLvl_3, "merge repalce at [" << oldFile.m_filename << "]: old line replace, new line do nothing at line = " << line << " -> " << oldLine.oldText );

					SkipRelyLines(oldLine.replaceTo.m_rely);
					oldFile.m_replaces.erase(oldLineItr++);
				}
			}
		}

		// 2. 新增的替换记录不能直接被删，要作标记
		for (auto &newLineItr : newFile.m_replaces)
		{
			int line					= newLineItr.first;
			const ReplaceLine &newLine	= newLineItr.second;

			if (!oldFile.IsLineBeReplaced(line))
			{
				SkipRelyLines(newLine.replaceTo.m_rely);
			}
		}
	}

	// 将某些文件中的一些行标记为不可修改
	void ParsingFile::SkipRelyLines(const FileSkipLineMap &skips) const
	{
		for (auto &itr : skips)
		{
			ProjectHistory::instance.m_skips.insert(itr);
		}
	}

	// 将当前cpp文件产生的待清理记录与之前其他cpp文件产生的待清理记录合并
	void ParsingFile::MergeTo(FileHistoryMap &oldFiles) const
	{
		const FileHistoryMap &newFiles = m_historys;

		// 若本文件有严重编译错误或编译错误数过多，则仅合并编译错误历史，用于打印
		if (m_compileErrorHistory.HaveFatalError())
		{
			std::string rootFile = GetAbsoluteFileName(m_srcMgr->getMainFileID());
			auto itr = newFiles.find(rootFile);
			if (itr == newFiles.end())
			{
				return;
			}

			oldFiles[rootFile] = itr->second;
			return;
		}

		for (auto & fileItr : newFiles)
		{
			const string &fileName	= fileItr.first;
			const FileHistory &newFile	= fileItr.second;

			auto & findItr = oldFiles.find(fileName);

			bool found = (findItr != oldFiles.end());
			if (found)
			{
				if (Project::IsCleanModeOpen(CleanMode_Need))
				{
					continue;
				}

				FileHistory &oldFile = findItr->second;

				MergeUnusedLine(newFile, oldFile);
				MergeForwardLine(newFile, oldFile);
				MergeReplaceLine(newFile, oldFile);

				oldFile.Fix();
			}
			else
			{
				oldFiles[fileName] = newFile;
			}
		}
	}

	// 文件格式是否是windows格式，换行符为[\r\n]，类Unix下为[\n]
	bool ParsingFile::IsWindowsFormat(FileID file) const
	{
		SourceLocation fileStart = m_srcMgr->getLocForStartOfFile(file);
		if (fileStart.isInvalid())
		{
			return false;
		}

		// 获取第一行正文范围
		SourceRange firstLine		= GetCurLine(fileStart);
		SourceLocation firstLineEnd	= firstLine.getEnd();

		{
			const char* c1	= GetSourceAtLoc(firstLineEnd);
			const char* c2	= GetSourceAtLoc(firstLineEnd.getLocWithOffset(1));

			// 说明第一行没有换行符，或者第一行正文结束后只跟上一个\r或\n字符
			if (nullptr == c1 || nullptr == c2)
			{
				return false;
			}

			// windows换行格式
			if (*c1 == '\r' && *c2 == '\n')
			{
				return true;
			}
			// 类Unix换行格式
			else if (*c1 == '\n')
			{
				return false;
			}
		}

		return false;
	}

	void ParsingFile::InitHistory(FileID file, FileHistory &history) const
	{
		history.m_isWindowFormat	= IsWindowsFormat(file);
		history.m_filename			= GetAbsoluteFileName(file);
		history.m_isSkip			= IsPrecompileHeader(file);
	}

	// 取出记录，使得各文件仅包含自己所需要的头文件
	void ParsingFile::TakeNeed(FileID top, FileHistory &history) const
	{
		InitHistory(top, history);
		history.m_isSkip			= false;

		auto includeItr = m_includes.find(top);
		if (includeItr == m_includes.end())
		{
			return;
		}

		FileSet kids;

		auto itr = m_min.find(top);
		if (itr != m_min.end())
		{
			kids = itr->second;
		}
		else
		{
			LogInfoByLvl(LogLvl_3, "not in m_min = " << GetDebugFileName(top));
		}

		FileSet includes	= includeItr->second;
		FileSet remainKids	= kids;
		FileSet adds;
		FileSet dels;

		std::map<FileID, FileID> replaces;

		for (FileID kid : kids)
		{
			if (includes.find(kid) != includes.end())
			{
				includes.erase(kid);
				remainKids.erase(kid);
				continue;
			}
		}

		for (FileID beInclude : includes)
		{
			for (FileID kid : remainKids)
			{
				if (IsAncestor(kid, beInclude))
				{
					replaces[beInclude] = kid;
					remainKids.erase(kid);

					break;
				}
			}
		}

		for (auto &itr : replaces)
		{
			FileID replaceFrom = itr.first;
			includes.erase(replaceFrom);
		}

		dels = includes;
		adds = remainKids;

		// 1.
		for (FileID del : dels)
		{
			SourceRange lineRange	= GetIncludeRange(del);
			int line				= GetIncludeLineNo(del);

			DelLine &delLine	= history.m_delLines[line];

			delLine.beg		= m_srcMgr->getFileOffset(lineRange.getBegin());
			delLine.end		= m_srcMgr->getFileOffset(lineRange.getEnd());
			delLine.text	= GetSourceOfLine(lineRange.getBegin());
		}

		auto skipIncludesItr = m_skipIncludeLocs.find(top);
		if (skipIncludesItr != m_skipIncludeLocs.end())
		{
			const std::set<SourceLocation> &skips = skipIncludesItr->second;

			for (SourceLocation skip : skips)
			{
				int line				= GetLineNo(skip);
				SourceRange skipLine = GetCurFullLine(skip);

				DelLine &delLine	= history.m_delLines[line];

				delLine.beg		= m_srcMgr->getFileOffset(skipLine.getBegin());
				delLine.end		= m_srcMgr->getFileOffset(skipLine.getEnd());
				delLine.text	= GetSourceOfLine(skipLine.getBegin());
			}
		}

		// 2.
		for (auto &itr : replaces)
		{
			FileID from	= itr.first;
			FileID to	= itr.second;

			bool isBeForceIncluded		= IsForceIncluded(from);

			// 1. 该行的旧文本
			SourceLocation include_loc	= m_srcMgr->getIncludeLoc(from);
			SourceRange	lineRange		= GetCurFullLine(include_loc);
			int line					= (isBeForceIncluded ? 0 : GetLineNo(include_loc));

			ReplaceLine &replaceLine	= history.m_replaces[line];
			replaceLine.oldFile			= GetAbsoluteFileName(from);
			replaceLine.oldText			= GetIncludeFullLine(from);
			replaceLine.beg				= m_srcMgr->getFileOffset(lineRange.getBegin());
			replaceLine.end				= m_srcMgr->getFileOffset(lineRange.getEnd());
			replaceLine.isSkip			= isBeForceIncluded || IsPrecompileHeader(from);	// 记载是否是强制包含

			// 2. 该行可被替换成什么
			ReplaceTo &replaceTo	= replaceLine.replaceTo;

			SourceLocation deep_include_loc	= m_srcMgr->getIncludeLoc(to);

			// 记录[旧#include、新#include]
			replaceTo.oldText		= GetIncludeFullLine(to);
			replaceTo.newText		= GetRelativeIncludeStr(GetParent(from), to);

			// 记录[所处的文件、所处行号]
			replaceTo.line			= GetLineNo(deep_include_loc);
			replaceTo.fileName		= GetAbsoluteFileName(to);
			replaceTo.inFile		= GetAbsoluteFileName(GetFileID(deep_include_loc));
		}

		// 3.
		for (auto &itr : m_useRecords)
		{
			SourceLocation loc = itr.first;
			const std::set<const CXXRecordDecl*> &cxxRecords = itr.second;

			FileID file = GetFileID(loc);
			string fileName = GetAbsoluteFileName(file);

			for (const CXXRecordDecl *cxxRecord : cxxRecords)
			{
				SourceLocation insertLoc = GetInsertForwardLine(file, *cxxRecord);
				if (insertLoc.isInvalid())
				{
					LogErrorByLvl(LogLvl_2, "insertLoc.isInvalid(), " << GetDebugFileName(file) << ", record = " << GetRecordName(*cxxRecord));
					continue;
				}

				PresumedLoc insertPresumedLoc = m_srcMgr->getPresumedLoc(insertLoc);
				if (insertPresumedLoc.isInvalid())
				{
					LogError("take_new_forwarddecl_by_file getPresumedLoc failed");
					continue;
				}

				string insertFileName = pathtool::get_absolute_path(insertPresumedLoc.getFilename());

				if (history.m_filename != insertFileName)
				{
					continue;
				}

				// 开始取出数据
				int line = insertPresumedLoc.getLine();
				const string cxxRecordName = GetRecordName(*cxxRecord);
				ForwardLine &forwardLine = history.m_forwards[line];

				forwardLine.offset = m_srcMgr->getFileOffset(insertLoc);
				forwardLine.oldText = GetSourceOfLine(insertLoc);
				forwardLine.classes.insert(cxxRecordName);

				SourceLocation fileStart = m_srcMgr->getLocForStartOfFile(GetFileID(insertLoc));
				if (fileStart.getLocWithOffset(forwardLine.offset) != insertLoc)
				{
					LogError("fileStart.getLocWithOffset(forwardLine.m_offsetAtFile) != insertLoc");
				}
			}
		}

		// 4.
		FileID firstInclude;
		int firstIncludeLine = 0;

		for (FileID include : includes)
		{
			int line = GetIncludeLineNo(include);
			if ((firstIncludeLine == 0 || line < firstIncludeLine) && line > 0)
			{
				firstIncludeLine	= line;
				firstInclude		= include;
			}
		}

		for (FileID add : adds)
		{
			FileID lv2 = GetLvl2AncestorBySame(add, top);
			if (lv2.isInvalid())
			{
				LogErrorByLvl(LogLvl_3, "lv2.isInvalid(): " << GetDebugFileName(add) << ", " << GetDebugFileName(top));
				continue;
			}

			if (IsForceIncluded(lv2))
			{
				lv2 = firstInclude;
			}

			int line = GetIncludeLineNo(lv2);

			AddLine &addLine = history.m_adds[line];
			if (addLine.offset <= 0)
			{
				addLine.offset	= m_srcMgr->getFileOffset(GetIncludeRange(lv2).getEnd());
				addLine.oldText	= GetIncludeFullLine(lv2);
			}

			BeAdd beAdd;
			beAdd.fileName	= GetAbsoluteFileName(add);
			beAdd.text		= GetRelativeIncludeStr(top, add);

			addLine.adds.push_back(beAdd);
		}
	}

	// 取出对当前cpp文件的分析结果
	void ParsingFile::TakeHistorys(FileHistoryMap &out) const
	{
		if (Project::IsCleanModeOpen(CleanMode_Need))
		{
			for (FileID top : m_files)
			{
				if (!IsUserFile(top))
				{
					continue;
				}

				string fileName = GetAbsoluteFileName(top);
				if (out.find(fileName) != out.end())
				{
					continue;
				}

				TakeNeed(top, out[fileName]);
			}

			TakeCompileErrorHistory(out);
		}
		else
		{
			// 先将当前cpp文件使用到的文件全存入map中
			for (FileID file : m_relys)
			{
				string fileName		= GetAbsoluteFileName(file);

				if (!Project::CanClean(fileName))
				{
					continue;
				}

				// 生成对应于该文件的的记录
				InitHistory(file, out[fileName]);
			}

			// 1. 将可清除的行按文件进行存放
			TakeUnusedLine(out);

			// 2. 将新增的前置声明按文件进行存放
			TakeForwardClass(out);

			// 3. 将可替换的#include按文件进行存放
			TakeReplace(out);

			// 4. 取出本文件的编译错误历史
			TakeCompileErrorHistory(out);

			// 5. 修复每个文件的历史，防止对同一行修改多次导致崩溃
			for (auto &itr : out)
			{
				FileHistory &history = itr.second;
				history.Fix();
			}
		}
	}

	// 将可清除的行按文件进行存放
	void ParsingFile::TakeUnusedLine(FileHistoryMap &out) const
	{
		for (SourceLocation loc : m_unusedLocs)
		{
			PresumedLoc presumedLoc = m_srcMgr->getPresumedLoc(loc);
			if (presumedLoc.isInvalid())
			{
				LogError("take_unused_line_by_file getPresumedLoc failed");
				continue;
			}

			string fileName			= pathtool::get_absolute_path(presumedLoc.getFilename());
			if (!Project::CanClean(fileName))
			{
				continue;
			}

			if (out.find(fileName) == out.end())
			{
				continue;
			}

			SourceRange lineRange	= GetCurLine(loc);
			SourceRange nextLine	= GetNextLine(loc);
			int line				= presumedLoc.getLine();

			FileHistory &eachFile	= out[fileName];
			DelLine &delLine		= eachFile.m_delLines[line];

			delLine.beg		= m_srcMgr->getFileOffset(lineRange.getBegin());
			delLine.end		= m_srcMgr->getFileOffset(nextLine.getBegin());
			delLine.text	= GetSourceOfRange(lineRange);

			LogInfoByLvl(LogLvl_2, "TakeUnusedLine [" << fileName << "]: [" << delLine.beg << "," << m_srcMgr->getFileOffset(lineRange.getEnd()) << ","
			             << delLine.end << "," << m_srcMgr->getFileOffset(nextLine.getEnd()) << "], text = [" << delLine.text << "]");
		}
	}

	// 将新增的前置声明按文件进行存放
	void ParsingFile::TakeForwardClass(FileHistoryMap &out) const
	{
		if (m_useRecords.empty())
		{
			return;
		}

		UseRecordsByFileMap forwards;
		SplitForwardByFile(forwards);

		for (auto &itr : forwards)
		{
			FileID file							= itr.first;
			const UseRecordsMap &locToRecords	= itr.second;

			string fileName		= GetAbsoluteFileName(file);

			if (out.find(fileName) == out.end())
			{
				continue;
			}

			for (auto &recordItr : locToRecords)
			{
				SourceLocation loc									= recordItr.first;
				const std::set<const CXXRecordDecl*> &cxxRecords	= recordItr.second;

				for (const CXXRecordDecl *cxxRecord : cxxRecords)
				{
					SourceLocation insertLoc = GetInsertForwardLine(file, *cxxRecord);
					if (insertLoc.isInvalid())
					{
						LogErrorByLvl(LogLvl_2, "insertLoc.isInvalid(), " << GetDebugFileName(file) << ", record = " << GetRecordName(*cxxRecord));
						continue;
					}

					PresumedLoc insertPresumedLoc = m_srcMgr->getPresumedLoc(insertLoc);
					if (insertPresumedLoc.isInvalid())
					{
						LogError("take_new_forwarddecl_by_file getPresumedLoc failed");
						continue;
					}

					string insertFileName = pathtool::get_absolute_path(insertPresumedLoc.getFilename());

					if (!Project::CanClean(insertFileName))
					{
						continue;
					}

					// 开始取出数据
					int line					= insertPresumedLoc.getLine();
					const string cxxRecordName	= GetRecordName(*cxxRecord);
					FileHistory &eachFile		= out[insertFileName];
					ForwardLine &forwardLine	= eachFile.m_forwards[line];

					forwardLine.offset		= m_srcMgr->getFileOffset(insertLoc);
					forwardLine.oldText		= GetSourceOfLine(insertLoc);
					forwardLine.classes.insert(cxxRecordName);

					SourceLocation fileStart = m_srcMgr->getLocForStartOfFile(GetFileID(insertLoc));
					if (fileStart.getLocWithOffset(forwardLine.offset) != insertLoc)
					{
						LogError("fileStart.getLocWithOffset(forwardLine.m_offsetAtFile) != insertLoc ");
					}
				}
			}
		}
	}

	// 将文件前置声明记录按文件进行归类
	void ParsingFile::SplitForwardByFile(UseRecordsByFileMap &forwards) const
	{
		for (auto &itr : m_useRecords)
		{
			SourceLocation loc		= itr.first;
			FileID file				= GetFileID(loc);

			if (file.isValid())
			{
				forwards[file].insert(itr);
			}
		}
	}

	// 该文件是否是被-include强制包含
	bool ParsingFile::IsForceIncluded(FileID file) const
	{
		if (file == m_srcMgr->getMainFileID())
		{
			return false;
		}

		FileID parent = GetFileID(m_srcMgr->getIncludeLoc(file));
		return (m_srcMgr->getFileEntryForID(parent) == nullptr);
	}

	// 该文件是否是预编译头文件
	bool ParsingFile::IsPrecompileHeader(FileID file) const
	{
		std::string filePath = GetAbsoluteFileName(file);

		std::string fileName = pathtool::get_file_name(filePath);

		bool is = !strnicmp(fileName.c_str(), "stdafx.h", fileName.size());
		is |= !strnicmp(fileName.c_str(), "stdafx.cpp", fileName.size());
		return is;
	}

	// 取出指定文件的#include替换信息
	void ParsingFile::TakeBeReplaceOfFile(FileHistory &history, FileID top, const ChildrenReplaceMap &childernReplaces) const
	{
		// 依次取出每行#include的替换信息[行号 -> 被替换成的#include列表]
		for (auto &itr : childernReplaces)
		{
			FileID oldFile		= itr.first;
			FileID replaceFile	= itr.second;

			bool isBeForceIncluded		= IsForceIncluded(oldFile);

			// 1. 该行的旧文本
			SourceLocation include_loc	= m_srcMgr->getIncludeLoc(oldFile);
			SourceRange	lineRange		= GetCurFullLine(include_loc);
			int line					= (isBeForceIncluded ? 0 : GetLineNo(include_loc));

			ReplaceLine &replaceLine	= history.m_replaces[line];
			replaceLine.oldFile			= GetAbsoluteFileName(oldFile);
			replaceLine.oldText			= GetIncludeFullLine(oldFile);
			replaceLine.beg				= m_srcMgr->getFileOffset(lineRange.getBegin());
			replaceLine.end				= m_srcMgr->getFileOffset(lineRange.getEnd());
			replaceLine.isSkip			= isBeForceIncluded || IsPrecompileHeader(oldFile);	// 记载是否是强制包含

			// 2. 该行可被替换成什么
			ReplaceTo &replaceTo	= replaceLine.replaceTo;

			SourceLocation deep_include_loc	= m_srcMgr->getIncludeLoc(replaceFile);

			// 记录[旧#include、新#include]
			replaceTo.oldText		= GetIncludeFullLine(replaceFile);
			replaceTo.newText		= GetRelativeIncludeStr(GetParent(oldFile), replaceFile);

			// 记录[所处的文件、所处行号]
			replaceTo.line			= GetLineNo(deep_include_loc);
			replaceTo.fileName		= GetAbsoluteFileName(replaceFile);
			replaceTo.inFile		= GetAbsoluteFileName(GetFileID(deep_include_loc));

			// 3. 该行依赖于其他文件的哪些行
			std::string replaceParentName	= GetAbsoluteFileName(GetParent(replaceFile));
			int replaceLineNo				= GetIncludeLineNo(replaceFile);

			if (Project::CanClean(replaceParentName))
			{
				replaceTo.m_rely[replaceParentName].insert(replaceLineNo);
			}

			auto childItr = m_relyChildren.find(replaceFile);
			if (childItr != m_relyChildren.end())
			{
				const std::set<FileID> &relys = childItr->second;

				for (FileID rely : relys)
				{
					std::string relyParentName	= GetAbsoluteFileName(GetParent(rely));
					int relyLineNo				= GetIncludeLineNo(rely);

					if (Project::CanClean(relyParentName))
					{
						replaceTo.m_rely[relyParentName].insert(relyLineNo);
					}
				}
			}
		}
	}

	// 取出各文件的#include替换信息
	void ParsingFile::TakeReplace(FileHistoryMap &out) const
	{
		if (m_replaces.empty())
		{
			return;
		}

		for (auto &itr : m_splitReplaces)
		{
			FileID top									= itr.first;
			const ChildrenReplaceMap &childrenReplaces	= itr.second;

			string fileName = GetAbsoluteFileName(top);

			if (out.find(fileName) == out.end())
			{
				continue;
			}

			// 新增替换记录
			string topFileName	= GetAbsoluteFileName(top);
			FileHistory &newFile	= out[topFileName];

			TakeBeReplaceOfFile(newFile, top, childrenReplaces);
		}
	}

	// 取出本文件的编译错误历史
	void ParsingFile::TakeCompileErrorHistory(FileHistoryMap &out) const
	{
		std::string rootFile			= GetAbsoluteFileName(m_srcMgr->getMainFileID());
		FileHistory &history			= out[rootFile];
		history.m_compileErrorHistory	= m_compileErrorHistory;
	}

	// 打印引用记录
	void ParsingFile::PrintUse() const
	{
		int num = 0;
		for (auto & use_list : m_uses)
		{
			FileID file = use_list.first;

			if (!CanClean(file))
			{
				continue;
			}

			++num;
		}

		HtmlDiv &div = HtmlLog::instance.m_newDiv;
		div.AddRow(AddPrintIdx() + ". list of use : use count = " + strtool::itoa(num), 1);

		for (auto & use_list : m_uses)
		{
			FileID file = use_list.first;

			if (!CanClean(file))
			{
				continue;
			}

			div.AddRow("file = " + DebugBeIncludeText(file), 2);

			for (FileID beuse : use_list.second)
			{
				div.AddRow("use = " + DebugBeIncludeText(beuse), 3);
			}

			div.AddRow("");
		}
	}

	// 打印#include记录
	void ParsingFile::PrintInclude() const
	{
		int num = 0;
		for (auto & includeList : m_includes)
		{
			FileID file = includeList.first;

			if (!CanClean(file))
			{
				continue;
			}

			++num;
		}

		HtmlDiv &div = HtmlLog::instance.m_newDiv;
		div.AddRow(AddPrintIdx() + ". list of include : include count = " + htmltool::get_number_html(num), 1);

		for (auto & includeList : m_includes)
		{
			FileID file = includeList.first;

			if (!CanClean(file))
			{
				continue;
			}

			div.AddRow("file = " + DebugBeIncludeText(file), 2);

			for (FileID beinclude : includeList.second)
			{
				div.AddRow("include = " + DebugBeIncludeText(beinclude), 3);
			}

			div.AddRow("");
		}
	}

	// 打印引用类名、函数名、宏名等的记录
	void ParsingFile::PrintUsedNames() const
	{
		int num = 0;
		for (auto & useItr : m_useNames)
		{
			FileID file = useItr.first;

			if (!CanClean(file))
			{
				continue;
			}

			++num;
		}

		HtmlDiv &div = HtmlLog::instance.m_newDiv;
		div.AddRow(AddPrintIdx() + ". list of use name : use count = " + htmltool::get_number_html(num), 1);

		for (auto & useItr : m_useNames)
		{
			FileID file									= useItr.first;
			const std::vector<UseNameInfo> &useNames	= useItr.second;

			if (!CanClean(file))
			{
				continue;
			}

			DebugUsedNames(file, useNames);
		}
	}

	// 获取文件所使用名称信息：文件名、所使用的类名、函数名、宏名等以及对应行号
	void ParsingFile::DebugUsedNames(FileID file, const std::vector<UseNameInfo> &useNames) const
	{
		HtmlDiv &div = HtmlLog::instance.m_newDiv;
		div.AddRow("file = " + DebugBeIncludeText(file), 2);

		for (const UseNameInfo &beuse : useNames)
		{
			div.AddRow("use = " + DebugBeIncludeText(beuse.file), 3);

			for (const string& name : beuse.nameVec)
			{
				std::stringstream linesStream;

				auto & linesItr = beuse.nameMap.find(name);
				if (linesItr != beuse.nameMap.end())
				{
					for (int line : linesItr->second)
					{
						linesStream << htmltool::get_number_html(line) << ", ";
					}
				}

				std::string linesText = linesStream.str();

				strtool::try_strip_right(linesText, std::string(", "));

				div.AddRow("name = " + name, 4, 70);
				div.AddGrid("lines = " + linesText, 30);
			}

			div.AddRow("");
		}
	}

	// 是否有必要打印该文件
	bool ParsingFile::IsNeedPrintFile(FileID file) const
	{
		if (CanClean(file))
		{
			return true;
		}

		FileID parent = GetParent(file);
		if (CanClean(parent))
		{
			return true;
		}

		return false;
	}

	// 获取属于本项目的允许被清理的文件数
	int ParsingFile::GetCanCleanFileCount() const
	{
		int num = 0;
		for (FileID file : m_relys)
		{
			if (!CanClean(file))
			{
				continue;
			}

			++num;
		}

		return num;
	}

	// 打印主文件的依赖文件集
	void ParsingFile::PrintTopRely() const
	{
		int num = 0;

		for (auto & file : m_topRelys)
		{
			if (!IsNeedPrintFile(file))
			{
				continue;
			}

			++num;
		}

		HtmlDiv &div = HtmlLog::instance.m_newDiv;
		div.AddRow(AddPrintIdx() + ". list of top rely: file count = " + htmltool::get_number_html(num), 1);

		for (auto & file : m_topRelys)
		{
			if (!IsNeedPrintFile(file))
			{
				continue;
			}

			div.AddRow("file = " + DebugBeIncludeText(file), 2);
		}

		div.AddRow("");
	}

	// 打印依赖文件集
	void ParsingFile::PrintRely() const
	{
		int num = 0;
		for (auto & file : m_relys)
		{
			if (!IsNeedPrintFile(file))
			{
				continue;
			}

			++num;
		}

		HtmlDiv &div = HtmlLog::instance.m_newDiv;
		div.AddRow(AddPrintIdx() + ". list of rely : file count = " + htmltool::get_number_html(num), 1);

		for (auto & file : m_relys)
		{
			if (!IsNeedPrintFile(file))
			{
				continue;
			}

			if (!IsRelyByTop(file))
			{
				div.AddRow("<--------- new add rely file --------->", 2, 100, true);
			}

			div.AddRow("file = " + DebugBeIncludeText(file), 2);
		}

		div.AddRow("");
	}

	// 打印各文件对应的有用孩子文件记录
	void ParsingFile::PrintRelyChildren() const
	{
		int num = 0;
		for (auto & usedItr : m_relyChildren)
		{
			FileID file = usedItr.first;

			if (!CanClean(file))
			{
				continue;
			}

			++num;
		}

		HtmlDiv &div = HtmlLog::instance.m_newDiv;
		div.AddRow(AddPrintIdx() + ". list of rely children file: file count = " + htmltool::get_number_html(num), 1);

		for (auto & usedItr : m_relyChildren)
		{
			FileID file = usedItr.first;

			if (!CanClean(file))
			{
				continue;
			}

			div.AddRow("file = " + DebugBeIncludeText(file), 2);

			for (auto & usedChild : usedItr.second)
			{
				div.AddRow("use children " + DebugBeIncludeText(usedChild), 3);
			}

			div.AddRow("");
		}
	}

	// 打印可转为前置声明的类指针或引用记录
	void ParsingFile::PrintForwardDecl() const
	{
		UseRecordsByFileMap forwards;
		SplitForwardByFile(forwards);

		HtmlDiv &div = HtmlLog::instance.m_newDiv;
		div.AddRow(AddPrintIdx() + ". maybe can forward decl list: file count = " + htmltool::get_number_html(forwards.size()), 1);

		for (auto &itr : forwards)
		{
			FileID file = itr.first;
			const UseRecordsMap &locToRecords = itr.second;

			div.AddRow("fileName = " + DebugBeIncludeText(file), 2);

			for (auto &recordItr : locToRecords)
			{
				SourceLocation loc = recordItr.first;
				const std::set<const CXXRecordDecl*> &cxxRecords = recordItr.second;

				div.AddRow("at loc = " + DebugLocText(loc), 3);

				for (const CXXRecordDecl *record : cxxRecords)
				{
					div.AddRow("use record = " + GetRecordName(*record), 4);
				}

				div.AddRow("");
			}
		}
	}

	// 打印允许被清理的所有文件列表
	void ParsingFile::PrintAllFile() const
	{
		int num = 0;
		for (FileID file : m_files)
		{
			if (!CanClean(file))
			{
				continue;
			}

			++num;
		}

		HtmlDiv &div = HtmlLog::instance.m_newDiv;
		div.AddRow(AddPrintIdx() + ". list of all file: file count = " + htmltool::get_number_html(num), 1);

		for (FileID file : m_files)
		{
			const string &name = GetAbsoluteFileName(file);

			if (!Project::CanClean(name))
			{
				continue;
			}

			div.AddRow("file = " + DebugBeIncludeText(file), 2);
		}

		div.AddRow("");
	}

	// 打印清理日志
	void ParsingFile::PrintCleanLog() const
	{
		HtmlDiv &div = HtmlLog::instance.m_newDiv;

		m_compileErrorHistory.Print();

		int i = 0;

		for (auto &itr : m_historys)
		{
			const FileHistory &history = itr.second;
			if (!history.IsNeedClean())
			{
				continue;
			}

			if (Project::instance.m_logLvl < LogLvl_3)
			{
				string ext = strtool::get_ext(history.m_filename);
				if (!cpptool::is_cpp(ext))
				{
					continue;
				}
			}

			history.Print(++i, false);
		}
	}

	// 打印各文件内的命名空间
	void ParsingFile::PrintNamespace() const
	{
		int num = 0;
		for (auto &itr : m_namespaces)
		{
			FileID file = itr.first;

			if (!IsNeedPrintFile(file))
			{
				continue;
			}

			++num;
		}

		HtmlDiv &div = HtmlLog::instance.m_newDiv;
		div.AddRow(AddPrintIdx() + ". each file's namespace: file count = " + htmltool::get_number_html(num), 1);

		for (auto &itr : m_namespaces)
		{
			FileID file = itr.first;

			if (!IsNeedPrintFile(file))
			{
				continue;
			}

			div.AddRow("file = " + DebugBeIncludeText(file), 2);

			const std::set<std::string> &namespaces = itr.second;

			for (const std::string &ns : namespaces)
			{
				div.AddRow("declare namespace = " + htmltool::get_include_html(ns), 3);
			}

			div.AddRow("");
		}
	}

	// 打印各文件内应保留的using namespace
	void ParsingFile::PrintUsingNamespace() const
	{
		std::map<FileID, std::set<std::string>>	nsByFile;
		for (auto &itr : m_usingNamespaces)
		{
			SourceLocation loc		= itr.first;
			const NamespaceDecl	*ns	= itr.second;

			nsByFile[GetFileID(loc)].insert(ns->getQualifiedNameAsString());
		}

		int num = 0;
		for (auto &itr : nsByFile)
		{
			FileID file = itr.first;

			if (!IsNeedPrintFile(file))
			{
				continue;
			}

			++num;
		}

		HtmlDiv &div = HtmlLog::instance.m_newDiv;
		div.AddRow(AddPrintIdx() + ". each file's using namespace: file count = " + htmltool::get_number_html(num), 1);

		for (auto &itr : nsByFile)
		{
			FileID file = itr.first;

			if (!IsNeedPrintFile(file))
			{
				continue;
			}

			div.AddRow("file = " + DebugBeIncludeText(file), 2);

			std::set<std::string> &namespaces = itr.second;

			for (const std::string &ns : namespaces)
			{
				div.AddRow("using namespace = " + htmltool::get_include_html(ns), 3);
			}

			div.AddRow("");
		}
	}

	// 打印被包含多次的文件
	void ParsingFile::PrintSameFile() const
	{
		if (m_sameFiles.empty())
		{
			return;
		}

		HtmlDiv &div = HtmlLog::instance.m_newDiv;
		div.AddRow(AddPrintIdx() + ". same file list: file count = " + htmltool::get_number_html(m_sameFiles.size()), 1);

		for (auto &itr : m_sameFiles)
		{
			const std::string &fileName			= itr.first;
			const std::set<FileID> sameFiles	= itr.second;

			div.AddRow("fileName = " + fileName, 2);

			for (FileID sameFile : sameFiles)
			{
				div.AddRow("same file = " + DebugBeIncludeText(sameFile), 3);
			}

			div.AddRow("");
		}
	}

	// 打印
	void ParsingFile::PrintMinUse() const
	{
		HtmlDiv &div = HtmlLog::instance.m_newDiv;
		div.AddRow(strtool::get_text(cn_file_min_use, htmltool::get_number_html(++m_printIdx).c_str(), htmltool::get_number_html(m_min.size()).c_str()), 1);

		for (auto & kidItr : m_min)
		{
			FileID by = kidItr.first;

			if (!CanClean(by))
			{
				continue;
			}

			div.AddRow("file = " + DebugBeIncludeText(by), 2);

			for (FileID kid : kidItr.second)
			{
				div.AddRow("min use = " + DebugBeIncludeText(kid), 3);
			}

			div.AddRow("");
		}
	}

	void ParsingFile::PrintMinKid() const
	{
		HtmlDiv &div = HtmlLog::instance.m_newDiv;
		div.AddRow(strtool::get_text(cn_file_min_kid, htmltool::get_number_html(++m_printIdx).c_str(), htmltool::get_number_html(m_minKids.size()).c_str()), 1);

		for (auto & kidItr : m_minKids)
		{
			FileID by = kidItr.first;

			if (!CanClean(by))
			{
				continue;
			}

			div.AddRow("file = " + DebugBeIncludeText(by), 2);

			for (FileID kid : kidItr.second)
			{
				div.AddRow("min kid = " + DebugBeIncludeText(kid), 3);
			}

			div.AddRow("");
		}
	}

	// 打印
	void ParsingFile::PrintOutFileAncestor() const
	{
		HtmlDiv &div = HtmlLog::instance.m_newDiv;
		div.AddRow(strtool::get_text(cn_file_sys_ancestor, htmltool::get_number_html(++m_printIdx).c_str(), htmltool::get_number_html(m_outFileAncestor.size()).c_str()), 1);

		for (auto &itr : m_outFileAncestor)
		{
			FileID kid		= itr.first;
			FileID ancestor	= itr.second;

			div.AddRow("kid  = " + DebugBeIncludeText(kid), 2);
			div.AddRow("out file ancestor = " + DebugBeIncludeText(ancestor), 3);

			div.AddRow("");
		}
	}

	void ParsingFile::PrintUserUse() const
	{
		HtmlDiv &div = HtmlLog::instance.m_newDiv;
		div.AddRow(strtool::get_text(cn_file_user_use, htmltool::get_number_html(++m_printIdx).c_str(), htmltool::get_number_html(m_userUses.size()).c_str()), 1);

		for (auto &itr : m_userUses)
		{
			FileID file = itr.first;

			div.AddRow("user file = " + DebugBeIncludeText(file), 2);

			for (FileID beuse : itr.second)
			{
				div.AddRow("user use = " + DebugBeIncludeText(beuse), 3);
			}

			div.AddRow("");
		}
	}
}