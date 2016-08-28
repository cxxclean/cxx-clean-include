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

		// cxx::log() << "ParsingFile::AddIncludeLoc = " << GetSourceOfLine(GetExpasionLoc(loc)) << "\n";

		m_includeLocs[GetExpasionLoc(loc)] = range;
	}

	// 添加成员文件
	void ParsingFile::AddFile(FileID file)
	{
		if (file.isValid())
		{
			m_files.insert(file);
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

		for (auto & itr : include_dirs_map)
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

	// 生成结果前的准备
	void ParsingFile::PrepareResult()
	{
		// 1. 首先生成每个文件的后代文件
		for (FileID file : m_files)
		{
			for (FileID child = file, parent; (parent = GetParent(child)).isValid(); child = parent)
			{
				m_children[parent].insert(file);
			}
		}

		// 2. 扩充引用文件集，主要是为了达到这个目标：每个文件优先保留自己文件内的#include语句
		for (auto & itr : m_uses)
		{
			FileID file							= itr.first;
			const std::set<FileID> &beuseList	= itr.second;

			if (!CanClean(file))
			{
				continue;
			}

			for (FileID beuse : beuseList)
			{
				if (!IsIncludedBy(beuse, file))
				{
					UseByFileName(file, GetAbsoluteFileName(beuse).c_str());
				}
			}
		}
	}

	// 生成各文件的待清理记录
	void ParsingFile::GenerateResult()
	{
		PrepareResult();

		GenerateRely();

		GenerateMove();
		GenerateUnusedInclude();
		GenerateForwardClass();
		GenerateReplace();

		Fix();

		ProjectHistory::instance.AddFile(this);
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
		if (IsBeRely(top))
		{
			return FileID();
		}

		auto & itr = m_relyChildren.find(top);
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

		for (auto & itr = m_relys.rbegin(); itr != m_relys.rend(); ++itr)
		{
			FileID file = *itr;

			for (FileID top = mainFile, lv2Top; top != file; top = lv2Top)
			{
				lv2Top = GetLvl2Ancestor(file, top);

				if (lv2Top == file)
				{
					break;
				}

				if (IsBeRely(lv2Top))
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

						// cxx::log() << "fatal: there is replace cleared!\n";
						return true;
					}

					break;
				}

				FileID canReplaceTo;

				// 仅[替换#include]清理选项开启时，才尝试替换
				if (Project::instance.IsCleanModeOpen(CleanMode_Replace))
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

	// 将文件b转移到文件a中
	void ParsingFile::AddMove(FileID a, FileID b)
	{
		const std::string bName = GetAbsoluteFileName(b);

		FileID sameFile = GetDirectChildByName(a, bName.c_str());
		if (sameFile.isValid())
		{
			//return;
		}

		FileID replaceTo = b;

		auto & itr = m_replaces.find(b);
		if (itr != m_replaces.end())
		{
			replaceTo = itr->second;
		}

		m_moves[a].insert(std::make_pair(b, replaceTo));
	}

	// 根据主文件的依赖关系，生成相关文件的依赖文件集
	void ParsingFile::GenerateRely()
	{
		/*
			下面这段代码是本工具的主要处理思路
		*/

		// 1. 获取主文件的循环引用文件集
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
	bool ParsingFile::IsBeRely(FileID file) const
	{
		return m_relys.find(file) != m_relys.end();
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

	// 生成可被移动的文件记录
	void ParsingFile::GenerateMove()
	{
		// 仅[移动#include]清理选项开启时，才尝试移动
		if (!Project::instance.IsCleanModeOpen(CleanMode_Move))
		{
			return;
		}

		for (auto & itr : m_includes)
		{
			FileID includeBy	= itr.first;
			auto &includeList	= itr.second;

			if (IsSkip(includeBy))
			{
				continue;
			}

			for (FileID beInclude : includeList)
			{
				if (IsUse(includeBy, beInclude))
				{
					continue;
				}

				for (auto & useItr : m_uses)
				{
					FileID useby	= useItr.first;

					if (!IsBeRely(useby))
					{
						continue;
					}

					if (!Project::instance.CanClean(GetAbsoluteFileName(useby)))
					{
						continue;
					}

					if (!IsAncestor(beInclude, useby))
					{
						continue;
					}

					auto &useList	= useItr.second;

					if (useList.find(beInclude) != useList.end())
					{
						AddMove(useby, beInclude);
					}
					else
					{
						for (FileID beuse : useList)
						{
							if (IsAncestor(beuse, beInclude))
							{
								AddMove(useby, beInclude);
								break;
							}
						}
					}
				}
			}
		}
	}

	// 生成无用#include的记录
	void ParsingFile::GenerateUnusedInclude()
	{
		// 仅[删除无用#include]清理选项开启时，才允许继续
		if (!Project::instance.IsCleanModeOpen(CleanMode_Unused))
		{
			return;
		}

		for (auto & locItr : m_includeLocs)
		{
			SourceLocation loc = locItr.first;
			FileID file = GetFileID(loc);

			if (!IsBeRely(file))
			{
				continue;
			}

			if (!IsLocBeUsed(loc))
			{
				m_unusedLocs.insert(loc);
			}
		}

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
		auto & itr = m_uses.find(a);
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
		auto & itr = m_includes.find(a);
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

		// cxx::log() << "ancestor = " << get_file_name(ancestor) << "\n";

		// 获取属于该文件的后代中被主文件使用的一部分
		for (FileID child : all_children)
		{
			if (!IsAncestor(child, ancestor))
			{
				continue;
			}

			// cxx::log() << "    child = " << get_file_name(child) << "\n";
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
		// 1. 删除一些不必要的替换
		std::map<FileID, FileID> copy = m_replaces;
		for (auto & itr : copy)
		{
			FileID from	= itr.first;
			FileID to	= itr.second;

			if (IsMoved(from))
			{
				m_replaces.erase(from);
			}
		}

		// 2. 将文件替换记录按父文件进行归类
		SplitReplaceByFile();
	}

	// 当前文件之前是否已有文件声明了该class、struct、union
	bool ParsingFile::HasRecordBefore(FileID cur, const CXXRecordDecl &cxxRecord) const
	{
		FileID recordAtFile = GetFileID(cxxRecord.getLocStart());

		// 若类所在的文件被引用，则不需要再加前置声明
		if (IsBeRely(recordAtFile))
		{
			return true;
		}

		// 否则，说明类所在的文件未被引用
		// 1. 此时，需查找类定义之前的所有文件中，是否已有该类的前置声明
		for (const CXXRecordDecl *prev = cxxRecord.getPreviousDecl(); prev; prev = prev->getPreviousDecl())
		{
			FileID prevFileId = GetFileID(prev->getLocation());
			if (!IsBeRely(prevFileId))
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
			if (!IsBeRely(redeclFileID))
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

	// 文件a是否被转移
	bool ParsingFile::IsMoved(FileID a) const
	{
		for (auto & itr : m_moves)
		{
			auto &beMoveList = itr.second;
			if (beMoveList.find(a) != beMoveList.end())
			{
				return true;
			}
		}

		return false;
	}

	// 获取文件a将被转移哪些文件中
	std::set<FileID> ParsingFile::GetMoveToList(FileID a) const
	{
		std::set<FileID> moveToList;

		for (auto & itr : m_moves)
		{
			FileID moveTo		= itr.first;
			auto &beMoveList	= itr.second;

			for (auto & moveItr : beMoveList)
			{
				FileID beMove = moveItr.first;

				if (IsAncestor(a, beMove))
				{
					moveToList.insert(moveTo);
				}
			}
		}

		return moveToList;
	}

	// 当前文件是否应新增class、struct、union的前置声明
	bool ParsingFile::IsNeedClass(FileID cur, const CXXRecordDecl &cxxRecord) const
	{
		FileID recordAtFile = GetFileID(cxxRecord.getLocStart());

		// 分3种情况，令类所在的文件为A
		// 1. 若A未被引用，则肯定要加前置声明
		if (!IsBeRely(recordAtFile))
		{
			return true;
		}

		std::set<FileID> moveToList = GetMoveToList(recordAtFile);

		// 2. 若A被引用，但A未被转移过
		if (moveToList.empty())
		{
			return false;
		}
		// 3. 若A被引用，但A将被转移
		else
		{
			for (FileID moveTo : moveToList)
			{
				if (moveTo == cur)
				{
					return false;
				}

				if (IsAncestor(moveTo, cur))
				{
					return false;
				}
			}

			return true;
		}

		return false;
	}

	// 生成新增前置声明列表
	void ParsingFile::GenerateForwardClass()
	{
		// 清除一些不必要保留的前置声明
		for (auto & itr = m_forwardDecls.begin(), end = m_forwardDecls.end(); itr != end;)
		{
			FileID file										= itr->first;
			std::set<const CXXRecordDecl*> &old_forwards	= itr->second;

			if (!IsBeRely(file))
			{
				m_forwardDecls.erase(itr++);
				continue;
			}

			++itr;

			std::set<const CXXRecordDecl*> can_forwards;

			for (const CXXRecordDecl* cxxRecordDecl : old_forwards)
			{
				if (IsNeedClass(file, *cxxRecordDecl))
				{
					can_forwards.insert(cxxRecordDecl);
				}
			}

			if (can_forwards.size() < old_forwards.size())
			{
				old_forwards = can_forwards;
			}
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
			llvm::errs() << "[error][ParsingFile::GetSourceOfRange] if (range.getEnd() < range.getBegin())\n";
			return "";
		}

		if (!m_srcMgr->isWrittenInSameFile(range.getBegin(), range.getEnd()))
		{
			llvm::errs() << "[error][ParsingFile::GetSourceOfRange] if (!m_srcMgr->isWrittenInSameFile(range.getBegin(), range.getEnd()))\n";
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
			/*
			PresumedLoc presumedLoc = m_srcMgr->getPresumedLoc(loc);
			if (presumedLoc.isValid())
			{
				llvm::errs() << "[error][ParsingFile::GetSourceAtLoc]! err = " << err << "at file = " << presumedLoc.getFilename() << ", line = " << GetLineNo(loc) << "\n";
			}
			else
			{
				llvm::errs() << "[error][ParsingFile::GetSourceAtLoc]! err = " << err << "\n";
			}
			*/

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

	/*
		根据传入的代码位置返回该行的范围：[该行开头，该行末（不含换行符） + 1]
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
	SourceRange ParsingFile::GetCurLineWithLinefeed(SourceLocation loc) const
	{
		SourceRange curLine		= GetCurLine(loc);
		SourceRange nextLine	= GetNextLine(loc);

		return SourceRange(curLine.getBegin(), nextLine.getBegin());
	}

	// 根据传入的代码位置返回下一行的范围
	SourceRange ParsingFile::GetNextLine(SourceLocation loc) const
	{
		SourceRange curLine		= GetCurLine(loc);
		SourceLocation lineEnd	= curLine.getEnd();

		const char* c1			= GetSourceAtLoc(lineEnd);
		const char* c2			= GetSourceAtLoc(lineEnd.getLocWithOffset(1));

		if (nullptr == c1 || nullptr == c2)
		{
			SourceLocation fileEndLoc	= m_srcMgr->getLocForEndOfFile(GetFileID(loc));
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
		return GetCurLineWithLinefeed(include_loc);
	}

	// 是否是换行符
	bool ParsingFile::IsNewLineWord(SourceLocation loc) const
	{
		string text = GetSourceOfRange(SourceRange(loc, loc.getLocWithOffset(1)));
		return text == "\r" || text == "\n";
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
	std::string ParsingFile::GetIncludeText(FileID file) const
	{
		SourceLocation loc = m_srcMgr->getIncludeLoc(file);

		auto & itr = m_includeLocs.find(loc);
		if (itr == m_includeLocs.end())
		{
			return "";
		}

		SourceRange range = itr->second;
		std::string text = GetSourceOfRange(range);

		if (text.empty())
		{
			text = GetSourceOfLine(range.getBegin());
		}

		return text;
	}

	// cur位置的代码使用src位置的代码
	void ParsingFile::Use(SourceLocation cur, SourceLocation src, const char* name /* = nullptr */)
	{
		cur = GetExpasionLoc(cur);
		src = GetExpasionLoc(src);

		FileID curFileID = GetFileID(cur);
		FileID srcFileID = GetFileID(src);

		UseInclude(curFileID, srcFileID, name, GetLineNo(cur));
	}

	void ParsingFile::UseName(FileID file, FileID beusedFile, const char* name /* = nullptr */, int line)
	{
		if (Project::instance.m_verboseLvl < VerboseLvl_3)
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
		for (auto & itr : m_parents)
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

		auto & itr = m_parents.find(child);
		if (itr == m_parents.end())
		{
			return FileID();
		}

		return itr->second;
	}

	// 当前文件使用目标文件
	void ParsingFile::UseInclude(FileID file, FileID beusedFile, const char* name /* = nullptr */, int line)
	{
		if (file == beusedFile)
		{
			return;
		}

		if (file.isInvalid() || beusedFile.isInvalid())
		{
			return;
		}

		// 这段先注释掉
		const FileEntry *rootFileEntry		= m_srcMgr->getFileEntryForID(file);
		const FileEntry *beusedFileEntry	= m_srcMgr->getFileEntryForID(beusedFile);

		if (nullptr == rootFileEntry || nullptr == beusedFileEntry)
		{
			// cxx::log() << "------->error: use_include() : m_srcMgr->getFileEntryForID(file) failed!" << m_srcMgr->getFilename(m_srcMgr->getLocForStartOfFile(file)) << ":" << m_srcMgr->getFilename(m_srcMgr->getLocForStartOfFile(beusedFile)) << "\n";
			// cxx::log() << "------->error: use_include() : m_srcMgr->getFileEntryForID(file) failed!" << get_source_of_line(m_srcMgr->getLocForStartOfFile(file)) << ":" << get_source_of_line(m_srcMgr->getLocForStartOfFile(beusedFile)) << "\n";
			return;
		}

		if (GetAbsoluteFileName(file) == GetAbsoluteFileName(beusedFile))
		{
			return;
		}

		m_uses[file].insert(beusedFile);
		UseName(file, beusedFile, name, line);
	}

	// 文件a使用指定名称的目标文件
	void ParsingFile::UseByFileName(FileID a, const char* filename)
	{
		auto &itr = m_uses.find(a);
		if (itr == m_uses.end())
		{
			llvm::errs() << "[error][ParsingFile::UseByFileName] if (itr == m_uses.end())";
			return;
		}

		FileID child = GetDirectChildByName(a, filename);
		if (child.isInvalid())
		{
			child = GetChildByName(a, filename);

			if (child.isInvalid())
			{
				// 这种情况很正常，直接返回
				return;
			}
		}

		std::set<FileID> &useList = itr->second;
		if (useList.find(child) == useList.end())
		{
			useList.insert(child);

			if (Project::instance.m_verboseLvl >= VerboseLvl_3)
			{
				m_newUse[a].insert(child);
				return;
			}
		}
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

		// cxx::log() << "macro id = " << macroName.getIdentifierInfo()->getName().str() << "\n";
		Use(loc, info->getDefinitionLoc(), macroName.c_str());
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

		std::string nsName		= GetNestedNamespace(ns);
		SourceLocation nsLoc	= ns->getLocStart();

		for (auto &itr : m_usingNamespaces)
		{
			SourceLocation using_loc	= itr.first;
			const NamespaceInfo &ns		= itr.second;

			if (nsName == ns.decl)
			{
				std::string usingNs		= "using namespace " + ns.ns->getQualifiedNameAsString() + ";";
				Use(loc, using_loc, usingNs.c_str());
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
		if (Project::instance.m_verboseLvl < VerboseLvl_3)
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
		SourceLocation loc = GetSpellingLoc(d->getLocation());

		FileID file = GetFileID(loc);
		if (file.isInvalid())
		{
			return;
		}

		const NamespaceDecl *ns	= d->getNominatedNamespace();

		NamespaceInfo &nsInfo = m_usingNamespaces[loc];
		nsInfo.decl	= GetNestedNamespace(ns);
		nsInfo.ns	= ns;

		// 引用命名空间所在的文件（注意：using namespace时必须能找到对应的namespace声明，比如，using namespace A前一定要有namespace A{}否则编译会报错）
		Use(loc, ns->getLocation(), nsInfo.decl.c_str());
	}

	// using了命名空间下的某类，比如：using std::string;
	void ParsingFile::UsingXXX(const UsingDecl *d)
	{
		SourceLocation usingLoc		= d->getUsingLoc();

		for (auto & itr = d->shadow_begin(); itr != d->shadow_end(); ++itr)
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

		string name;// = "namespace " + d->getNameAsString() + "{}";

		while (d)
		{
			string namespaceName = "namespace " + d->getNameAsString();
			name = namespaceName + "{" + name + "}";

			const DeclContext *parent = d->getParent();
			if (parent && parent->isNamespace())
			{
				d = cast<NamespaceDecl>(parent);
			}
			else
			{
				break;
			}
		}

		return name;
	}

	// 文件b是否直接#include文件a
	bool ParsingFile::IsIncludedBy(FileID a, FileID b)
	{
		auto & itr = m_includes.find(b);
		if (itr == m_includes.end())
		{
			return false;
		}

		const std::set<FileID> &includes = itr->second;
		return includes.find(a) != includes.end();
	}

	// 获取文件a的指定名称的直接后代文件
	FileID ParsingFile::GetDirectChildByName(FileID a, const char* childFileName)
	{
		auto & itr = m_includes.find(a);
		if (itr == m_includes.end())
		{
			return FileID();
		}

		const std::set<FileID> &includes = itr->second;
		for (FileID child : includes)
		{
			if (GetAbsoluteFileName(child) == childFileName)
			{
				return child;
			}
		}

		return FileID();
	}

	// 获取文件a的指定名称的后代文件
	FileID ParsingFile::GetChildByName(FileID a, const char* childFileName)
	{
		auto & itr = m_children.find(a);
		if (itr == m_children.end())
		{
			return FileID();
		}

		const std::set<FileID> &children = itr->second;
		for (FileID child : children)
		{
			if (GetAbsoluteFileName(child) == childFileName)
			{
				return child;
			}
		}

		return FileID();
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
				// llvm::errs() << "------------ TemplateTypeParmType dump ------------:\n";
				// llvm::errs() << "------------ templateTypeParmType->getDecl()->dumpColor() ------------\n";
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
			// llvm::errs() << "------------ ParenType dump ------------:\n";
			// llvm::errs() << "------------ parenType->getInnerType().dump() ------------:\n";
			UseQualType(loc, parenType->getInnerType());
		}
		else if (isa<InjectedClassNameType>(t))
		{
			const InjectedClassNameType *injectedClassNameType = cast<InjectedClassNameType>(t);
			// llvm::errs() << "------------InjectedClassNameType dump:\n";
			// llvm::errs() << "------------injectedClassNameType->getInjectedSpecializationType().dump():\n";
			UseQualType(loc, injectedClassNameType->getInjectedSpecializationType());
		}
		else if (isa<PackExpansionType>(t))
		{
			const PackExpansionType *packExpansionType = cast<PackExpansionType>(t);
			// llvm::errs() << "\n------------PackExpansionType------------\n";
			UseQualType(loc, packExpansionType->getPattern());
		}
		else if (isa<DecltypeType>(t))
		{
			const DecltypeType *decltypeType = cast<DecltypeType>(t);
			// llvm::errs() << "\n------------DecltypeType------------\n";
			UseQualType(loc, decltypeType->getUnderlyingType());
		}
		else if (isa<DependentNameType>(t))
		{
			//				const DependentNameType *dependentNameType = cast<DependentNameType>(t);
			//				llvm::errs() << "\n------------DependentNameType------------\n";
			//				llvm::errs() << "\n------------dependentNameType->getQualifier()->dump()------------\n";
			//				dependentNameType->getQualifier()->dump();
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
			// llvm::errs() << "-------------- haven't support type --------------\n";
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

	// 新增使用前置声明记录
	void ParsingFile::UseForward(SourceLocation loc, const CXXRecordDecl *cxxRecordDecl)
	{
		if (nullptr == cxxRecordDecl)
		{
			return;
		}

		FileID file = GetFileID(loc);
		if (file.isInvalid())
		{
			return;
		}

		// 添加文件所使用的前置声明记录（对于不必要添加的前置声明将在之后进行清理）
		m_forwardDecls[file].insert(cxxRecordDecl);
	}

	// 是否为可前置声明的类型
	bool ParsingFile::IsForwardType(const QualType &var)
	{
		if (!var->isPointerType() && !var->isReferenceType())
		{
			return false;
		}

		if (isa<TypedefType>(var))
		{
			return false;
		}

		QualType pointeeType = var->getPointeeType();

		// 如果是指针类型就获取其指向类型(PointeeType)
		while (pointeeType->isPointerType() || pointeeType->isReferenceType())
		{
			pointeeType = pointeeType->getPointeeType();
		}

		if (!isa<RecordType>(pointeeType))
		{
			return false;
		}

		if (!pointeeType->isRecordType())
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

		QualType pointeeType = var->getPointeeType();
		while (pointeeType->isPointerType() || pointeeType->isReferenceType())
		{
			pointeeType = pointeeType->getPointeeType();
		}

		const CXXRecordDecl *cxxRecordDecl = pointeeType->getAsCXXRecordDecl();
		UseForward(loc, cxxRecordDecl);
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
			UseQualType(loc, arg.getAsType());
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
	}

	// 是否允许清理该c++文件（若不允许清理，则文件内容不会有任何变化）
	bool ParsingFile::CanClean(FileID file) const
	{
		return Project::instance.CanClean(GetAbsoluteFileName(file));
	}

	// 打印各文件的父文件
	void ParsingFile::PrintParent()
	{
		int num = 0;
		for (auto & itr : m_parents)
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

		for (auto & itr : m_parents)
		{
			FileID child	= itr.first;
			FileID parent	= itr.second;

			// 仅打印跟项目内文件有直接关联的文件
			if (!CanClean(child) && !CanClean(parent))
			{
				continue;
			}

			div.AddRow(htmltool::get_file_html(GetAbsoluteFileName(child)), 2, 50);
			div.AddGrid("parent = ", 10);
			div.AddGrid(htmltool::get_file_html(GetAbsoluteFileName(parent)));
		}

		div.AddRow("");
	}

	// 打印各文件的孩子文件
	void ParsingFile::PrintChildren()
	{
		int num = 0;
		for (auto & itr : m_children)
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

		for (auto & itr : m_children)
		{
			FileID parent = itr.first;

			if (!CanClean(parent))
			{
				continue;
			}

			div.AddRow("file = " + htmltool::get_file_html(GetAbsoluteFileName(parent)), 2);

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

	// 打印新增的引用关系
	void ParsingFile::PrintNewUse()
	{
		int num = 0;
		for (auto & itr : m_newUse)
		{
			FileID file = itr.first;

			if (!CanClean(file))
			{
				continue;
			}

			++num;
		}

		HtmlDiv &div = HtmlLog::instance.m_newDiv;
		div.AddRow(AddPrintIdx() + ". list of new use : use count = " + strtool::itoa(num), 1);

		for (auto & itr : m_newUse)
		{
			FileID file = itr.first;

			if (!CanClean(file))
			{
				continue;
			}

			div.AddRow("file = " + htmltool::get_file_html(GetAbsoluteFileName(file)), 2);

			for (FileID newuse : itr.second)
			{
				div.AddRow("new use = " + DebugBeIncludeText(newuse), 3);
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
			text = strtool::get_text(cn_main_file_debug_text, htmltool::get_file_html(fileName).c_str(), file.getHashValue());
		}
		else
		{
			SourceLocation loc				= m_srcMgr->getIncludeLoc(file);
			PresumedLoc parentPresumedLoc	= m_srcMgr->getPresumedLoc(loc);
			string includeToken				= GetIncludeText(file);
			string parentFileName			= pathtool::get_absolute_path(parentPresumedLoc.getFilename());

			if (includeToken.empty())
			{
				includeToken = "empty";
			}

			text = strtool::get_text(cn_file_debug_text,
			                         htmltool::get_file_html(fileName).c_str(), file.getHashValue(),
			                         htmltool::get_file_html(parentFileName).c_str(),
			                         htmltool::get_number_html(GetLineNo(loc)).c_str(), htmltool::get_include_html(includeToken).c_str());
		}

		return text;
	}

	// 获取该文件的被直接包含信息，返回内容包括：该文件名、被父文件#include的行号、被父文件#include的原始文本串
	string ParsingFile::DebugBeDirectIncludeText(FileID file) const
	{
		SourceLocation loc		= m_srcMgr->getIncludeLoc(file);
		string fileName			= GetFileName(file);
		string includeToken		= GetIncludeText(file);

		if (includeToken.empty())
		{
			includeToken = "empty";
		}

		std::stringstream text;
		text << "{line = " << htmltool::get_number_html(GetLineNo(loc)) << " [" << htmltool::get_include_html(includeToken) << "]";
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
		text << "] line = " << htmltool::get_number_html(GetLineNo(loc));
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

	// 获取第1个文件#include第2个文件的文本串
	std::string ParsingFile::GetRelativeIncludeStr(FileID f1, FileID f2) const
	{
		// 若第2个文件的被包含串原本就是#include <xxx>的格式，则返回原本的#include串
		{
			string rawInclude2 = GetIncludeText(f2);
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
			llvm::errs() << "[Error]overwrite some changed files failed!\n";
		}
	}

	// 将清理结果回写到c++源文件，返回：true回写文件时发生错误 false回写成功
	// （本接口拷贝自Rewriter::overwriteChangedFiles，唯一的不同是回写成功时会删除文件缓存）
	bool ParsingFile::Overwrite()
	{
		// 下面这个类是从clang\lib\Rewrite\Rewriter.cpp文件拷贝过来的
		// A wrapper for a file stream that atomically overwrites the target.
		//
		// Creates a file output stream for a temporary file in the constructor,
		// which is later accessible via getStream() if ok() return true.
		// Flushes the stream and moves the temporary file to the target location
		// in the destructor.
		class AtomicallyMovedFile
		{
		public:
			AtomicallyMovedFile(DiagnosticsEngine &Diagnostics, StringRef Filename,
			                    bool &AllWritten)
				: Diagnostics(Diagnostics), Filename(Filename), AllWritten(AllWritten)
			{
				TempFilename = Filename;
				TempFilename += "-%%%%%%%%";
				int FD;
				if (llvm::sys::fs::createUniqueFile(TempFilename, FD, TempFilename))
				{
					AllWritten = false;
					Diagnostics.Report(clang::diag::err_unable_to_make_temp)
					        << TempFilename;
				}
				else
				{
					FileStream.reset(new llvm::raw_fd_ostream(FD, /*shouldClose=*/true));
				}
			}

			~AtomicallyMovedFile()
			{
				if (!ok()) return;

				// Close (will also flush) theFileStream.
				FileStream->close();
				if (std::error_code ec = llvm::sys::fs::rename(TempFilename, Filename))
				{
					AllWritten = false;
					Diagnostics.Report(clang::diag::err_unable_to_rename_temp)
					        << TempFilename << Filename << ec.message();
					// If the remove fails, there's not a lot we can do - this is already an
					// error.
					llvm::sys::fs::remove(TempFilename);
				}
			}

			bool ok() { return (bool)FileStream; }
			raw_ostream &getStream() { return *FileStream; }

		private:
			DiagnosticsEngine &Diagnostics;
			StringRef Filename;
			SmallString<128> TempFilename;
			std::unique_ptr<llvm::raw_fd_ostream> FileStream;
			bool &AllWritten;
		};

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
			const FileEntry *Entry				= srcMgr.getFileEntryForID(I->first);
			const RewriteBuffer &rewriteBuffer	= I->second;

			bool ok = CxxCleanReWriter::WriteToFile(rewriteBuffer, *Entry);
			if (!ok)
			{
				llvm::errs() << "======> [error] overwrite file[" << Entry->getName() << "] failed!\n";
				AllWritten = false;
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
			llvm::errs() << "[error][ParsingFile::RemoveText] nullptr == GetSourceAtLoc(begLoc) || nullptr == GetSourceAtLoc(endLoc)\n";
			return;
		}

		if (Project::instance.m_verboseLvl >= VerboseLvl_2)
		{
			llvm::errs() << "------->replace [" << GetAbsoluteFileName(file) << "]: [" << beg << "," << end << "] to text = [" << text << "]\n";
		}

		bool err = m_rewriter->ReplaceText(begLoc, end - beg, text);
		if (err)
		{
			llvm::errs() << "=======>[error] replace [" << GetAbsoluteFileName(file) << "]: [" << beg << "," << end << "] to text = [" << text << "] failed\n";
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
			llvm::errs() << "[error][ParsingFile::RemoveText] nullptr == GetSourceAtLoc(insertLoc)\n";
			return;
		}

		if (Project::instance.m_verboseLvl >= VerboseLvl_2)
		{
			llvm::errs() << "------->insert [" << GetAbsoluteFileName(file) << "]: [" << loc << "] to text = [" << text << "]\n";
		}

		bool err = m_rewriter->InsertText(insertLoc, text, false, false);
		if (err)
		{
			llvm::errs() << "=======>[error] insert [" << GetAbsoluteFileName(file) << "]: [" << loc << "] to text = [" << text << "] failed\n";
		}
	}

	// 移除指定范围文本，若移除文本后该行变为空行，则将该空行一并移除
	void ParsingFile::RemoveText(FileID file, int beg, int end)
	{
		SourceLocation fileBegLoc	= m_srcMgr->getLocForStartOfFile(file);
		if (fileBegLoc.isInvalid())
		{
			llvm::errs() << "[error][ParsingFile::RemoveText] if (fileBegLoc.isInvalid()), remove text in [" << GetAbsoluteFileName(file) << "] failed!\n";
			return;
		}

		SourceLocation begLoc		= fileBegLoc.getLocWithOffset(beg);
		SourceLocation endLoc		= fileBegLoc.getLocWithOffset(end);

		if (nullptr == GetSourceAtLoc(begLoc) || nullptr == GetSourceAtLoc(endLoc))
		{
			llvm::errs() << "[error][ParsingFile::RemoveText] nullptr == GetSourceAtLoc(begLoc) || nullptr == GetSourceAtLoc(endLoc)\n";
			return;
		}

		SourceRange range(begLoc, endLoc);

		Rewriter::RewriteOptions rewriteOption;
		rewriteOption.IncludeInsertsAtBeginOfRange	= false;
		rewriteOption.IncludeInsertsAtEndOfRange	= false;
		rewriteOption.RemoveLineIfEmpty				= false;

		if (Project::instance.m_verboseLvl >= VerboseLvl_2)
		{
			llvm::errs() << "------->remove [" << GetAbsoluteFileName(file) << "]: [" << beg << "," << end << "], text = [" << GetSourceOfRange(range) << "]\n";
		}

		bool err = m_rewriter->RemoveText(range.getBegin(), end - beg, rewriteOption);
		if (err)
		{
			llvm::errs() << "=======>[error] remove [" << GetAbsoluteFileName(file) << "]: [" << beg << "," << end << "], text = [" << GetSourceOfRange(range) << "] failed\n";
		}
	}

	// 移除指定文件内的无用#include
	void ParsingFile::CleanByUnusedLine(const FileHistory &history, FileID file)
	{
		if (history.m_unusedLines.empty())
		{
			return;
		}

		for (auto & unusedLineItr : history.m_unusedLines)
		{
			const UnUsedLine &unusedLine = unusedLineItr.second;

			RemoveText(file, unusedLine.beg, unusedLine.end);
		}
	}

	// 在指定文件内添加前置声明
	void ParsingFile::CleanByForward(const FileHistory &history, FileID file)
	{
		if (history.m_forwards.empty())
		{
			return;
		}

		for (auto & forwardItr : history.m_forwards)
		{
			const ForwardLine &forwardLine	= forwardItr.second;

			std::stringstream text;

			for (const string &cxxRecord : forwardLine.classes)
			{
				text << cxxRecord;
				text << history.GetNewLineWord();
			}

			InsertText(file, forwardLine.offset, text.str());
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

		for (auto & replaceItr : history.m_replaces)
		{
			const ReplaceLine &replaceLine	= replaceItr.second;

			// 若是被-include参数强制引入，则跳过，因为替换并没有效果
			if (replaceLine.isSkip)
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

	// 在指定文件内移动#include
	void ParsingFile::CleanByMove(const FileHistory &history, FileID file)
	{
		if (history.m_moves.empty())
		{
			return;
		}

		const char* newLineWord = history.GetNewLineWord();

		for (auto & itr : history.m_moves)
		{
			const MoveLine &moveLine	= itr.second;

			for (auto & itr : moveLine.moveFrom)
			{
				const std::map<int, MoveFrom> &froms = itr.second;

				for (auto & innerItr : froms)
				{
					const MoveFrom &from = innerItr.second;

					if (!from.isSkip)
					{
						std::stringstream text;
						text << from.newText;
						text << newLineWord;

						InsertText(file, moveLine.line_end, text.str());
					}
				}
			}

			if (!moveLine.moveTo.empty())
			{
				RemoveText(file, moveLine.line_beg, moveLine.line_end);
			}
		}
	}

	// 根据历史清理指定文件
	void ParsingFile::CleanByHistory(const FileHistoryMap &historys)
	{
		std::map<std::string, FileID> nowFiles;

		// 建立当前cpp中文件名到文件FileID的map映射（注意：同一个文件可能被包含多次，FileID是不一样的，这里只存入最小的FileID）
		{
			for (FileID file : m_files)
			{
				const string &name = GetAbsoluteFileName(file);

				if (!Project::instance.CanClean(name))
				{
					continue;
				}

				if (nowFiles.find(name) == nowFiles.end())
				{
					nowFiles.insert(std::make_pair(name, file));
				}
			}
		}

		for (auto & itr : historys)
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

			if (!Project::instance.CanClean(fileName))
			{
				continue;
			}

			if (history.m_isSkip || history.HaveFatalError())
			{
				continue;
			}

			// 根据名称在当前cpp各文件中找到对应的文件ID（注意：同一个文件可能被包含多次，FileID是不一样的，这里取出来的是最小的FileID）
			auto & findItr = nowFiles.find(fileName);
			if (findItr == nowFiles.end())
			{
				// llvm::errs() << "[error][ParsingFile::CleanBy] if (findItr == allFiles.end()) filename = " << fileName << "\n";
				continue;
			}

			FileID file	= findItr->second;

			CleanByReplace(history, file);
			CleanByForward(history, file);
			CleanByUnusedLine(history, file);
			CleanByMove(history, file);

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
		for (auto & itr : m_uses)
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

		for (auto & itr : m_uses)
		{
			FileID file = itr.first;

			if (!CanClean(file))
			{
				continue;
			}

			const std::set<FileID> &be_uses = itr.second;

			div.AddRow("file = " + htmltool::get_file_html(GetAbsoluteFileName(file)), 2);

			for (FileID be_used_file : be_uses)
			{
				div.AddRow("old include = " + htmltool::get_include_html(GetIncludeText(be_used_file)), 3, 45);
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

		for (auto & itr : m_uses)
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
					div.AddRow("not found = " + htmltool::get_file_html(GetAbsoluteFileName(beuse_file)), 2);
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
		VerboseLvl verbose = Project::instance.m_verboseLvl;
		if (verbose <= VerboseLvl_0)
		{
			return;
		}

		HtmlDiv &div = HtmlLog::instance.m_newDiv;

		std::string rootFileName	= GetAbsoluteFileName(m_srcMgr->getMainFileID());
		div.AddTitle(strtool::get_text(cn_file_history_title,
		                               htmltool::get_number_html(ProjectHistory::instance.g_printFileNo).c_str(),
		                               htmltool::get_number_html(Project::instance.m_cpps.size()).c_str(),
		                               htmltool::get_file_html(rootFileName).c_str()));

		m_printIdx = 0;

		if (verbose >= VerboseLvl_1)
		{
			PrintCleanLog();
		}

		if (verbose >= VerboseLvl_3)
		{
			PrintUsedNames();
		}

		if (verbose >= VerboseLvl_4)
		{
			PrintUse();
			PrintNewUse();

			PrintTopRely();
			PrintRely();

			PrintRelyChildren();
			PrintMove();

			PrintAllFile();
			PrintInclude();

			PrintHeaderSearchPath();
			PrintRelativeInclude();
			PrintParent();
			PrintChildren();
			PrintNamespace();
			PrintUsingNamespace();
		}

		if (verbose >= VerboseLvl_5)
		{
			PrintNotFoundIncludeLocForDebug();
		}

		HtmlLog::instance.AddDiv(div);
	}

	// 合并可被移除的#include行
	void ParsingFile::MergeUnusedLine(const FileHistory &newFile, FileHistory &oldFile) const
	{
		FileHistory::UnusedLineMap &oldLines = oldFile.m_unusedLines;

		for (FileHistory::UnusedLineMap::iterator oldLineItr = oldLines.begin(), end = oldLines.end(); oldLineItr != end; )
		{
			int line = oldLineItr->first;
			UnUsedLine &oldLine = oldLineItr->second;

			if (newFile.IsLineUnused(line))
			{
				if (Project::instance.m_verboseLvl >= VerboseLvl_3)
				{
					llvm::errs() << "======> merge unused at [" << oldFile.m_filename << "]: new line unused, old line unused at line = " << line << " -> " << oldLine.text << "\n";
				}

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

					if (Project::instance.m_verboseLvl >= VerboseLvl_3)
					{
						llvm::errs() << "======> merge unused at [" << oldFile.m_filename << "]: new line replace, old line unused at line = " << line << " -> " << oldLine.text << "\n";
					}
				}
				else
				{
					if (Project::instance.m_verboseLvl >= VerboseLvl_3)
					{
						llvm::errs() << "======> merge unused at [" << oldFile.m_filename << "]: new line do nothing, old line unused at line = " << line << " -> " << oldLine.text << "\n";
					}
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

		for (auto & itr = m_splitReplaces.begin(); itr != m_splitReplaces.end(); ++itr)
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
					if (Project::instance.m_verboseLvl >= VerboseLvl_3)
					{
						llvm::errs() << "======> merge repalce at [" << oldFile.m_filename << "]: error, not found newFileID at line = " << line << " -> " << oldLine.oldText << "\n";
					}

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
					if (Project::instance.m_verboseLvl >= VerboseLvl_3)
					{
						llvm::errs() << "======> merge repalce at [" << oldFile.m_filename << "]: old line replace is new line replace ancestorat line = " << line << " -> " << oldLine.oldText << "\n";
					}

					++oldLineItr;
				}
				// 若新的取代文件是旧的取代文件的祖先，则改为使用新的替换信息
				else if(IsAncestor(oldLine.oldFile.c_str(), beReplaceFileID))
				{
					if (Project::instance.m_verboseLvl >= VerboseLvl_3)
					{
						llvm::errs() << "======> merge repalce at [" << oldFile.m_filename << "]: new line replace is old line replace ancestor at line = " << line << " -> " << oldLine.oldText << "\n";
					}

					oldLine.replaceTo = newLine.replaceTo;
					++oldLineItr;
				}
				// 否则，若没有直系关系，则该行无法被替换，删除该行原有的替换记录
				else
				{
					if (Project::instance.m_verboseLvl >= VerboseLvl_3)
					{
						llvm::errs() << "======> merge repalce at [" << oldFile.m_filename << "]: old line replace, new line replace at line = " << line << " -> " << oldLine.oldText << "\n";
					}

					SkipRelyLines(oldLine.replaceTo.m_rely);
					oldFile.m_replaces.erase(oldLineItr++);
				}
			}
			else
			{
				// 若在新文件中该行应被删除，而在旧文件中该行应被替换，则保留旧文件的替换记录
				if (newFile.IsLineUnused(line))
				{
					if (Project::instance.m_verboseLvl >= VerboseLvl_3)
					{
						llvm::errs() << "======> merge repalce at [" << oldFile.m_filename << "]: old line replace, new line delete at line = " << line << " -> " << oldLine.oldText << "\n";
					}

					++oldLineItr;
					continue;
				}
				// 若该行没有新的替换记录，说明该行无法被替换，删除该行旧的替换记录
				else
				{
					if (Project::instance.m_verboseLvl >= VerboseLvl_3)
					{
						llvm::errs() << "======> merge repalce at [" << oldFile.m_filename << "]: old line replace, new line do nothing at line = " << line << " -> " << oldLine.oldText << "\n";
					}

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

	// 合并可转移的#include
	void ParsingFile::MergeMoveLine(const FileHistory &newFile, FileHistory &oldFile) const
	{
		for (auto & itr : newFile.m_moves)
		{
			int line = itr.first;
			const MoveLine &newLine = itr.second;

			auto & lineItr = oldFile.m_moves.find(line);
			if (lineItr == oldFile.m_moves.end())
			{
				oldFile.m_moves.insert(std::make_pair(line, newLine));
				continue;
			}

			MoveLine &oldLine = lineItr->second;

			oldLine.moveTo.insert(newLine.moveTo.begin(), newLine.moveTo.end());

			for (auto & fromItr : newLine.moveFrom)
			{
				const std::string &fromFileName = fromItr.first;
				auto &newFroms = fromItr.second;

				auto & sameItr = oldLine.moveFrom.find(fromFileName);
				if (sameItr == oldLine.moveFrom.end())
				{
					oldLine.moveFrom.insert(fromItr);
				}
				else
				{
					auto &oldFroms = sameItr->second;
					oldFroms.insert(newFroms.begin(), newFroms.end());
				}
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
		FileHistoryMap newFiles;
		TakeHistorys(newFiles);

		// 若本文件有严重编译错误或编译错误数过多，则仅合并编译错误历史，用于打印
		if (m_compileErrorHistory.HaveFatalError())
		{
			std::string rootFile	= GetAbsoluteFileName(m_srcMgr->getMainFileID());
			oldFiles[rootFile]		= newFiles[rootFile];
			return;
		}

		for (auto & fileItr : newFiles)
		{
			const string &fileName	= fileItr.first;
			const FileHistory &newFile	= fileItr.second;

			auto & findItr = oldFiles.find(fileName);

			bool found = (findItr != oldFiles.end());
			if (!found)
			{
				oldFiles[fileName] = newFile;
			}
			else
			{
				FileHistory &oldFile = findItr->second;

				MergeUnusedLine(newFile, oldFile);
				MergeForwardLine(newFile, oldFile);
				MergeReplaceLine(newFile, oldFile);
				MergeMoveLine(newFile, oldFile);

				oldFile.Fix();
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

	// 取出当前cpp文件产生的待清理记录
	void ParsingFile::TakeHistorys(FileHistoryMap &out) const
	{
		// 先将当前cpp文件使用到的文件全存入map中
		for (FileID file : m_relys)
		{
			string fileName		= GetAbsoluteFileName(file);

			if (!Project::instance.CanClean(fileName))
			{
				continue;
			}

			// 生成对应于该文件的的记录
			FileHistory &eachFile		= out[fileName];
			eachFile.m_isWindowFormat	= IsWindowsFormat(file);
			eachFile.m_filename			= fileName;
			eachFile.m_isSkip			= IsPrecompileHeader(file);
		}

		// 1. 将可清除的行按文件进行存放
		TakeUnusedLine(out);

		// 2. 将新增的前置声明按文件进行存放
		TakeNewForwarddeclByFile(out);

		// 3. 将可替换的#include按文件进行存放
		TakeReplace(out);

		// 4. 将可转移的#include按文件进行存放
		TakeMove(out);

		// 5. 取出本文件的编译错误历史
		TakeCompileErrorHistory(out);

		// 6. 修复每个文件的历史，防止对同一行修改多次导致崩溃
		for (auto & itr : out)
		{
			FileHistory &history = itr.second;
			history.Fix();
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
				cxx::log() << "------->error: take_unused_line_by_file getPresumedLoc failed\n";
				continue;
			}

			string fileName			= pathtool::get_absolute_path(presumedLoc.getFilename());
			if (!Project::instance.CanClean(fileName))
			{
				// cxx::log() << "------->error: !Vsproject::instance.has_file(fileName) : " << fileName << "\n";
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
			UnUsedLine &unusedLine	= eachFile.m_unusedLines[line];

			unusedLine.beg	= m_srcMgr->getFileOffset(lineRange.getBegin());
			unusedLine.end	= m_srcMgr->getFileOffset(nextLine.getBegin());
			unusedLine.text	= GetSourceOfRange(lineRange);
		}
	}

	// 将新增的前置声明按文件进行存放
	void ParsingFile::TakeNewForwarddeclByFile(FileHistoryMap &out) const
	{
		if (m_forwardDecls.empty())
		{
			return;
		}

		for (auto & itr : m_forwardDecls)
		{
			FileID file			= itr.first;
			auto &cxxRecords	= itr.second;

			string fileName		= GetAbsoluteFileName(file);

			if (out.find(fileName) == out.end())
			{
				continue;
			}

			for (const CXXRecordDecl *cxxRecord : cxxRecords)
			{
				SourceLocation insertLoc = GetInsertForwardLine(file, *cxxRecord);
				if (insertLoc.isInvalid())
				{
					continue;
				}

				PresumedLoc insertPresumedLoc = m_srcMgr->getPresumedLoc(insertLoc);
				if (insertPresumedLoc.isInvalid())
				{
					cxx::log() << "------->error: take_new_forwarddecl_by_file getPresumedLoc failed\n";
					continue;
				}

				string insertFileName			= pathtool::get_absolute_path(insertPresumedLoc.getFilename());

				if (!Project::instance.CanClean(insertFileName))
				{
					continue;
				}

				// 开始取出数据
				{
					int line					= insertPresumedLoc.getLine();
					const string cxxRecordName	= GetRecordName(*cxxRecord);
					FileHistory &eachFile		= out[insertFileName];
					ForwardLine &forwardLine	= eachFile.m_forwards[line];

					forwardLine.offset		= m_srcMgr->getFileOffset(insertLoc);
					forwardLine.oldText		= GetSourceOfLine(insertLoc);
					forwardLine.classes.insert(cxxRecordName);

					{
						SourceLocation fileStart = m_srcMgr->getLocForStartOfFile(GetFileID(insertLoc));
						if (fileStart.getLocWithOffset(forwardLine.offset) != insertLoc)
						{
							cxx::log() << "error: fileStart.getLocWithOffset(forwardLine.m_offsetAtFile) != insertLoc \n";
						}
					}
				}
			}
		}
	}

	// 将文件替换记录按父文件进行归类
	void ParsingFile::SplitReplaceByFile()
	{
		for (auto & itr : m_replaces)
		{
			FileID from		= itr.first;
			FileID parent	= GetParent(from);

			if (parent.isValid())
			{
				m_splitReplaces[parent].insert(itr);
			}
		}
	}

	// 该文件是否是被-include强制包含
	bool ParsingFile::IsForceIncluded(FileID file) const
	{
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
		for (auto & itr : childernReplaces)
		{
			FileID oldFile		= itr.first;
			FileID replaceFile	= itr.second;

			bool isBeForceIncluded		= IsForceIncluded(oldFile);

			// 1. 该行的旧文本
			SourceLocation include_loc	= m_srcMgr->getIncludeLoc(oldFile);
			SourceRange	lineRange		= GetCurLineWithLinefeed(include_loc);
			int line					= (isBeForceIncluded ? 0 : GetLineNo(include_loc));

			ReplaceLine &replaceLine	= history.m_replaces[line];
			replaceLine.oldFile			= GetAbsoluteFileName(oldFile);
			replaceLine.oldText			= GetIncludeText(oldFile);
			replaceLine.beg				= m_srcMgr->getFileOffset(lineRange.getBegin());
			replaceLine.end				= m_srcMgr->getFileOffset(lineRange.getEnd());
			replaceLine.isSkip			= isBeForceIncluded || IsPrecompileHeader(oldFile);	// 记载是否是强制包含

			// 2. 该行可被替换成什么
			ReplaceTo &replaceTo	= replaceLine.replaceTo;

			SourceLocation deep_include_loc	= m_srcMgr->getIncludeLoc(replaceFile);

			// 记录[旧#include、新#include]
			replaceTo.oldText		= GetIncludeText(replaceFile);
			replaceTo.newText		= GetRelativeIncludeStr(GetParent(oldFile), replaceFile);

			// 记录[所处的文件、所处行号]
			replaceTo.line			= GetLineNo(deep_include_loc);
			replaceTo.fileName		= GetAbsoluteFileName(replaceFile);
			replaceTo.inFile		= GetAbsoluteFileName(GetFileID(deep_include_loc));

			// 3. 该行依赖于其他文件的哪些行
			std::string replaceParentName	= GetAbsoluteFileName(GetParent(replaceFile));
			int replaceLineNo				= GetIncludeLineNo(replaceFile);

			if (Project::instance.CanClean(replaceParentName))
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

					if (Project::instance.CanClean(relyParentName))
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

		for (auto & itr : m_splitReplaces)
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

	// 取出本文件的可转移信息
	void ParsingFile::TakeMove(FileHistoryMap &out) const
	{
		if (m_moves.empty())
		{
			return;
		}

		for (auto & itr : m_moves)
		{
			FileID to	= itr.first;
			auto &moves	= itr.second;

			string toFileName = GetAbsoluteFileName(to);

			if (out.find(toFileName) == out.end())
			{
				continue;
			}

			if (moves.empty())
			{
				continue;
			}

			FileHistory &toHistory = out[toFileName];

			for (auto & moveItr : moves)
			{
				FileID move			= moveItr.first;
				FileID replaceTo	= moveItr.second;

				FileID from = GetParent(move);
				if (from.isInvalid())
				{
					continue;
				}

				// 找出第2层祖先
				FileID lv2 = GetLvl2Ancestor(move, to);
				if (lv2.isInvalid())
				{
					continue;
				}

				int fromLineNo			= GetIncludeLineNo(move);
				if (IsSkip(lv2))
				{
					ProjectHistory::instance.AddSkipLine(GetAbsoluteFileName(from), fromLineNo);
					continue;
				}

				int toLineNo			= GetIncludeLineNo(lv2);
				string toOldText		= GetIncludeText(lv2);
				string toNewText		= GetRelativeIncludeStr(to, replaceTo);
				string newTextFile		= GetAbsoluteFileName(GetParent(replaceTo));
				int newTextLine			= GetIncludeLineNo(replaceTo);

				// 1. from中记载应转移到to文件
				string fromFileName = GetAbsoluteFileName(from);
				if (out.find(fromFileName) != out.end())
				{
					FileHistory &fromHistory = out[fromFileName];

					MoveLine &fromLine	= fromHistory.m_moves[fromLineNo];

					if (fromLine.moveTo.find(toFileName) != fromLine.moveTo.end())
					{
						continue;
					}

					MoveTo &moveTo			= fromLine.moveTo[toFileName];
					moveTo.toLine			= toLineNo;
					moveTo.toFile			= toFileName;
					moveTo.oldText			= toOldText;
					moveTo.newText			= toNewText;
					moveTo.newTextFile		= newTextFile;
					moveTo.newTextLine		= newTextLine;

					if (fromLine.line_beg == 0)
					{
						SourceRange lineRange	= GetIncludeRange(move);
						fromLine.line_beg		= m_srcMgr->getFileOffset(lineRange.getBegin());
						fromLine.line_end		= m_srcMgr->getFileOffset(lineRange.getEnd());
						fromLine.oldText		= GetIncludeText(move);
					}
				}

				// 2. to中记载应转移自from文件
				MoveLine &toLine		= toHistory.m_moves[toLineNo];
				auto &moveFroms			= toLine.moveFrom[fromFileName];
				MoveFrom &moveFrom		= moveFroms[fromLineNo];
				moveFrom.fromFile		= fromFileName;
				moveFrom.fromLine		= fromLineNo;
				moveFrom.oldText		= toOldText;
				moveFrom.newText		= toNewText;
				moveFrom.isSkip			= IsSkip(from);
				moveFrom.newTextFile	= newTextFile;
				moveFrom.newTextLine	= newTextLine;

				if (toLine.line_beg == 0)
				{
					SourceRange lineRange	= GetIncludeRange(lv2);
					toLine.line_beg			= m_srcMgr->getFileOffset(lineRange.getBegin());
					toLine.line_end			= m_srcMgr->getFileOffset(lineRange.getEnd());
					toLine.oldText			= toOldText;
				}
			}
		}
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

			div.AddRow("file = " + htmltool::get_file_html(GetAbsoluteFileName(file)), 2);

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

			div.AddRow("file = " + htmltool::get_file_html(GetAbsoluteFileName(file)), 2);

			for (FileID beinclude : includeList.second)
			{
				div.AddRow("include = " + DebugBeDirectIncludeText(beinclude), 3);
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
		div.AddRow("file = " + htmltool::get_file_html(GetAbsoluteFileName(file)), 2);

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

			div.AddRow("file = " + htmltool::get_file_html(GetFileName(file)), 2);
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

			div.AddRow("file = " + htmltool::get_file_html(GetFileName(file)), 2);
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

			div.AddRow("file = " + htmltool::get_file_html(GetAbsoluteFileName(file)), 2);

			for (auto & usedChild : usedItr.second)
			{
				div.AddRow("use children " + DebugBeIncludeText(usedChild), 3);
			}

			div.AddRow("");
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

			if (!Project::instance.CanClean(name))
			{
				continue;
			}

			div.AddRow("file id = " + strtool::itoa(file.getHashValue()), 2, 20);
			div.AddGrid("filename = " + htmltool::get_file_html(name));
		}

		div.AddRow("");
	}

	// 打印清理日志
	void ParsingFile::PrintCleanLog() const
	{
		HtmlDiv &div = HtmlLog::instance.m_newDiv;

		m_compileErrorHistory.Print();

		FileHistoryMap files;
		TakeHistorys(files);

		int i = 0;

		for (auto & itr : files)
		{
			const FileHistory &history = itr.second;
			if (!history.IsNeedClean())
			{
				continue;
			}

			if (Project::instance.m_verboseLvl < VerboseLvl_3)
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
		for (auto & itr : m_namespaces)
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

		for (auto & itr : m_namespaces)
		{
			FileID file = itr.first;

			if (!IsNeedPrintFile(file))
			{
				continue;
			}

			div.AddRow("file = " + htmltool::get_file_html(GetAbsoluteFileName(file)), 2);

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
		for (auto & itr : m_usingNamespaces)
		{
			SourceLocation loc		= itr.first;
			const NamespaceInfo &ns	= itr.second;

			nsByFile[GetFileID(loc)].insert(ns.ns->getQualifiedNameAsString());
		}

		int num = 0;
		for (auto & itr : nsByFile)
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

		for (auto & itr : nsByFile)
		{
			FileID file = itr.first;

			if (!IsNeedPrintFile(file))
			{
				continue;
			}

			div.AddRow("file = " + htmltool::get_file_html(GetAbsoluteFileName(file)), 2);

			std::set<std::string> &namespaces = itr.second;

			for (const std::string &ns : namespaces)
			{
				div.AddRow("using namespace = " + htmltool::get_include_html(ns), 3);
			}

			div.AddRow("");
		}
	}

	// 打印可被移动到cpp的文件列表
	void ParsingFile::PrintMove() const
	{
		HtmlDiv &div = HtmlLog::instance.m_newDiv;
		div.AddRow(AddPrintIdx() + ". can moved to cpp file list: file count = " + htmltool::get_number_html(m_moves.size()), 1);

		for (auto & itr : m_moves)
		{
			FileID at	= itr.first;
			auto &moves	= itr.second;

			if (!IsNeedPrintFile(at))
			{
				continue;
			}

			div.AddRow("file = " + DebugBeIncludeText(at), 2);

			for (auto & moveItr : moves)
			{
				FileID beMove		= moveItr.first;
				FileID replaceTo	= moveItr.second;

				div.AddRow("move = " + DebugBeIncludeText(beMove), 3);

				if (beMove != replaceTo)
				{
					div.AddRow("replaceTo = " + DebugBeIncludeText(replaceTo), 3);
				}
			}
		}

		div.AddRow("");
	}
}