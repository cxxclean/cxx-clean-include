///<------------------------------------------------------------------------------
//< @file:   parsing_cpp.cpp
//< @author: 洪坤安
//< @date:   2016年2月22日
//< @brief:  存储当前正在解析的cpp文件及其包含的头文件的信息
//< Copyright (c) 2016. All rights reserved.
///<------------------------------------------------------------------------------

#include "parsing_cpp.h"

#include <sstream>

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

namespace cxxcleantool
{
	ParsingFile::ParsingFile(clang::Rewriter &rewriter, clang::CompilerInstance &compiler)
	{
		m_rewriter	= &rewriter;
		m_compiler	= &compiler;
		m_srcMgr	= &compiler.getSourceManager();;
	}

	// 初始化本对象
	bool ParsingFile::Init()
	{
		clang::HeaderSearch &headerSearch = m_compiler->getPreprocessor().getHeaderSearchInfo();
		m_headerSearchPaths = TakeHeaderSearchPaths(headerSearch);

		m_printIdx		= 0;
		g_atFile		= this;

		return true;
	}

	// 添加#include的位置记录
	void ParsingFile::AddIncludeLoc(SourceLocation loc, SourceRange range)
	{
		SourceRange spellingRange(GetExpasionLoc(range.getBegin()), GetExpasionLoc(range.getEnd()));

		// llvm::outs() << "ParsingFile::AddIncludeLoc = " << GetSourceOfLine(GetExpasionLoc(loc)) << "\n";

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

		for (auto itr : include_dirs_map)
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
		for (auto itr : m_uses)
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

		GenerateRootCycleUse();
		GenerateAllCycleUse();

		GenerateUnusedInclude();
		GenerateCanForwardDeclaration();
		GenerateCanReplace();

		GenerateRemainUsingNamespace();
	}

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
	void ParsingFile::GenerateRootCycleUse()
	{
		FileID mainFileID = m_srcMgr->getMainFileID();

		GetCycleUseFile(mainFileID, m_rootCycleUse);
	}

	// 记录各文件的被依赖后代文件
	void ParsingFile::GenerateCycleUsedChildren()
	{
		for (FileID usedFile : m_allCycleUse)
		{
			for (FileID child = usedFile, parent; (parent = GetParent(child)).isValid(); child = parent)
			{
				m_cycleUsedChildren[parent].insert(usedFile);
			}
		}
	}

	// 获取该文件可被替换到的文件，若无法被替换，则返回空文件id
	FileID ParsingFile::GetCanReplaceTo(FileID top) const
	{
		// 若文件本身也被使用到，则无法被替换
		if (IsCyclyUsed(top))
		{
			return FileID();
		}

		auto itr = m_cycleUsedChildren.find(top);
		if (itr == m_cycleUsedChildren.end())
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

	// 尝试添加各个被依赖文件的祖先文件，返回值：true表示依赖文件集被扩张、false表示依赖文件夹不变
	bool ParsingFile::TryAddAncestor()
	{
		FileID mainFile = m_srcMgr->getMainFileID();

		for (auto itr = m_allCycleUse.rbegin(); itr != m_allCycleUse.rend(); ++itr)
		{
			FileID file = *itr;

			for (FileID top = mainFile, lv2Top; top != file; top = lv2Top)
			{
				lv2Top = GetLvl2Ancestor(file, top);

				if (lv2Top == file)
				{
					break;
				}

				if (IsCyclyUsed(lv2Top))
				{
					continue;
				}

				if (IsReplaced(lv2Top))
				{
					FileID oldReplaceTo = *(m_replaces[lv2Top].begin());
					FileID canReplaceTo = GetCanReplaceTo(lv2Top);

					if (canReplaceTo != oldReplaceTo)
					{
						m_replaces.erase(lv2Top);

						// llvm::outs() << "fatal: there is replace cleared!\n";
						return true;
					}

					break;
				}

				FileID canReplaceTo = GetCanReplaceTo(lv2Top);

				bool expand = false;

				// 若不可被替换
				if (canReplaceTo.isInvalid())
				{
					expand = ExpandAllCycleUse(lv2Top);
				}
				// 若可被替换
				else
				{
					expand = ExpandAllCycleUse(canReplaceTo);
					if (!expand)
					{
						m_replaces[lv2Top].insert(canReplaceTo);
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
	void ParsingFile::GenerateAllCycleUse()
	{
		/*
			下面这段代码是本工具的主要处理思路
		*/
		{
			// 1. 获取主文件的循环引用文件集
			m_allCycleUse = m_rootCycleUse;

			// 2. 记录各文件的后代文件中被依赖的部分
			GenerateCycleUsedChildren();

			// 3. 尝试添加各个被依赖文件的祖先文件
			while (TryAddAncestor())
			{
				// 4. 若依赖文件集被扩张，则重新生成后代依赖文件集
				GenerateCycleUsedChildren();
			};
		}

		for (FileID beusedFile : m_allCycleUse)
		{
			// 从被使用文件的父节点，层层往上建立引用父子引用关系
			for (FileID child = beusedFile, parent; (parent = GetParent(child)).isValid(); child = parent)
			{
				m_usedLocs.insert(m_srcMgr->getIncludeLoc(child));
			}
		}
	}

	bool ParsingFile::ExpandAllCycleUse(FileID newCycleUse)
	{
		std::set<FileID> cycleUseList;
		GetCycleUseFile(newCycleUse, cycleUseList);

		int oldSize = m_allCycleUse.size();

		m_allCycleUse.insert(cycleUseList.begin(), cycleUseList.end());

		int newSize = m_allCycleUse.size();

		return newSize > oldSize;
	}

	// 该文件是否被循环引用到
	bool ParsingFile::IsCyclyUsed(FileID file) const
	{
		return m_allCycleUse.find(file) != m_allCycleUse.end();
	}

	// 该文件是否被主文件循环引用到
	bool ParsingFile::IsRootCyclyUsed(FileID file) const
	{
		return m_rootCycleUse.find(file) != m_rootCycleUse.end();
	}

	// 该文件是否可被替换
	bool ParsingFile::IsReplaced(FileID file) const
	{
		return m_replaces.find(file) != m_replaces.end();
	}

	// 生成无用#include的记录
	void ParsingFile::GenerateUnusedInclude()
	{
		for (auto locItr : m_includeLocs)
		{
			SourceLocation loc = locItr.first;
			FileID file = GetFileID(loc);

			if (!IsCyclyUsed(file))
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
			if (IsPrecompileHeader(beusedFile))
			{
				m_unusedLocs.erase(m_srcMgr->getIncludeLoc(beusedFile));
			}
		}
	}

	// 从指定的文件列表中找到属于传入文件的后代
	std::set<FileID> ParsingFile::GetChildren(FileID ancestor, std::set<FileID> all_children/* 包括非ancestor孩子的文件 */)
	{
		// 属于ancestor的后代文件列表
		std::set<FileID> children;

		// llvm::outs() << "ancestor = " << get_file_name(ancestor) << "\n";

		// 获取属于该文件的后代中被主文件使用的一部分
		for (FileID child : all_children)
		{
			if (!IsAncestor(child, ancestor))
			{
				continue;
			}

			// llvm::outs() << "    child = " << get_file_name(child) << "\n";
			children.insert(child);
		}

		return children;
	}

	// 获取指定文件直接依赖和间接依赖的文件集
	// 计算过程是：
	//     获取top所依赖的文件，并循环获取这些依赖文件所依赖的其他文件，直到所有的被依赖文件均已被处理
	void ParsingFile::GetCycleUseFile(FileID top, std::set<FileID> &out) const
	{
		// 查找主文件的引用记录
		auto topUseItr = m_uses.find(top);
		if (topUseItr == m_uses.end())
		{
			out.insert(top);
			return;
		}

		std::set<FileID> left;
		std::set<FileID> history;

		history.insert(top);

		// 获取主文件的依赖文件集
		const std::set<FileID> &topUseFiles = topUseItr->second;
		left.insert(topUseFiles.begin(), topUseFiles.end());

		// 循环获取被依赖文件所依赖的其他文件
		while (!left.empty())
		{
			FileID cur = *left.begin();
			left.erase(left.begin());

			history.insert(cur);

			// 若当前文件不再依赖其他文件，则可以不再扩展当前文件
			auto useItr = m_uses.find(cur);
			if (useItr == m_uses.end())
			{
				continue;
			}

			const std::set<FileID> &useFiles = useItr->second;

			for (const FileID &used : useFiles)
			{
				if (history.find(used) != history.end())
				{
					continue;
				}

				left.insert(used);
			}
		}

		out.insert(history.begin(), history.end());
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

		bool found = false;

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
	void ParsingFile::GenerateCanReplace()
	{
		SplitReplaceByFile(m_splitReplaces);
	}

	// 生成应保留的using namespace
	void ParsingFile::GenerateRemainUsingNamespace()
	{
		std::set<string> usedNamespaces;
		for (auto itr : m_namespaces)
		{
			FileID file		= itr.first;
			auto namespaces	= itr.second;
			if (!IsCyclyUsed(file))
			{
				continue;
			}

			usedNamespaces.insert(namespaces.begin(), namespaces.end());
		}

		for (auto itr = m_remainUsingNamespaces.begin(); itr != m_remainUsingNamespaces.end();)
		{
			SourceLocation loc		= itr->first;
			const NamespaceInfo &ns	= itr->second;

			bool need = true;
			need &= !IsCyclyUsed(GetFileID(loc));
			need &= (usedNamespaces.find(ns.ns_name) != usedNamespaces.end());

			if (!need)
			{
				m_remainUsingNamespaces.erase(itr++);
			}
			else
			{
				++itr;
			}
		}
	}

	// 当前文件之前是否已有文件声明了该class、struct、union
	bool ParsingFile::HasRecordBefore(FileID cur, const CXXRecordDecl &cxxRecord) const
	{
		FileID recordAtFile = GetFileID(cxxRecord.getLocStart());

		// 若类所在的文件被引用，则不需要再加前置声明
		if (IsCyclyUsed(recordAtFile))
		{
			return true;
		}

		// 否则，说明类所在的文件未被引用
		// 1. 此时，需查找类定义之前的所有文件中，是否已有该类的前置声明
		for (const CXXRecordDecl *prev = cxxRecord.getPreviousDecl(); prev; prev = prev->getPreviousDecl())
		{
			FileID prevFileId = GetFileID(prev->getLocation());
			if (!IsCyclyUsed(prevFileId))
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
			if (!IsCyclyUsed(redeclFileID))
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

	// 生成新增前置声明列表
	void ParsingFile::GenerateCanForwardDeclaration()
	{
		// 将被使用文件的前置声明记录删掉
		for (auto itr = m_forwardDecls.begin(), end = m_forwardDecls.end(); itr != end;)
		{
			FileID file										= itr->first;
			std::set<const CXXRecordDecl*> &old_forwards	= itr->second;

			if (IsCyclyUsed(file))
			{
				++itr;
			}
			else
			{
				m_forwardDecls.erase(itr++);
				continue;
			}

			std::set<const CXXRecordDecl*> can_forwards;

			for (const CXXRecordDecl* cxxRecordDecl : old_forwards)
			{
				if (!HasRecordBefore(file, *cxxRecordDecl))
				{
					can_forwards.insert(cxxRecordDecl);
				}
			}

			if (can_forwards.size() < old_forwards.size())
			{
				/*
				// 打印前后对比
				{
					llvm::outs() << "\nprocessing file: " << GetAbsoluteFileName(file) << "\n";
					llvm::outs() << "    old forwarddecl list: \n";

					for (const CXXRecordDecl* cxxRecordDecl : old_forwards)
					{
						llvm::outs() << "        forwarddecl: " << GetCxxrecordName(*cxxRecordDecl) << " in " << GetAbsoluteFileName(file) << "\n";
					}

					llvm::outs() << "\n";
					llvm::outs() << "    new forwarddecl list: \n";

					for (const CXXRecordDecl* cxxRecordDecl : can_forwards)
					{
						llvm::outs() << "        forwarddecl: " << GetCxxrecordName(*cxxRecordDecl) << "\n";
					}
				}
				*/

				old_forwards = can_forwards;
			}
		}
	}

	// 获取指定范围的文本
	std::string ParsingFile::GetSourceOfRange(SourceRange range) const
	{
		if (range.getEnd() < range.getBegin())
		{
			return "";
		}

		if (!m_srcMgr->isWrittenInSameFile(range.getBegin(), range.getEnd()))
		{
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
			llvm::outs() << "[error][ParsingFile::GetSourceAtLoc]! err = " << err << "line = " << GetSourceOfLine(loc) << "\n";
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
	std::string ParsingFile::GetRawIncludeStr(FileID file) const
	{
		SourceLocation loc = m_srcMgr->getIncludeLoc(file);

		auto itr = m_includeLocs.find(loc);
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
		if (Project::instance.m_verboseLvl < VerboseLvl_2)
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
		auto parentItr = m_parentIDs.begin();

		while ((parentItr = m_parentIDs.find(young)) != m_parentIDs.end())
		{
			FileID parent = parentItr->second;
			if (parent == old)
			{
				return true;
			}

			young = parent;
		}

		return false;
	}

	// 第2个文件是否是第1个文件的祖先
	bool ParsingFile::IsAncestor(FileID young, const char* old) const
	{
		auto parentItr = m_parentIDs.begin();

		while ((parentItr = m_parentIDs.find(young)) != m_parentIDs.end())
		{
			FileID parent = parentItr->second;
			if (GetAbsoluteFileName(parent) == old)
			{
				return true;
			}

			young = parent;
		}

		return false;
	}

	// 第2个文件是否是第1个文件的祖先
	bool ParsingFile::IsAncestor(const char* young, FileID old) const
	{
		// 在父子关系表查找与后代文件同名的FileID（若多次#include同一文件，则会为该文件分配多个不同的FileID）
		for (auto itr : m_parentIDs)
		{
			FileID child	= itr.first;

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

		auto itr = m_parentIDs.find(child);
		if (itr == m_parentIDs.end())
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
			// llvm::outs() << "------->error: use_include() : m_srcMgr->getFileEntryForID(file) failed!" << m_srcMgr->getFilename(m_srcMgr->getLocForStartOfFile(file)) << ":" << m_srcMgr->getFilename(m_srcMgr->getLocForStartOfFile(beusedFile)) << "\n";
			// llvm::outs() << "------->error: use_include() : m_srcMgr->getFileEntryForID(file) failed!" << get_source_of_line(m_srcMgr->getLocForStartOfFile(file)) << ":" << get_source_of_line(m_srcMgr->getLocForStartOfFile(beusedFile)) << "\n";
			return;
		}

		m_uses[file].insert(beusedFile);
		UseName(file, beusedFile, name, line);
	}

	// 文件a使用指定名称的目标文件
	void ParsingFile::UseByFileName(FileID a, const char* filename)
	{
		auto itr = m_uses.find(a);
		if (itr == m_uses.end())
		{
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
			m_newUse[a].insert(child);
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

		// llvm::outs() << "macro id = " << macroName.getIdentifierInfo()->getName().str() << "\n";
		Use(loc, info->getDefinitionLoc(), macroName.c_str());
	}

	// 当前位置使用定位
	void ParsingFile::UseQualifier(SourceLocation loc, const NestedNameSpecifier *specifier)
	{
		while (specifier)
		{
			const Type *pType = specifier->getAsType();
			UseType(loc, pType);

			specifier = specifier->getPrefix();
		}
	}

	// 声明了命名空间
	void ParsingFile::DeclareNamespace(const NamespaceDecl *d)
	{
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

		const NamespaceDecl *nsDecl	= d->getNominatedNamespace();
		std::string ns				= nsDecl->getQualifiedNameAsString();
		m_usingNamespaces[file].insert(ns);

		NamespaceInfo nsInfo;
		nsInfo.ns_decl = GetNestedNamespace(nsDecl);
		nsInfo.ns_name = ns;
		m_remainUsingNamespaces[loc] = nsInfo;

		// 引用命名空间所在的文件（注意：using namespace时必须能找到对应的namespace声明，比如，using namespace A前一定要有namespace A{}否则编译会报错）
		Use(loc, nsDecl->getLocation(), GetNestedNamespace(nsDecl).c_str());
	}

	// using了命名空间下的某类，比如：using std::string;
	void ParsingFile::UsingXXX(const UsingDecl *d)
	{
		SourceLocation usingLoc		= d->getUsingLoc();

		for (auto itr = d->shadow_begin(); itr != d->shadow_end(); ++itr)
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

	// 获取可能缺失的using namespace
	bool ParsingFile::GetMissingNamespace(SourceLocation loc, std::map<std::string, std::string> &miss) const
	{
		loc = GetSpellingLoc(loc);

		FileID file = GetFileID(loc);

		bool is_need_add = true;

		for (auto itr : m_remainUsingNamespaces)
		{
			SourceLocation usingNsLoc	= itr.first;
			const NamespaceInfo &ns		= itr.second;

			FileID usingNsfile	= GetFileID(usingNsLoc);

			if (!IsAncestor(usingNsfile, file))
			{
				continue;
			}

			FileID lv2 = GetLvl2Ancestor(usingNsfile, file);
			if (m_srcMgr->getIncludeLoc(lv2) == loc)
			{
				std::string addNs = "using namespace " + ns.ns_name + ";";
				miss[addNs] = ns.ns_decl;
				is_need_add = true;
			}
		}

		return is_need_add;
	}

	// 获取可能缺失的using namespace
	bool ParsingFile::GetMissingNamespace(SourceLocation topLoc, SourceLocation oldLoc,
	                                      std::map<std::string, std::string> &frontMiss, std::map<std::string, std::string> &backMiss) const
	{
		FileID file = GetFileID(topLoc);

		bool is_need_add = true;

		for (auto itr : m_remainUsingNamespaces)
		{
			SourceLocation usingNsLoc	= itr.first;
			const NamespaceInfo &ns		= itr.second;
			FileID usingNsfile	= GetFileID(usingNsLoc);

			if (!IsAncestor(usingNsfile, file))
			{
				continue;
			}

			FileID lv2 = GetLvl2Ancestor(usingNsfile, file);

			SourceLocation lv2BeIncludeLoc = m_srcMgr->getIncludeLoc(lv2);
			if (lv2BeIncludeLoc != topLoc)
			{
				continue;
			}

			is_need_add = true;

			std::string addNs = "using namespace " + ns.ns_name + ";";
			if (oldLoc < usingNsLoc)
			{
				backMiss[addNs] = ns.ns_decl;
			}
			else
			{
				frontMiss[addNs] = ns.ns_decl;
			}
		}

		return is_need_add;
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
		auto itr = m_includes.find(b);
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
		auto itr = m_includes.find(a);
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
		auto itr = m_children.find(a);
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

			UseNameDecl(loc, templateType->getTemplateName().getAsTemplateDecl());

			for (int i = 0, size = templateType->getNumArgs(); i < size; ++i)
			{
				const TemplateArgument &arg = templateType->getArg((unsigned)i);

				TemplateArgument::ArgKind argKind = arg.getKind();

				switch (argKind)
				{
				case TemplateArgument::Type:
					// arg.getAsType().dump();
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
			//llvm::errs() << "-------------- haven't support type --------------\n";
			//t->dump();
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
			llvm::outs() << "LexSkipComment Invalid = true";
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
		FileID file = GetFileID(loc);
		if (file.isInvalid())
		{
			return;
		}

		string cxxRecordName = GetRecordName(*cxxRecordDecl);

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

		if (!IsForwardType(var->getType()))
		{
			UseValueDecl(loc, var);
			return;
		}

		UseVarType(loc, var->getType());
	}

	// 引用变量声明（为左值）、函数表示、enum常量
	void ParsingFile::UseValueDecl(SourceLocation loc, const ValueDecl *valueDecl)
	{
		std::stringstream name;
		name << valueDecl->getQualifiedNameAsString() << "[" << valueDecl->getDeclKindName() << "]";

		Use(loc, valueDecl->getLocEnd(), name.str().c_str());
		UseVarType(loc, valueDecl->getType());
	}

	// 引用带有名称的声明
	void ParsingFile::UseNameDecl(SourceLocation loc, const NamedDecl *nameDecl)
	{
		std::stringstream name;
		name << nameDecl->getQualifiedNameAsString() << "[" << nameDecl->getDeclKindName() << "]";

		Use(loc, nameDecl->getLocEnd(), name.str().c_str());
	}

	// 新增使用函数声明记录
	void ParsingFile::UseFuncDecl(SourceLocation loc, const FunctionDecl *funcDecl)
	{
		std::stringstream name;
		name << funcDecl->getQualifiedNameAsString() << "[" << funcDecl->clang::Decl::getDeclKindName() << "]";

		Use(loc, funcDecl->getTypeSpecStartLoc(), name.str().c_str());
	}

	// 新增使用class、struct、union记录
	void ParsingFile::UseRecord(SourceLocation loc, const RecordDecl *record)
	{
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
		for (auto childparent : m_parentIDs)
		{
			FileID childFileID = childparent.first;
			FileID parentFileID = childparent.second;

			// 仅打印跟项目内文件有直接关联的文件
			if (!CanClean(childFileID) && !CanClean(parentFileID))
			{
				continue;
			}

			++num;
		}

		HtmlDiv &div = HtmlLog::instance.m_newDiv;
		div.AddRow(AddPrintIdx() + ". list of parent id: has parent file count = " + htmltool::get_number_html(num), 1);

		for (auto childparent : m_parentIDs)
		{
			FileID childFileID = childparent.first;
			FileID parentFileID = childparent.second;

			// 仅打印跟项目内文件有直接关联的文件
			if (!CanClean(childFileID) && !CanClean(parentFileID))
			{
				continue;
			}

			div.AddRow(htmltool::get_file_html(GetAbsoluteFileName(childFileID)), 2, 50);
			div.AddGrid("parent = ", 10);
			div.AddGrid(htmltool::get_file_html(GetAbsoluteFileName(parentFileID)), 39);
		}

		div.AddRow("");
	}

	// 打印各文件的孩子文件
	void ParsingFile::PrintChildren()
	{
		int num = 0;
		for (auto itr : m_children)
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

		for (auto itr : m_children)
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
		for (auto itr : m_newUse)
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

		for (auto itr : m_newUse)
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
			string includeToken				= GetRawIncludeStr(file);
			string parentFileName			= pathtool::get_absolute_path(parentPresumedLoc.getFilename());

			if (includeToken.empty())
			{
				includeToken = "empty";
			}

			text = strtool::get_text(cn_file_debug_text,
			                         htmltool::get_file_html(fileName).c_str(), file.getHashValue(),
			                         htmltool::get_file_html(parentFileName).c_str(),
			                         htmltool::get_include_html(includeToken).c_str(), htmltool::get_number_html(GetLineNo(loc)).c_str());
		}

		return text;
	}

	// 获取该文件的被直接包含信息，返回内容包括：该文件名、被父文件#include的行号、被父文件#include的原始文本串
	string ParsingFile::DebugBeDirectIncludeText(FileID file) const
	{
		SourceLocation loc		= m_srcMgr->getIncludeLoc(file);
		string fileName			= GetFileName(file);
		string includeToken		= GetRawIncludeStr(file);

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

	// 打印引用记录
	void ParsingFile::PrintUse() const
	{
		int num = 0;
		for (auto use_list : m_uses)
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

		for (auto use_list : m_uses)
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
		for (auto includeList : m_includes)
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

		for (auto includeList : m_includes)
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
		for (auto useItr : m_useNames)
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

		for (auto useItr : m_useNames)
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

				auto linesItr = beuse.nameMap.find(name);
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
				div.AddGrid("lines = " + linesText, 29);
			}

			div.AddRow("");
		}
	}

	// 打印主文件循环引用的名称记录
	void ParsingFile::PrintRootCycleUsedNames() const
	{
		int num = 0;
		for (auto useItr : m_useNames)
		{
			FileID file = useItr.first;

			if (!CanClean(file) || !IsRootCyclyUsed(file))
			{
				continue;
			}

			++num;
		}

		HtmlDiv &div = HtmlLog::instance.m_newDiv;
		div.AddRow(AddPrintIdx() + ". list of root cycle use name : file count = " + strtool::itoa(num), 1);

		for (auto useItr : m_useNames)
		{
			FileID file									= useItr.first;
			const std::vector<UseNameInfo> &useNames	= useItr.second;

			if (!CanClean(file) || !IsRootCyclyUsed(file))
			{
				continue;
			}

			DebugUsedNames(file, useNames);
		}
	}

	// 打印循环引用的名称记录
	void ParsingFile::PrintAllCycleUsedNames() const
	{
		HtmlDiv &div = HtmlLog::instance.m_newDiv;
		div.AddRow(AddPrintIdx() + ". list of all cycle use name : file count = " + htmltool::get_number_html(GetCanCleanFileCount()), 1);

		for (auto useItr : m_useNames)
		{
			FileID file									= useItr.first;
			const std::vector<UseNameInfo> &useNames	= useItr.second;

			if (!CanClean(file) || !IsCyclyUsed(file))
			{
				continue;
			}

			if (!IsRootCyclyUsed(file))
			{
				div.AddRow("<--------- new add cycle use file --------->", 2, 100, true);
			}

			DebugUsedNames(file, useNames);
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
		for (FileID file : m_allCycleUse)
		{
			if (!CanClean(file))
			{
				continue;
			}

			++num;
		}

		return num;
	}

	// 打印主文件循环引用的文件列表
	void ParsingFile::PrintRootCycleUse() const
	{
		int num = 0;

		for (auto file : m_rootCycleUse)
		{
			if (!IsNeedPrintFile(file))
			{
				continue;
			}

			++num;
		}

		HtmlDiv &div = HtmlLog::instance.m_newDiv;
		div.AddRow(AddPrintIdx() + ". list of root cycle use : file count = " + htmltool::get_number_html(num), 1);

		for (auto file : m_rootCycleUse)
		{
			if (!IsNeedPrintFile(file))
			{
				continue;
			}

			div.AddRow("file = " + htmltool::get_file_html(GetFileName(file)), 2);
		}

		div.AddRow("");
	}

	// 打印循环引用的文件列表
	void ParsingFile::PrintAllCycleUse() const
	{
		int num = 0;
		for (auto file : m_allCycleUse)
		{
			if (!IsNeedPrintFile(file))
			{
				continue;
			}

			++num;
		}

		HtmlDiv &div = HtmlLog::instance.m_newDiv;
		div.AddRow(AddPrintIdx() + ". list of all cycle use : file count = " + htmltool::get_number_html(num), 1);

		for (auto file : m_allCycleUse)
		{
			if (!IsNeedPrintFile(file))
			{
				continue;
			}

			if (!IsRootCyclyUsed(file))
			{
				div.AddRow("<--------- new add cycle use file --------->", 2, 100, true);
			}

			div.AddRow("file = " + htmltool::get_file_html(GetFileName(file)), 2);
		}

		div.AddRow("");
	}

	// 打印各文件对应的有用孩子文件记录
	void ParsingFile::PrintCycleUsedChildren() const
	{
		int num = 0;
		for (auto usedItr : m_cycleUsedChildren)
		{
			FileID file = usedItr.first;

			if (!CanClean(file))
			{
				continue;
			}

			++num;
		}

		HtmlDiv &div = HtmlLog::instance.m_newDiv;
		div.AddRow(AddPrintIdx() + ". list of cycle used children file: file count = " + htmltool::get_number_html(num), 1);

		for (auto usedItr : m_cycleUsedChildren)
		{
			FileID file = usedItr.first;

			if (!CanClean(file))
			{
				continue;
			}

			div.AddRow("file = " + htmltool::get_file_html(GetAbsoluteFileName(file)), 2);

			for (auto usedChild : usedItr.second)
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
			div.AddGrid("filename = " + htmltool::get_file_html(name), 79);
		}

		div.AddRow("");
	}

	// 打印单个文件的清理历史
	void ParsingFile::PrintFileHistory(const FileHistory &history)
	{
		if (!Project::instance.CanClean(history.m_filename))
		{
			return;
		}

		if (history.m_isSkip)
		{
			HtmlDiv &div = HtmlLog::instance.m_newDiv;

			// 打印：本文件为预编译头文件，禁止被改动
			div.AddRow(htmltool::get_warn_html(cn_file_skip), 1);
		}

		PrintUnusedIncludeOfFile(history);
		PrintCanForwarddeclOfFile(history);
		PrintCanReplaceOfFile(history);
	}

	// 打印单个文件内的可被删#include记录
	void ParsingFile::PrintUnusedIncludeOfFile(const FileHistory &history)
	{
		HtmlDiv &div = HtmlLog::instance.m_newDiv;
		if (history.m_unusedLines.empty())
		{
			return;
		}

		// 打印带url的文件名
		div.AddRow(strtool::get_text(cn_file_unused_count, htmltool::get_number_html(history.m_unusedLines.size()).c_str()), 2);

		for (auto unusedLineItr : history.m_unusedLines)
		{
			int line = unusedLineItr.first;

			UselessLine &unusedLine = unusedLineItr.second;

			div.AddRow(strtool::get_text(cn_file_unused_line, htmltool::get_number_html(line).c_str()), 3, 25);
			div.AddGrid(strtool::get_text(cn_file_unused_include, htmltool::get_include_html(unusedLine.m_text).c_str()), 74, true);

			for (auto itr : unusedLine.m_usingNamespace)
			{
				const std::string &ns_name = itr.first;
				const std::string &ns_decl = itr.second;

				div.AddRow(strtool::get_text(cn_file_add_using_namespace, htmltool::get_include_html(ns_decl).c_str()), 4);
				div.AddRow(strtool::get_text(cn_file_add_using_namespace, htmltool::get_include_html(ns_name).c_str()), 4);
			}
		}

		div.AddRow("");
	}

	// 打印单个文件内的可新增前置声明记录
	void ParsingFile::PrintCanForwarddeclOfFile(const FileHistory &history)
	{
		HtmlDiv &div = HtmlLog::instance.m_newDiv;
		if (history.m_forwards.empty())
		{
			return;
		}

		// 打印带url的文件名
		div.AddRow(strtool::get_text(cn_file_add_forward_num, htmltool::get_number_html(history.m_forwards.size()).c_str()), 2);

		for (auto forwardItr : history.m_forwards)
		{
			int line = forwardItr.first;

			ForwardLine &forwardLine = forwardItr.second;

			div.AddRow(strtool::get_text(cn_file_add_forward_line, htmltool::get_number_html(line).c_str(), htmltool::get_include_html(forwardLine.m_oldText).c_str()), 3);
			// div.AddGrid(strtool::get_text(cn_file_add_forward_old_text, ), 69, true);

			for (const string &name : forwardLine.m_classes)
			{
				div.AddRow(strtool::get_text(cn_file_add_forward_new_text, htmltool::get_include_html(name).c_str()), 4);
			}

			div.AddRow("");
		}
	}

	// 打印单个文件内的可被替换#include记录
	void ParsingFile::PrintCanReplaceOfFile(const FileHistory &history)
	{
		HtmlDiv &div = HtmlLog::instance.m_newDiv;
		if (history.m_replaces.empty())
		{
			return;
		}

		// 打印带url的文件名
		div.AddRow(strtool::get_text(cn_file_can_replace_num, htmltool::get_number_html(history.m_replaces.size()).c_str()), 2);

		for (auto replaceItr : history.m_replaces)
		{
			int line = replaceItr.first;

			ReplaceLine &replaceLine = replaceItr.second;

			// 比如，输出: [line = 1] -> {old text = [#include "a.h"]}

			std::string oldText = strtool::get_text(cn_file_can_replace_line, htmltool::get_number_html(line).c_str(), htmltool::get_include_html(replaceLine.m_oldText).c_str());
			if (replaceLine.m_isSkip)
			{
				oldText += cn_file_force_include_text;
			}

			div.AddRow(oldText.c_str(), 3);
			// div.AddGrid(oldText, 69);

			// 比如，输出: replace to = #include <../1.h> in [./2.h : line = 3]
			for (const ReplaceTo &replaceInfo : replaceLine.m_newInclude)
			{
				// 若替换的串内容不变，则只打印一个
				if (replaceInfo.m_newText == replaceInfo.m_oldText)
				{
					div.AddRow(strtool::get_text(cn_file_replace_same_text, htmltool::get_include_html(replaceInfo.m_oldText).c_str()), 4, 40, true);
				}
				// 否则，打印替换前和替换后的#include整串
				else
				{
					div.AddRow(strtool::get_text(cn_file_replace_old_text, htmltool::get_include_html(replaceInfo.m_oldText).c_str()), 4, 40, true);
					div.AddGrid(strtool::get_text(cn_file_replace_new_text, htmltool::get_include_html(replaceInfo.m_newText).c_str()), 59);
				}

				// 在行尾添加[in 所处的文件 : line = xx]
				div.AddRow(strtool::get_text(cn_file_replace_in_file, htmltool::get_file_html(replaceInfo.m_inFile).c_str(), htmltool::get_number_html(replaceInfo.m_line).c_str()), 5);
			}

			for (auto itr : replaceLine.m_frontNamespace)
			{
				const std::string &ns_name = itr.first;
				const std::string &ns_decl = itr.second;

				div.AddRow(strtool::get_text(cn_file_add_front_using_ns, htmltool::get_include_html(ns_decl).c_str()), 5);
				div.AddRow(strtool::get_text(cn_file_add_front_using_ns, htmltool::get_include_html(ns_name).c_str()), 5);
			}

			for (auto itr : replaceLine.m_backNamespace)
			{
				const std::string &ns_name = itr.first;
				const std::string &ns_decl = itr.second;

				div.AddRow(strtool::get_text(cn_file_add_back_using_ns, htmltool::get_include_html(ns_decl).c_str()), 5);
				div.AddRow(strtool::get_text(cn_file_add_back_using_ns, htmltool::get_include_html(ns_name).c_str()), 5);
			}

			div.AddRow("");
		}
	}

	// 打印该文件产生的编译错误
	void ParsingFile::PrintCompileError(const CompileErrorHistory &errHistory)
	{
		if (errHistory.m_errNum <= 0)
		{
			return;
		}

		HtmlDiv &div = HtmlLog::instance.m_newDiv;
		div.AddRow(" ", 1, 100, false, true);

		div.AddRow(cn_error, 1, 100, false, true);

		for (const std::string& errTip : errHistory.m_errTips)
		{
			div.AddRow(errTip, 2, 100, true, true);
			div.AddRow(" ", 1, 100, false, true);
		}

		// div.AddRow(" ", 1, 100, false, true);

		if (!errHistory.m_fatalErrors.empty())
		{
			std::string errTexts;

			int i		= 0;
			int size	= errHistory.m_fatalErrors.size();

			for (int err : errHistory.m_fatalErrors)
			{
				errTexts += htmltool::get_number_html(err);

				if (++i < size)
				{
					errTexts += ",";
				}
			}

			std::string tip = get_text(cn_error_fatal, errTexts.c_str());
			div.AddRow(tip, 1, 100, true, true);
		}

		if (errHistory.m_hasTooManyError)
		{
			std::string tip = get_text(cn_error_too_many, htmltool::get_number_html(errHistory.m_errNum).c_str());
			div.AddRow(tip, 1, 100, true, true);
		}
		else if (errHistory.m_fatalErrors.empty())
		{
			if (errHistory.m_errNum > 0)
			{
				std::string tip = get_text(cn_error_ignore, htmltool::get_number_html(errHistory.m_errNum).c_str());
				div.AddRow(tip, 1, 100, true, true);
			}
		}

		div.AddRow("");
	}

	// 打印清理日志
	void ParsingFile::PrintCleanLog() const
	{
		HtmlDiv &div = HtmlLog::instance.m_newDiv;

		PrintCompileError(m_compileErrorHistory);

		FileHistoryMap files;
		TakeAllInfoTo(files);

		int i = 0;

		for (auto itr : files)
		{
			const FileHistory &history = itr.second;
			if (!history.IsNeedClean())
			{
				continue;
			}

			if (Project::instance.m_verboseLvl < VerboseLvl_2)
			{
				string ext = strtool::get_ext(history.m_filename);
				if (!cpptool::is_cpp(ext))
				{
					continue;
				}
			}

			const char *tip = (history.m_compileErrorHistory.HaveFatalError() ? cn_file_history_compile_error : cn_file_history);

			div.AddRow(strtool::get_text(tip, htmltool::get_number_html(++i).c_str(), htmltool::get_file_html(history.m_filename).c_str()), 1);

			PrintFileHistory(history);
		}
	}

	// 打印各文件内的命名空间
	void ParsingFile::PrintNamespace() const
	{
		int num = 0;
		for (auto itr : m_namespaces)
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

		for (auto itr : m_namespaces)
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
				div.AddRow("declare namespace = " + htmltool::get_include_html(ns), 3);
			}

			div.AddRow("");
		}
	}

	// 打印各文件内的using namespace
	void ParsingFile::PrintUsingNamespace() const
	{
		int num = 0;
		for (auto itr : m_usingNamespaces)
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

		for (auto itr : m_usingNamespaces)
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

	// 打印各文件内应保留的using namespace
	void ParsingFile::PrintRemainUsingNamespace() const
	{
		std::map<FileID, std::set<std::string>>	remainNamespaces;
		for (auto itr : m_remainUsingNamespaces)
		{
			SourceLocation loc		= itr.first;
			const NamespaceInfo &ns	= itr.second;

			remainNamespaces[GetFileID(loc)].insert(ns.ns_name);
		}

		int num = 0;
		for (auto itr : remainNamespaces)
		{
			FileID file = itr.first;

			if (!IsNeedPrintFile(file))
			{
				continue;
			}

			++num;
		}

		HtmlDiv &div = HtmlLog::instance.m_newDiv;
		div.AddRow(AddPrintIdx() + ". each file's remain using namespace: file count = " + htmltool::get_number_html(num), 1);

		for (auto itr : remainNamespaces)
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
				div.AddRow("remain using namespace = " + htmltool::get_include_html(ns), 3);
			}

			div.AddRow("");
		}
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
			string rawInclude2 = GetRawIncludeStr(f2);
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
		// 若本文件有严重编译错误或编译错误数过多，则禁止改动
		if (m_compileErrorHistory.HaveFatalError())
		{
			return;
		}

		if (Project::instance.m_isDeepClean)
		{
			CleanAllFile();
		}
		else
		{
			CleanMainFile();
		}

		// 仅当开启覆盖选项时，才将变动回写到c++文件
		if (Project::instance.m_isOverWrite)
		{
			m_rewriter->overwriteChangedFiles();
		}
	}

	// 替换指定范围文本
	void ParsingFile::ReplaceText(FileID file, int beg, int end, string text)
	{
		SourceLocation fileBegLoc	= m_srcMgr->getLocForStartOfFile(file);
		SourceLocation begLoc		= fileBegLoc.getLocWithOffset(beg);
		SourceLocation endLoc		= fileBegLoc.getLocWithOffset(end);

		SourceRange range(begLoc, endLoc);

		// llvm::outs() << "\n------->replace text = [" << get_source_of_range(range) << "] in [" << get_absolute_file_name(file) << "]\n";

		m_rewriter->ReplaceText(begLoc, end - beg, text);
	}

	// 将文本插入到指定位置之前
	// 例如：假设有"abcdefg"文本，则在c处插入123的结果将是："ab123cdefg"
	void ParsingFile::InsertText(FileID file, int loc, string text)
	{
		SourceLocation fileBegLoc	= m_srcMgr->getLocForStartOfFile(file);
		SourceLocation insertLoc	= fileBegLoc.getLocWithOffset(loc);

		m_rewriter->InsertText(insertLoc, text, true, true);
	}

	// 移除指定范围文本，若移除文本后该行变为空行，则将该空行一并移除
	void ParsingFile::RemoveText(FileID file, int beg, int end)
	{
		SourceLocation fileBegLoc	= m_srcMgr->getLocForStartOfFile(file);
		if (fileBegLoc.isInvalid())
		{
			llvm::errs() << "\n------->error: fileBegLoc.isInvalid(), remove text in [" << GetAbsoluteFileName(file) << "] failed!\n";
			return;
		}

		SourceLocation begLoc		= fileBegLoc.getLocWithOffset(beg);
		SourceLocation endLoc		= fileBegLoc.getLocWithOffset(end);

		SourceRange range(begLoc, endLoc);

		Rewriter::RewriteOptions rewriteOption;
		rewriteOption.IncludeInsertsAtBeginOfRange	= false;
		rewriteOption.IncludeInsertsAtEndOfRange	= false;
		rewriteOption.RemoveLineIfEmpty				= false;

		// llvm::outs() << "\n------->remove text = [" << get_source_of_range(range) << "] in [" << get_absolute_file_name(file) << "]\n";

		bool err = m_rewriter->RemoveText(range.getBegin(), end - beg, rewriteOption);
		if (err)
		{
			llvm::errs() << "\n------->error: remove text = [" << GetSourceOfRange(range) << "] in [" << GetAbsoluteFileName(file) << "] failed!\n";
		}
	}

	// 移除指定文件内的无用#include
	void ParsingFile::CleanByUnusedLine(const FileHistory &eachFile, FileID file)
	{
		if (eachFile.m_unusedLines.empty())
		{
			return;
		}

		for (auto unusedLineItr : eachFile.m_unusedLines)
		{
			int line				= unusedLineItr.first;
			UselessLine &unusedLine	= unusedLineItr.second;

			RemoveText(file, unusedLine.m_beg, unusedLine.m_end);

			for (auto itr : unusedLine.m_usingNamespace)
			{
				const std::string &ns_name = itr.first;
				const std::string &ns_decl = itr.second;

				InsertText(file, unusedLine.m_beg, ns_decl + eachFile.GetNewLineWord());
				InsertText(file, unusedLine.m_beg, ns_name + eachFile.GetNewLineWord());
			}
		}
	}

	// 在指定文件内添加前置声明
	void ParsingFile::CleanByForward(const FileHistory &eachFile, FileID file)
	{
		if (eachFile.m_forwards.empty())
		{
			return;
		}

		for (auto forwardItr : eachFile.m_forwards)
		{
			int line						= forwardItr.first;
			const ForwardLine &forwardLine	= forwardItr.second;

			std::stringstream text;

			for (const string &cxxRecord : forwardLine.m_classes)
			{
				text << cxxRecord;
				text << eachFile.GetNewLineWord();
			}

			InsertText(file, forwardLine.m_offsetAtFile, text.str());
		}
	}

	// 在指定文件内替换#include
	void ParsingFile::CleanByReplace(const FileHistory &eachFile, FileID file)
	{
		if (eachFile.m_replaces.empty())
		{
			return;
		}

		for (auto replaceItr : eachFile.m_replaces)
		{
			int line						= replaceItr.first;
			const ReplaceLine &replaceLine	= replaceItr.second;

			// 若是被-include参数强制引入，则跳过，因为替换并没有效果
			if (replaceLine.m_isSkip)
			{
				continue;
			}

			const char* newLineWord = eachFile.GetNewLineWord();

			// 1. 先替换#include
			{
				std::stringstream text;

				for (const ReplaceTo &replaceInfo : replaceLine.m_newInclude)
				{
					text << replaceInfo.m_newText;
					text << newLineWord;
				}

				ReplaceText(file, replaceLine.m_beg, replaceLine.m_end, text.str());
			}

			// 2. 在原来的#include前添加namespace声明和using namespace
			{
				std::stringstream text;

				for (auto itr : replaceLine.m_frontNamespace)
				{
					const std::string &ns_name = itr.first;
					const std::string &ns_decl = itr.second;

					text << ns_decl << newLineWord << ns_name << newLineWord;
				}

				InsertText(file, replaceLine.m_beg, text.str());
			}

			// 3. 在原来的#include后添加namespace声明和using namespace
			{
				std::stringstream text;

				for (auto itr : replaceLine.m_backNamespace)
				{
					const std::string &ns_name = itr.first;
					const std::string &ns_decl = itr.second;

					text << ns_decl << newLineWord << ns_name << newLineWord;
				}

				InsertText(file, replaceLine.m_end, text.str());
			}
		}
	}

	// 清理指定文件
	void ParsingFile::CleanBy(const FileHistoryMap &files)
	{
		std::map<std::string, FileID> allFiles;

		// 建立文件名到文件FileID的map映射（注意：同一个文件可能被包含多次，FileID是不一样的，这里只存入最小的FileID）
		{
			for (FileID file : m_files)
			{
				const string &name = GetAbsoluteFileName(file);

				if (!Project::instance.CanClean(name))
				{
					continue;
				}

				if (allFiles.find(name) == allFiles.end())
				{
					allFiles.insert(std::make_pair(name, file));
				}
			}
		}

		for (auto itr : files)
		{
			const string &fileName		= itr.first;

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

			// 找到文件名对应的文件ID（注意：同一个文件可能被包含多次，FileID是不一样的，这里取出来的是最小的FileID）
			auto findItr = allFiles.find(fileName);
			if (findItr == allFiles.end())
			{
				continue;
			}

			FileID file					= findItr->second;
			const FileHistory &eachFile	= ProjectHistory::instance.m_files[fileName];

			if (eachFile.m_isSkip)
			{
				continue;
			}

			CleanByReplace(eachFile, file);
			CleanByForward(eachFile, file);
			CleanByUnusedLine(eachFile, file);

			ProjectHistory::instance.OnCleaned(fileName);
		}
	}

	// 清理所有有必要清理的文件
	void ParsingFile::CleanAllFile()
	{
		CleanBy(ProjectHistory::instance.m_files);
	}

	// 清理主文件
	void ParsingFile::CleanMainFile()
	{
		FileHistoryMap root;

		// 仅取出主文件的待清理记录
		{
			string rootFileName = GetAbsoluteFileName(m_srcMgr->getMainFileID());

			auto rootItr = ProjectHistory::instance.m_files.find(rootFileName);
			if (rootItr != ProjectHistory::instance.m_files.end())
			{
				root.insert(*rootItr);
			}
		}

		CleanBy(root);
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
		for (auto itr : m_uses)
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

		for (auto itr : m_uses)
		{
			FileID file = itr.first;

			if (!CanClean(file))
			{
				continue;
			}

			std::set<FileID> &be_uses = itr.second;

			div.AddRow("file = " + htmltool::get_file_html(GetAbsoluteFileName(file)), 2);

			for (FileID be_used_file : be_uses)
			{
				div.AddRow("old include = " + htmltool::get_include_html(GetRawIncludeStr(be_used_file)), 3, 45);
				div.AddGrid("-> relative include = " + htmltool::get_include_html(GetRelativeIncludeStr(file, be_used_file)), 54);
			}

			div.AddRow("");
		}
	}

	// 用于调试跟踪：打印是否有文件的被包含串被遗漏
	void ParsingFile::PrintNotFoundIncludeLocForDebug()
	{
		HtmlDiv &div = HtmlLog::instance.m_newDiv;
		div.AddRow(AddPrintIdx() + ". not found include loc:", 1);

		for (auto itr : m_uses)
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
		int verbose = Project::instance.m_verboseLvl;
		if (verbose <= VerboseLvl_0)
		{
			return;
		}
		else if (verbose <= VerboseLvl_1 && Project::instance.m_onlyHas1File)
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
			PrintUse();
			PrintNewUse();

			PrintAllCycleUsedNames();
			PrintRootCycleUsedNames();

			PrintRootCycleUse();
			PrintAllCycleUse();

			PrintCycleUsedChildren();
		}

		if (verbose >= VerboseLvl_4)
		{
			PrintAllFile();
			PrintInclude();

			PrintHeaderSearchPath();
			PrintRelativeInclude();
			PrintParent();
			PrintChildren();
			PrintNamespace();
			PrintUsingNamespace();
			PrintRemainUsingNamespace();
		}

		if (verbose >= VerboseLvl_5)
		{
			PrintNotFoundIncludeLocForDebug();
		}

		HtmlLog::instance.AddDiv(div);
	}

	// 合并可被移除的#include行
	void ParsingFile::MergeUnusedLineTo(const FileHistory &newFile, FileHistory &oldFile) const
	{
		FileHistory::UnusedLineMap &oldLines = oldFile.m_unusedLines;

		for (FileHistory::UnusedLineMap::iterator oldLineItr = oldLines.begin(), end = oldLines.end(); oldLineItr != end; )
		{
			int line = oldLineItr->first;
			UselessLine &unusedLine = oldLineItr->second;

			if (newFile.IsLineUnused(line))
			{
				auto newLineItr = newFile.m_unusedLines.find(line);
				const UselessLine &newUnusedLine = newLineItr->second;

				unusedLine.m_usingNamespace.insert(newUnusedLine.m_usingNamespace.begin(), newUnusedLine.m_usingNamespace.end());
				++oldLineItr;
			}
			else
			{
				// llvm::outs() << oldFile.m_filename << ": conflict at line " << line << " -> " << oldLineItr->second.m_text << "\n";
				oldLines.erase(oldLineItr++);
			}
		}
	}

	// 合并可新增的前置声明
	void ParsingFile::MergeForwardLineTo(const FileHistory &newFile, FileHistory &oldFile) const
	{
		for (auto newLineItr : newFile.m_forwards)
		{
			int line				= newLineItr.first;
			ForwardLine &newLine	= newLineItr.second;

			auto oldLineItr = oldFile.m_forwards.find(line);
			if (oldLineItr != oldFile.m_forwards.end())
			{
				ForwardLine &oldLine	= oldFile.m_forwards[line];
				oldLine.m_classes.insert(newLine.m_classes.begin(), newLine.m_classes.end());
			}
			else
			{
				oldFile.m_forwards[line] = newLine;
			}
		}
	}

	// 合并可替换的#include
	void ParsingFile::MergeReplaceLineTo(const FileHistory &newFile, FileHistory &oldFile) const
	{
		// 若处理其他cpp文件时并未在本文件产生替换记录，则说明本文件内没有需要替换的#include
		if (oldFile.m_replaces.empty())
		{
			return;
		}

		FileID newFileID;
		auto newFileItr = m_splitReplaces.begin();

		{
			for (auto itr = m_splitReplaces.begin(); itr != m_splitReplaces.end(); ++itr)
			{
				FileID top = itr->first;

				if (GetAbsoluteFileName(top) == oldFile.m_filename)
				{
					newFileID	= top;
					newFileItr	= itr;
					break;
				}
			}
		}

		// 说明该文件未新生成任何替换记录，此时应将该文件内所有旧替换记录全部删除
		if (newFileID.isInvalid())
		{
			// llvm::outs() << "error, merge_replace_line_to not found = " << oldFile.m_filename << "\n";
			oldFile.m_replaces.clear();
			return;
		}

		const ChildrenReplaceMap &newReplaceMap	= newFileItr->second;

		// 合并新增替换记录到历史记录中
		for (auto oldLineItr = oldFile.m_replaces.begin(), end = oldFile.m_replaces.end(); oldLineItr != end; )
		{
			int line				= oldLineItr->first;
			ReplaceLine &oldLine	= oldLineItr->second;

			// 该行是否发生冲突了
			auto conflictItr = newFile.m_replaces.find(line);
			bool conflict = (conflictItr != newFile.m_replaces.end());

			// 若发生冲突，则分析被新的取代文件与旧的取代文件是否有直系后代关系
			if (conflict)
			{
				const ReplaceLine &newLine	= conflictItr->second;

				// 找到该行旧的#include对应的FileID
				FileID beReplaceFileID;

				{
					for (auto childItr : newReplaceMap)
					{
						FileID child = childItr.first;
						if (GetAbsoluteFileName(child) == newLine.m_oldFile)
						{
							beReplaceFileID = child;
							break;
						}
					}
				}

				if (beReplaceFileID.isInvalid())
				{
					++oldLineItr;
					continue;
				}

				// 若旧的取代文件是新的取代文件的祖先，则保留旧的替换信息
				if (IsAncestor(beReplaceFileID, oldLine.m_oldFile.c_str()))
				{
					++oldLineItr;
				}
				// 若新的取代文件是旧的取代文件的祖先，则改为使用新的替换信息
				else if(IsAncestor(oldLine.m_oldFile.c_str(), beReplaceFileID))
				{
					oldLine.m_newInclude = newLine.m_newInclude;
					oldLine.m_frontNamespace.insert(newLine.m_frontNamespace.begin(), newLine.m_frontNamespace.end());
					oldLine.m_backNamespace.insert(newLine.m_backNamespace.begin(), newLine.m_backNamespace.end());
					++oldLineItr;
				}
				// 否则，若没有直系关系，则该行无法被替换，删除该行原有的替换记录
				else
				{
					// llvm::outs() << "merge_replace_line_to: " << oldFile.m_filename << " should remove conflict old line = " << line << " -> " << oldLine.m_oldText << "\n";
					oldFile.m_replaces.erase(oldLineItr++);
				}
			}
			// 若该行没有新的替换记录，说明该行无法被替换，删除该行旧的替换记录
			else
			{
				// llvm::outs() << "merge_replace_line_to: " << oldFile.m_filename << " should remove old line = " << line << " -> " << oldLine.m_oldText << "\n";
				oldFile.m_replaces.erase(oldLineItr++);
			}
		}
	}

	void ParsingFile::MergeCountTo(FileHistoryMap &oldFiles) const
	{
		for (auto itr : m_parentIDs)
		{
			FileID child		= itr.first;
			string fileName		= GetAbsoluteFileName(child);
			FileHistory &oldFile	= oldFiles[fileName];

			++oldFile.m_oldBeIncludeCount;

			if (m_unusedLocs.find(m_srcMgr->getIncludeLoc(child)) != m_unusedLocs.end())
			{
				++oldFile.m_newBeIncludeCount;
			}
		}

		for (FileID file : m_allCycleUse)
		{
			string fileName		= GetAbsoluteFileName(file);
			FileHistory &oldFile	= oldFiles[fileName];

			++oldFile.m_beUseCount;
		}
	}

	// 将当前cpp文件产生的待清理记录与之前其他cpp文件产生的待清理记录合并
	void ParsingFile::MergeTo(FileHistoryMap &oldFiles) const
	{
		FileHistoryMap newFiles;
		TakeAllInfoTo(newFiles);

		// 若本文件有严重编译错误或编译错误数过多，则仅合并编译错误历史，用于打印
		if (m_compileErrorHistory.HaveFatalError())
		{
			std::string rootFile	= GetAbsoluteFileName(m_srcMgr->getMainFileID());
			oldFiles[rootFile]		= newFiles[rootFile];
			return;
		}

		for (auto fileItr : newFiles)
		{
			const string &fileName	= fileItr.first;
			const FileHistory &newFile	= fileItr.second;

			auto findItr = oldFiles.find(fileName);

			bool found = (findItr != oldFiles.end());
			if (!found)
			{
				oldFiles[fileName] = newFile;
			}
			else
			{
				FileHistory &oldFile = findItr->second;

				MergeUnusedLineTo(newFile, oldFile);
				MergeForwardLineTo(newFile, oldFile);
				MergeReplaceLineTo(newFile, oldFile);
			}
		}

		MergeCountTo(oldFiles);
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
	void ParsingFile::TakeAllInfoTo(FileHistoryMap &out) const
	{
		// 先将当前cpp文件使用到的文件全存入map中
		for (FileID file : m_allCycleUse)
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
		TakeUnusedLineByFile(out);

		// 2. 将新增的前置声明按文件进行存放
		TakeNewForwarddeclByFile(out);

		// 3. 将文件内的#include替换按文件进行存放
		TakeReplaceByFile(out);

		// 4. 取出本文件的编译错误历史
		TakeCompileErrorHistory(out);
	}

	// 将可清除的行按文件进行存放
	void ParsingFile::TakeUnusedLineByFile(FileHistoryMap &out) const
	{
		for (SourceLocation loc : m_unusedLocs)
		{
			PresumedLoc presumedLoc = m_srcMgr->getPresumedLoc(loc);
			if (presumedLoc.isInvalid())
			{
				llvm::outs() << "------->error: take_unused_line_by_file getPresumedLoc failed\n";
				continue;
			}

			string fileName			= pathtool::get_absolute_path(presumedLoc.getFilename());
			if (!Project::instance.CanClean(fileName))
			{
				// llvm::outs() << "------->error: !Vsproject::instance.has_file(fileName) : " << fileName << "\n";
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
			UselessLine &unusedLine	= eachFile.m_unusedLines[line];

			unusedLine.m_beg	= m_srcMgr->getFileOffset(lineRange.getBegin());
			unusedLine.m_end	= m_srcMgr->getFileOffset(nextLine.getBegin());
			unusedLine.m_text	= GetSourceOfRange(lineRange);

			{
				GetMissingNamespace(loc, unusedLine.m_usingNamespace);
			}
		}
	}

	// 将新增的前置声明按文件进行存放
	void ParsingFile::TakeNewForwarddeclByFile(FileHistoryMap &out) const
	{
		if (m_forwardDecls.empty())
		{
			return;
		}

		for (auto itr : m_forwardDecls)
		{
			FileID file									= itr.first;
			std::set<const CXXRecordDecl*> &cxxRecords	= itr.second;

			string fileName			= GetAbsoluteFileName(file);

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
					llvm::outs() << "------->error: take_new_forwarddecl_by_file getPresumedLoc failed\n";
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

					forwardLine.m_offsetAtFile	= m_srcMgr->getFileOffset(insertLoc);
					forwardLine.m_oldText		= GetSourceOfLine(insertLoc);
					forwardLine.m_classes.insert(cxxRecordName);

					{
						SourceLocation fileStart = m_srcMgr->getLocForStartOfFile(GetFileID(insertLoc));
						if (fileStart.getLocWithOffset(forwardLine.m_offsetAtFile) != insertLoc)
						{
							llvm::outs() << "error: fileStart.getLocWithOffset(forwardLine.m_offsetAtFile) != insertLoc \n";
						}
					}
				}
			}
		}
	}

	// 将文件替换记录按父文件进行归类
	void ParsingFile::SplitReplaceByFile(ReplaceFileMap &replaces) const
	{
		for (auto itr : m_replaces)
		{
			FileID file						= itr.first;
			std::set<FileID> &to_replaces	= itr.second;

			auto parentItr = m_parentIDs.find(file);
			if (parentItr == m_parentIDs.end())
			{
				continue;
			}

			FileID parent = parentItr->second;
			replaces[parent].insert(itr);
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
	void ParsingFile::TakeBeReplaceOfFile(FileHistory &eachFile, FileID top, const ChildrenReplaceMap &childernReplaces) const
	{
		for (auto itr : childernReplaces)
		{
			FileID oldFile						= itr.first;
			const std::set<FileID> &to_replaces	= itr.second;

			// 取出该行#include的替换信息[行号 -> 被替换成的#include列表]
			{
				bool isBeForceIncluded		= IsForceIncluded(oldFile);

				SourceLocation include_loc	= m_srcMgr->getIncludeLoc(oldFile);
				SourceRange	lineRange		= GetCurLineWithLinefeed(include_loc);
				int line					= (isBeForceIncluded ? 0 : GetLineNo(include_loc));

				ReplaceLine &replaceLine	= eachFile.m_replaces[line];
				replaceLine.m_oldFile		= GetAbsoluteFileName(oldFile);
				replaceLine.m_oldText		= GetRawIncludeStr(oldFile);
				replaceLine.m_beg			= m_srcMgr->getFileOffset(lineRange.getBegin());
				replaceLine.m_end			= m_srcMgr->getFileOffset(lineRange.getEnd());
				replaceLine.m_isSkip		= isBeForceIncluded || IsPrecompileHeader(oldFile);	// 记载是否是强制包含

				for (FileID replace_file : to_replaces)
				{
					SourceLocation deep_include_loc	= m_srcMgr->getIncludeLoc(replace_file);

					ReplaceTo replaceTo;

					// 记录[旧#include、新#include]
					replaceTo.m_oldText	= GetRawIncludeStr(replace_file);
					replaceTo.m_newText	= GetRelativeIncludeStr(GetParent(oldFile), replace_file);

					// 记录[所处的文件、所处行号]
					replaceTo.m_line		= GetLineNo(deep_include_loc);
					replaceTo.m_fileName	= GetAbsoluteFileName(replace_file);
					replaceTo.m_inFile	= GetAbsoluteFileName(GetFileID(deep_include_loc));

					replaceLine.m_newInclude.push_back(replaceTo);

					GetMissingNamespace(include_loc, deep_include_loc, replaceLine.m_frontNamespace, replaceLine.m_backNamespace);
				}
			}
		}
	}

	// 取出各文件的#include替换信息
	void ParsingFile::TakeReplaceByFile(FileHistoryMap &out) const
	{
		if (m_replaces.empty())
		{
			return;
		}

		for (auto itr : m_splitReplaces)
		{
			FileID top									= itr.first;
			const ChildrenReplaceMap &childrenReplaces	= itr.second;

			string fileName			= GetAbsoluteFileName(top);

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
}