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

ParsingFile* ParsingFile::g_nowFile = nullptr;

namespace cxxclean
{
	// set减去set
	template <typename T>
	inline void Del(std::set<T> &a, const std::set<T> &b)
	{
		for (const T &t : b)
		{
			a.erase(t);
		}
	}

	// set减去map
	template <typename Key, typename Val>
	inline void Del(std::set<Key> &a, const std::map<Key, Val> &b)
	{
		for (const auto &itr : b)
		{
			a.erase(itr.first);
		}
	}

	// set减去set
	template <typename T>
	inline void Add(std::set<T> &a, const std::set<T> &b)
	{
		a.insert(b.begin(), b.end());
	}

	// set加上map
	template <typename Key, typename Val>
	inline void Add(std::set<Key> &a, const std::map<Key, Val> &b)
	{
		for (const auto &itr : b)
		{
			a.insert(itr.first);
		}
	}

	// set加上set中符合条件的成员
	template <typename Key, typename Op>
	inline void AddIf(std::set<Key> &a, const std::set<Key> &b, const Op& op)
	{
		for (const Key &key : b)
		{
			if (op(key))
			{
				a.insert(key);
			}
		}
	}

	// set加上map中指定键对应的值
	template <typename Key, typename Val>
	inline void AddByKey(std::set<Val> &a, const std::map<Key, std::set<Val>> &b, const Key &key)
	{
		auto &itr = b.find(key);
		if (itr != b.end())
		{
			Add(a, itr->second);
		}
	}

	template <typename Container, typename Op>
	inline void EraseIf(Container& container, const Op& op)
	{
		for (auto it = container.begin(); it != container.end(); )
		{
			if (op(*it)) container.erase(it++);
			else ++it;
		}
	}

	template <typename Container, typename Op>
	inline void MapEraseIf(Container& container, const Op& op)
	{
		for (auto it = container.begin(); it != container.end(); )
		{
			if (op(it->first, it->second)) container.erase(it++);
			else ++it;
		}
	}

	template <typename Container, typename Key>
	inline bool Has(Container& container, const Key &key)
	{
		return container.find(key) != container.end();
	}

	template <typename Key>
	inline bool HasInMap(const std::map<Key, std::set<Key>> &container, const Key &by, const Key &kid)
	{
		auto &itr = container.find(by);
		if (itr == container.end())
		{
			return false;
		}

		return Has(itr->second, kid);
	}

	template <typename Expand>
	FileSet GetChain(FileID top, const Expand& expand)
	{
		FileSet todo;
		FileSet done;

		todo.insert(top);

		while (!todo.empty())
		{
			FileID cur = *todo.begin();
			todo.erase(todo.begin());

			if (done.find(cur) == done.end())
			{
				done.insert(cur);
				expand(done, todo, cur);
			}
		}

		return done;
	}

	ParsingFile::ParsingFile(clang::CompilerInstance &compiler)
	{
		m_compiler	= &compiler;
		m_srcMgr	= &compiler.getSourceManager();
		m_printIdx	= 0;
		g_nowFile	= this;
		m_root		= m_srcMgr->getMainFileID();

		m_rewriter.setSourceMgr(*m_srcMgr, compiler.getLangOpts());
		m_headerSearchPaths = TakeHeaderSearchPaths(m_compiler->getPreprocessor().getHeaderSearchInfo());
	}

	ParsingFile::~ParsingFile()
	{
		g_nowFile = nullptr;
	}

	// 添加父文件关系
	inline void ParsingFile::AddParent(FileID child, FileID parent)
	{
		if (child != parent && child.isValid() && parent.isValid())
		{
			m_parents[child] = parent;
		}
	}

	// 添加包含文件记录
	inline void ParsingFile::AddInclude(FileID file, FileID beInclude)
	{
		if (file != beInclude && file.isValid() && beInclude.isValid())
		{
			m_includes[file].insert(beInclude);
		}
	}

	// 添加成员文件
	void ParsingFile::AddFile(FileID file)
	{
		if (file.isInvalid())
		{
			return;
		}

		m_files.insert(file);

		const std::string fileName = GetAbsoluteFileName(file);
		if (!fileName.empty())
		{
			const std::string lowerFileName = tolower(fileName);

			m_fileNames.insert(std::make_pair(file, fileName));
			m_lowerFileNames.insert(std::make_pair(file, lowerFileName));
			m_sameFiles[lowerFileName].insert(file);
		}

		FileID parent = m_srcMgr->getFileID(m_srcMgr->getIncludeLoc(file));
		if (parent.isValid())
		{
			if (IsForceIncluded(file))
			{
				parent = m_root;
			}

			AddParent(file, parent);
			AddInclude(parent, file);
		}
	}

	// 获取头文件搜索路径
	vector<ParsingFile::HeaderSearchDir> ParsingFile::TakeHeaderSearchPaths(const clang::HeaderSearch &headerSearch) const
	{
		typedef clang::HeaderSearch::search_dir_iterator search_iterator;

		IncludeDirMap dirs;

		auto AddIncludeDir = [&](search_iterator beg, search_iterator end, SrcMgr::CharacteristicKind includeKind)
		{
			// 获取系统头文件搜索路径
			for (auto itr = beg; itr != end; ++itr)
			{
				if (const DirectoryEntry* entry = itr->getDir())
				{
					const string path = pathtool::fix_path(entry->getName());
					dirs.insert(make_pair(path, includeKind));
				}
			}
		};

		// 获取系统头文件搜索路径
		AddIncludeDir(headerSearch.system_dir_begin(), headerSearch.system_dir_end(), SrcMgr::C_System);
		AddIncludeDir(headerSearch.search_dir_begin(), headerSearch.search_dir_end(), SrcMgr::C_User);

		return SortHeaderSearchPath(dirs);
	}

	// 将头文件搜索路径根据长度由长到短排列
	std::vector<ParsingFile::HeaderSearchDir> ParsingFile::SortHeaderSearchPath(const IncludeDirMap& includeDirsMap) const
	{
		std::vector<HeaderSearchDir> dirs;

		for (const auto &itr : includeDirsMap)
		{
			const string &path						= itr.first;
			SrcMgr::CharacteristicKind includeKind	= itr.second;

			string absolutePath = pathtool::get_absolute_path(path.c_str());

			dirs.push_back(HeaderSearchDir(absolutePath, includeKind));
		}

		// 根据长度由长到短排列
		sort(dirs.begin(), dirs.end(), [](const ParsingFile::HeaderSearchDir& left, const ParsingFile::HeaderSearchDir& right)
		{
			return left.m_dir.length() > right.m_dir.length();
		});

		return dirs;
	}

	// 根据头文件搜索路径，将绝对路径转换为双引号包围的文本串，
	// 例如：假设有头文件搜索路径"d:/a/b/c" 则"d:/a/b/c/d/e.h" -> "d/e.h"
	string ParsingFile::GetQuotedIncludeStr(const char *absoluteFilePath) const
	{
		string path = pathtool::simplify_path(absoluteFilePath);

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

	// 获取同名文件列表
	FileSet ParsingFile::GetAllSameFiles(FileID file) const
	{
		auto sameItr = m_sameFiles.find(GetLowerFileNameInCache(file));
		if (sameItr != m_sameFiles.end())
		{
			const FileSet &sames = sameItr->second;
			return std::move(sames);
		}
		else
		{
			FileSet sames;
			sames.insert(file);
			return sames;
		}
	}

	// 2个文件是否文件名一样
	inline bool ParsingFile::IsSameName(FileID a, FileID b) const
	{
		return (std::string(GetLowerFileNameInCache(a)) == GetLowerFileNameInCache(b));
	}

	// 为当前cpp文件的清理作前期准备
	void ParsingFile::InitCpp()
	{
		// 1. 生成每个文件的后代文件集（分析过程中需要用到）
		for (FileID file : m_files)
		{
			for (FileID child = file, parent; (parent = GetParent(child)).isValid(); child = parent)
			{
				m_kids[parent].insert(file);
			}
		}

		// 2. 保留被反复包含的文件
		MapEraseIf(m_sameFiles, [&](const std::string&, const FileSet &sameFiles)
		{
			return sameFiles.size() <= 1;
		});
	}

	// 开始分析
	void ParsingFile::Analyze()
	{
		/*
			下面这段代码是本工具的主要处理思路
		*/
		if (Project::IsCleanModeOpen(CleanMode_Need))
		{
			GenerateForceIncludes();
			GenerateOutFileAncestor();
			GenerateKidBySame();
			GenerateUserUse();
			GenerateMinUse();
			GenerateForwardClass();
		}
		else
		{
			GenerateRely();

			GenerateUnusedInclude();
			GenerateReplace();
		}

		TakeHistorys(m_historys);
		MergeTo(ProjectHistory::instance.m_files);
	}

	FileSet ParsingFile::GetUseChain(FileID top) const
	{
		FileSet chain = GetChain(top, [&](const FileSet &done, FileSet &todo, FileID cur)
		{
			// 1. 若当前文件不依赖其他文件，则跳过
			auto & useItr = m_userUses.find(cur);
			if (useItr != m_userUses.end())
			{
				// 2. todo集合 += 当前文件依赖的其他文件
				const FileSet &useFiles = useItr->second;
				AddIf(todo, useFiles, [&top](FileID beuse)
				{
					// 只扩展后代文件
					return g_nowFile->IsAncestorBySame(beuse, top);
				});
			}
		});

		chain.erase(top);
		return chain;
	}

	bool ParsingFile::MergeMin()
	{
		bool smaller = false;

		// 合并
		for (auto &itr : m_min)
		{
			FileID top		= itr.first;
			FileSet &kids	= itr.second;

			if (kids.empty())
			{
				m_min.erase(top);
				return true;
			}

			FileSet minKids = kids;
			FileSet eraseList;

			for (FileID kid : kids)
			{
				for (FileID forceInclude : m_forceIncludes)
				{
					FileID same = GetBestKid(forceInclude, kid);
					if (same != kid)
					{
						LogInfoByLvl(LogLvl_3, "force includes: erase [kid](top = " << GetDebugFileName(top) << ", forceInclude = " << GetDebugFileName(forceInclude) << ", kid = " << GetDebugFileName(kid) << ")");

						eraseList.insert(kid);
						break;
					}
				}

				minKids.erase(kid);

				for (FileID other : minKids)
				{
					if (IsSameName(kid, other))
					{
						LogInfoByLvl(LogLvl_3, "[kid]'name = [other]'name: erase [other](top = " << GetDebugFileName(top) << ", kid = " << GetDebugFileName(kid) << ", other = " << GetDebugFileName(other) << ")");

						eraseList.insert(other);
						break;
					}

					if (HasMinKidBySameName(kid, other))
					{
						LogInfoByLvl(LogLvl_3, "[kid]'s child contains [other]: erase [other](top = " << GetDebugFileName(top) << ", kid = " << GetDebugFileName(kid) << ", other = " << GetDebugFileName(other) << ")");

						eraseList.insert(other);
						break;
					}

					if (HasMinKidBySameName(other, kid))
					{
						LogInfoByLvl(LogLvl_3, "[other]'s child contains [kid]: erase [kid](top = " << GetDebugFileName(top) << ", other = " << GetDebugFileName(other) << ", kid = " << GetDebugFileName(kid) << ")");

						eraseList.insert(kid);
						break;
					}
				}
			}

			if (!eraseList.empty())
			{
				Del(kids, eraseList);
				smaller = true;
			}
		}

		return smaller;
	}

	inline bool ParsingFile::IsUserFile(FileID file) const
	{
		return CanClean(file);
	}

	inline bool ParsingFile::IsOuterFile(FileID file) const
	{
		return file.isValid() && (IsAncestorForceInclude(file) || !IsUserFile(file));
	}

	inline FileID ParsingFile::GetOuterFileAncestor(FileID file) const
	{
		auto itr = m_outFileAncestor.find(file);
		return itr != m_outFileAncestor.end() ? itr->second : file;
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

	void ParsingFile::GenerateKidBySame()
	{
		m_userKids = m_kids;

		for (const auto &itr : m_userKids)
		{
			FileID top = itr.first;

			FileSet &kids = m_kidsBySame[GetLowerFileNameInCache(top)];
			GetKidsBySame(top, kids);
		}
	}

	void ParsingFile::GenerateUserUse()
	{
		for (const auto &itr : m_uses)
		{
			FileID by				= itr.first;
			const FileSet &useList	= itr.second;

			if (IsForceIncluded(by))
			{
				continue;
			}

			bool isByOuter = IsOuterFile(by);

			FileID byAncestor = GetOuterFileAncestor(by);

			FileSet userUseList;
			for (FileID beUse : useList)
			{
				if (isByOuter)
				{
					if (IsAncestorBySame(beUse, by))
					{
						continue;
					}
				}

				FileID a = GetBestKidBySame(by, beUse);
				FileID b = GetBestKid(by, beUse);

				if (a != b)
				{
					LogInfo("a != b: by = " << GetDebugFileName(by));
					LogInfo("------: beuse = " << GetDebugFileName(beUse));
					LogInfo("------: a = " << GetDebugFileName(a));
					LogInfo("------: b = " << GetDebugFileName(b));
				}

				FileID beUseAncestor = GetOuterFileAncestor(a);
				userUseList.insert(beUseAncestor);
			}

			userUseList.erase(byAncestor);
			if (!userUseList.empty())
			{
				Add(m_userUses[byAncestor], userUseList);
			}
		}
	}

	void ParsingFile::GenerateMinUse()
	{
		for (const auto &itr : m_userUses)
		{
			FileID top = itr.first;
			const FileSet chain = GetUseChain(top);
			if (!chain.empty())
			{
				m_min.insert(std::make_pair(top, chain));
			}
		}

		m_minKids = m_min;

		// 2. 合并
		while (MergeMin()) {}
	}

	void ParsingFile::GetKidsBySame(FileID top, FileSet &kids) const
	{
		kids = GetChain(top, [&](const FileSet &done, FileSet &todo, FileID cur)
		{
			// 内部函数：将单个文件加入todo集合
			auto AddTodo = [&] (FileID file)
			{
				if (!Has(done, file))
				{
					todo.insert(file);
				}

				auto kidItr = m_userKids.find(file);
				if (kidItr != m_userKids.end())
				{
					const FileSet &kids = kidItr->second;
					AddIf(todo, kids, [&done](FileID kid)
					{
						return !Has(done, kid);
					});
				}
			};

			const FileSet sames = GetAllSameFiles(cur);
			for (FileID same : sames)
			{
				AddTodo(same);
			}
		});

		const FileSet sames = GetAllSameFiles(top);
		Del(kids, sames);
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
				m_relyKids[parent].insert(usedFile);
			}
		}
	}

	// 获取该文件可被替换到的文件，若无法被替换，则返回空文件id
	FileID ParsingFile::GetCanReplaceTo(FileID top) const
	{
		// 仅[替换#include]清理选项开启时，才尝试替换
		if (!Project::IsCleanModeOpen(CleanMode_Replace))
		{
			return FileID();
		}

		// 若文件本身也被使用到，则无法被替换
		if (IsRely(top))
		{
			return FileID();
		}

		auto &itr = m_relyKids.find(top);
		if (itr == m_relyKids.end())
		{
			return FileID();
		}

		const FileSet &children = itr->second;		// 有用的后代文件

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
		for (auto &itr = m_relys.rbegin(); itr != m_relys.rend(); ++itr)
		{
			FileID file = *itr;

			for (FileID top = m_root, lv2Top; top != file; top = lv2Top)
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

				FileID canReplaceTo = GetCanReplaceTo(lv2Top);

				auto &replaceItr = m_replaces.find(lv2Top);
				if (replaceItr != m_replaces.end())
				{
					FileID oldReplaceTo = replaceItr->second;

					if (canReplaceTo != oldReplaceTo)
					{
						m_replaces.erase(lv2Top);
						return true;
					}
				}
				else
				{
					bool expand = true;

					// 若不可被替换
					if (canReplaceTo.isInvalid())
					{
						expand = ExpandRely(lv2Top);
					}
					// 若可被替换
					else if (!ExpandRely(canReplaceTo))
					{
						m_replaces[lv2Top] = canReplaceTo;
					}

					if (expand)
					{
						return true;
					}
				}
			}
		}

		return false;
	}

	// 根据主文件的依赖关系，生成相关文件的依赖文件集
	void ParsingFile::GenerateRely()
	{
		// 1. 获取主文件的依赖文件集
		GetTopRelys(m_root, m_topRelys);

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
				m_usedFiles.insert(child);
			}
		}
	}

	bool ParsingFile::ExpandRely(FileID top)
	{
		FileSet topRelys;
		GetTopRelys(top, topRelys);

		int oldSize = m_relys.size();
		Add(m_relys, topRelys);
		int newSize = m_relys.size();

		return newSize > oldSize;
	}

	// 是否禁止改动某文件
	bool ParsingFile::IsSkip(FileID file) const
	{
		return IsForceIncluded(file) || IsPrecompileHeader(file);
	}

	// 该文件是否被依赖
	inline bool ParsingFile::IsRely(FileID file) const
	{
		return Has(m_relys, file);
	}

	// 该文件的所有同名文件是否被依赖（同一文件可被包含多次）
	inline bool ParsingFile::HasMinKidBySameName(FileID top, FileID kid) const
	{
		const FileSet &sames = GetAllSameFiles(kid);
		for (FileID same : sames)
		{
			if (HasInMap(m_minKids, top, same))
			{
				return true;
			}
		}

		return false;
	}

	// a文件是否在b位置之前
	bool ParsingFile::IsFileBeforeLoc(FileID a, SourceLocation b) const
	{
		SourceLocation aBeg = m_srcMgr->getLocForStartOfFile(a);
		return m_srcMgr->isBeforeInTranslationUnit(aBeg, b);
	}

	// 祖先文件是否被强制包含
	inline bool ParsingFile::IsAncestorForceInclude(FileID file) const
	{
		return GetAncestorForceInclude(file).isValid();
	}

	// 获取被强制包含祖先文件
	inline FileID ParsingFile::GetAncestorForceInclude(FileID file) const
	{
		for (FileID forceInclude : m_forceIncludes)
		{
			if (file == forceInclude || IsAncestor(file, forceInclude))
			{
				return forceInclude;
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

		// 生成无用文件列表
		for (FileID file : m_files)
		{
			FileID parent = GetParent(file);
			if (!IsRely(parent))
			{
				continue;
			}

			// 排除被用到的文件
			if (Has(m_usedFiles, file))
			{
				continue;
			}

			// 有些#include不应被删除或无法被删除，如强制包含、预编译头文件
			if (IsSkip(file))
			{
				continue;
			}

			m_unusedFiles.insert(file);
		}
	}

	void ParsingFile::GetTopRelys(FileID top, FileSet &out) const
	{
		out = GetChain(top, [&](const FileSet &done, FileSet &todo, FileID cur)
		{
			// 1. 若当前文件不再依赖其他文件，则可以不再扩展当前文件
			auto &useItr = m_uses.find(cur);
			if (useItr != m_uses.end())
			{
				// 2. 将当前文件依赖的其他文件加入待处理集合中
				const FileSet &useFiles = useItr->second;
				AddIf(todo, useFiles, [&](FileID beuse)
				{
					return done.find(beuse) == done.end();
				});
			}
		});
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
	FileID ParsingFile::GetCommonAncestor(const FileSet &kids) const
	{
		FileID highestKid;
		int minDepth = 0;

		for (FileID kid : kids)
		{
			int depth = GetDepth(kid);

			if (minDepth == 0 || depth < minDepth)
			{
				highestKid = kid;
				minDepth = depth;
			}
		}

		FileID ancestor = highestKid;
		while (!IsCommonAncestor(kids, ancestor))
		{
			FileID parent = GetParent(ancestor);
			if (parent.isInvalid())
			{
				return m_root;
			}

			ancestor = parent;
		}

		return ancestor;
	}

	// 生成文件替换列表
	void ParsingFile::GenerateReplace()
	{
		for (auto &itr : m_replaces)
		{
			FileID from		= itr.first;
			FileID parent	= GetParent(from);

			m_splitReplaces[parent].insert(itr);
		}

		m_splitReplaces.erase(FileID());
	}

	// 是否应保留该位置引用的class、struct、union的前置声明
	bool ParsingFile::IsNeedMinClass(FileID by, const CXXRecordDecl &cxxRecord) const
	{
		auto IsAnyKidHasRecord = [&](FileID byFile, FileID recordAt) -> bool
		{
			if (IsAncestorForceInclude(recordAt))
			{
				LogInfoByLvl(LogLvl_3, "record is force included: skip record = [" <<  GetRecordName(cxxRecord) << "], by = " << GetDebugFileName(byFile) << ", record file = " << GetDebugFileName(recordAt));
				return true;
			}

			if (IsSameName(byFile, recordAt))
			{
				LogInfoByLvl(LogLvl_3, "record is at same file: skip record = [" <<  GetRecordName(cxxRecord) << "], by = " << GetDebugFileName(byFile) << ", record file = " << GetDebugFileName(recordAt));
				return true;
			}

			FileID outerAncestor = GetOuterFileAncestor(recordAt);
			return HasMinKidBySameName(byFile, outerAncestor);
		};

		auto IsAnyRecordBeInclude = [&]() -> bool
		{
			// 如果本文件内该位置之前已有前置声明则不再处理
			const TagDecl *first = cxxRecord.getFirstDecl();
			for (const TagDecl *next : first->redecls())
			{
				FileID recordAtFile = GetFileID(next->getLocation());
				if (IsAnyKidHasRecord(by, recordAtFile))
				{
					return true;
				}

				LogErrorByLvl(LogLvl_3, "[IsAnyKidHasRecord = false]: by = " << GetDebugFileName(by) <<  ", file = " << GetDebugFileName(recordAtFile) << ", record = " << next->getNameAsString());
			}

			return false;
		};

		if (IsAnyRecordBeInclude())
		{
			return false;
		}

		return true;
	}

	// 生成新增前置声明列表
	void ParsingFile::GenerateForwardClass()
	{
		// 1. 清除一些不必要保留的前置声明
		for (auto &itr : m_fileUseRecordPointers)
		{
			FileID by			= itr.first;
			RecordSet &records	= itr.second;

			auto &beUseItr = m_fileUseRecords.find(by);
			if (beUseItr != m_fileUseRecords.end())
			{
				const RecordSet &beUseRecords = beUseItr->second;
				Del(records, beUseRecords);
			}
		}

		m_fowardClass = m_fileUseRecordPointers;

		MapEraseIf(m_fowardClass, [&](FileID by, RecordSet &records)
		{
			EraseIf(records, [&](const CXXRecordDecl* record)
			{
				bool need = g_nowFile->IsNeedMinClass(by, *record);
				if (need)
				{
					LogErrorByLvl(LogLvl_3, "IsNeedMinClass = true: " << GetDebugFileName(by) << "," << GetRecordName(*record));
				}

				return !need;
			});

			return records.empty();
		});

		// 2.
		MinimizeForwardClass();
	}

	// 裁剪前置声明列表
	void ParsingFile::MinimizeForwardClass()
	{
		FileSet all;
		Add(all, m_fileUseRecordPointers);
		Add(all, m_min);

		FileUseRecordsMap bigRecordMap;

		for (FileID by : all)
		{
			GetUseRecordsInKids(by, m_fowardClass, bigRecordMap[by]);
		}

		for (auto &itr : bigRecordMap)
		{
			FileID by = itr.first;
			RecordSet small = itr.second;	// 这里故意深拷贝

			auto &useItr = m_min.find(by);
			if (useItr != m_min.end())
			{
				const FileSet &useList = useItr->second;

				for (FileID beUse : useList)
				{
					auto &recordItr = bigRecordMap.find(beUse);
					if (recordItr != bigRecordMap.end())
					{
						const RecordSet &records = recordItr->second;
						Del(small, records);
					}
				}
			}

			if (!small.empty())
			{
				m_fowardClass[by] = small;
			}
		}
	}

	void ParsingFile::GetUseRecordsInKids(FileID top, const FileUseRecordsMap &recordMap, RecordSet &records)
	{
		const FileSet done = GetChain(top, [&](const FileSet &done, FileSet &todo, FileID cur)
		{
			// 1. 若当前文件不依赖其他文件，则跳过
			auto &useItr = m_min.find(cur);
			if (useItr != m_min.end())
			{
				// 2. todo集合 += 当前文件依赖的其他文件
				const FileSet &useFiles = useItr->second;
				AddIf(todo, useFiles, [&done](FileID beuse)
				{
					return done.find(beuse) == done.end();
				});
			}
		});

		for (FileID file : done)
		{
			AddByKey(records, recordMap, file);
		}
	}

	// 获取指定范围的文本
	std::string ParsingFile::GetSourceOfRange(SourceRange range) const
	{
		if (range.isInvalid())
		{
			return "";
		}

		range = m_srcMgr->getExpansionRange(range);

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

		if (nullptr == beg || nullptr == end || end < beg)
		{
			// 注意：这里如果不判断end < beg遇到宏可能会崩，因为有可能末尾反而在起始字符前面，比如在宏之内
			return "";
		}

		return string(beg, end);
	}

	// 获取指定位置的文本
	const char* ParsingFile::GetSourceAtLoc(SourceLocation loc) const
	{
		bool err = true;

		const char* str = m_srcMgr->getCharacterData(loc, &err);
		return err ? nullptr : str;
	}

	// 获取指定位置所在行的文本
	std::string ParsingFile::GetSourceOfLine(SourceLocation loc) const
	{
		return GetSourceOfRange(GetCurLine(loc));
	}

	SourceRange ParsingFile::GetCurLine(SourceLocation loc) const
	{
		if (loc.isInvalid())
		{
			return SourceRange();
		}

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
		SourceLocation includeLoc = m_srcMgr->getIncludeLoc(file);
		return GetCurFullLine(includeLoc);
	}

	// 是否是换行符
	bool ParsingFile::IsNewLineWord(SourceLocation loc) const
	{
		string text = GetSourceOfRange(SourceRange(loc, loc.getLocWithOffset(1)));
		return text == "\r" || text == "";
	}

	// 获取文件对应的#include所在的行（不含换行符）
	std::string ParsingFile::GetBeIncludeLineText(FileID file) const
	{
		SourceLocation loc = m_srcMgr->getIncludeLoc(file);
		return GetSourceOfLine(loc);
	}

	// a位置的代码使用b位置的代码
	inline void ParsingFile::Use(SourceLocation a, SourceLocation b, const char* name /* = nullptr */)
	{
		a = GetExpasionLoc(a);
		b = GetExpasionLoc(b);

		UseInclude(GetFileID(a), GetFileID(b), name, GetLineNo(a));
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
	inline FileID ParsingFile::GetLvl2AncestorBySame(FileID kid, FileID top) const
	{
		auto itr = m_includes.find(top);
		if (itr == m_includes.end())
		{
			return FileID();
		}

		const std::string kidFileName = GetLowerFileNameInCache(kid);

		const FileSet &includes = itr->second;
		for (FileID beInclude : includes)
		{
			if (kidFileName == GetLowerFileNameInCache(beInclude))
			{
				return beInclude;
			}

			const FileSet &sames = GetAllSameFiles(beInclude);
			for (FileID same : sames)
			{
				if (IsAncestorBySame(kid, same))
				{
					return beInclude;
				}
			}
		}

		return FileID();
	}

	// 第2个文件是否是第1个文件的祖先
	inline bool ParsingFile::IsAncestor(FileID young, FileID old) const
	{
		// 搜索后代文件
		auto &itr = m_kids.find(old);
		if (itr != m_kids.end())
		{
			const FileSet &children = itr->second;
			return children.find(young) != children.end();
		}

		return false;
	}

	// 第2个文件是否是第1个文件的祖先
	inline bool ParsingFile::IsAncestor(FileID young, const char* old) const
	{
		const std::string strOld = tolower(old);

		for (FileID parent = GetParent(young); parent.isValid(); parent = GetParent(parent))
		{
			if (GetLowerFileNameInCache(parent) == strOld)
			{
				return true;
			}
		}

		return false;
	}

	// 第2个文件是否是第1个文件的祖先
	inline bool ParsingFile::IsAncestor(const char* young, FileID old) const
	{
		// 搜索后代文件
		auto &itr = m_kids.find(old);
		if (itr != m_kids.end())
		{
			const FileSet &kids = itr->second;

			const std::string strKid = tolower(young);
			for (FileID kid : kids)
			{
				if (strKid == GetLowerFileNameInCache(kid))
				{
					return true;
				}
			}
		}

		return false;
	}

	// 第2个文件是否是第1个文件的祖先（考虑同名文件）
	inline bool ParsingFile::IsAncestorBySame(FileID young, FileID old) const
	{
		auto itr = m_kidsBySame.find(GetLowerFileNameInCache(old));
		if (itr != m_kidsBySame.end())
		{
			const FileSet &kids = itr->second;
			return Has(kids, young);
		}

		return false;
	}

	// 是否为孩子文件的共同祖先
	bool ParsingFile::IsCommonAncestor(const FileSet &children, FileID old) const
	{
		for (FileID child : children)
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
		auto &itr = m_parents.find(child);
		if (itr != m_parents.end())
		{
			if (child == m_root)
			{
				return FileID();
			}

			return itr->second;
		}

		return FileID();
	}

	// a文件使用b文件
	inline void ParsingFile::UseInclude(FileID a, FileID b, const char* name /* = nullptr */, int line)
	{
		if (a == b || a.isInvalid() || b.isInvalid())
		{
			return;
		}

		if (nullptr == m_srcMgr->getFileEntryForID(a) || nullptr == m_srcMgr->getFileEntryForID(b))
		{
			LogErrorByLvl(LogLvl_5, "m_srcMgr->getFileEntryForID(a) failed!" << m_srcMgr->getFilename(m_srcMgr->getLocForStartOfFile(a)) << ":" << m_srcMgr->getFilename(m_srcMgr->getLocForStartOfFile(b)));
			LogErrorByLvl(LogLvl_5, "m_srcMgr->getFileEntryForID(b) failed!" << GetSourceOfLine(m_srcMgr->getLocForStartOfFile(a)) << ":" << GetSourceOfLine(m_srcMgr->getLocForStartOfFile(b)));

			return;
		}

		if (IsSameName(a, b))
		{
			return;
		}

		b = GetBestKid(a, b);

		m_uses[a].insert(b);
		UseName(a, b, name, line);
	}

	// 当前位置使用指定的宏
	void ParsingFile::UseMacro(SourceLocation loc, const MacroDefinition &macro, const Token &macroNameTok, const MacroArgs *args /* = nullptr */)
	{
		MacroInfo *info = macro.getMacroInfo();
		if (info)
		{
			string macroName = macroNameTok.getIdentifierInfo()->getName().str() + "[macro]";
			Use(loc, info->getDefinitionLoc(), macroName.c_str());
		}
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
		if (Project::instance.m_logLvl >= LogLvl_3)
		{
			SourceLocation loc = GetSpellingLoc(d->getLocation());

			FileID file = GetFileID(loc);
			if (file.isInvalid())
			{
				return;
			}

			m_namespaces[file].insert(d->getQualifiedNameAsString());
		}
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
		if (usingLoc.isInvalid())
		{
			return;
		}

		m_usingNamespaces[usingLoc] = firstNs;

		for (const NamespaceDecl *ns : firstNs->redecls())
		{
			SourceLocation nsLoc	= GetSpellingLoc(ns->getLocStart());
			if (nsLoc.isInvalid())
			{
				continue;
			}

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

	// 当a使用b时，如果b对应的文件被包含多次，从b的同名文件中选取一个最好的文件
	inline FileID ParsingFile::GetBestKid(FileID a, FileID b) const
	{
		// 如果b文件被包含多次，则在其中选择一个较合适的（注意：每个文件优先保留自己及后代文件中的#include语句）
		auto &sameItr = m_sameFiles.find(GetLowerFileNameInCache(b));
		if (sameItr != m_sameFiles.end())
		{
			// 先找到同名文件列表
			const FileSet &sames = sameItr->second;

			// 1. 优先返回直接直接包含的文件
			auto &includeItr = m_includes.find(a);
			if (includeItr != m_includes.end())
			{
				const FileSet &includes = includeItr->second;

				for (FileID same :sames)
				{
					if (Has(includes, same))
					{
						return same;
					}
				}
			}

			// 2. 否则，搜索后代文件
			auto &kidItr = m_kids.find(a);
			if (kidItr != m_kids.end())
			{
				const FileSet &children = kidItr->second;
				for (FileID same :sames)
				{
					if (Has(children, same))
					{
						return same;
					}
				}
			}
		}

		return b;
	}

	inline FileID ParsingFile::GetBestKidBySame(FileID a, FileID b) const
	{
		if (!IsAncestorBySame(b, a))
		{
			return b;
		}

		FileSet todo;
		FileSet done;

		todo.insert(b);

		while (!todo.empty())
		{
			todo.erase(FileID());
			Del(todo, done);

			FileSet doing = todo;
			todo.clear();

			for (FileID f : doing)
			{
				const FileSet sames = GetAllSameFiles(f);
				for (FileID same : sames)
				{
					if (IsAncestor(same, a))
					{
						return same;
					}

					todo.insert(GetParent(same));
				}
			}

			Add(done, doing);
		}

		return b;
	}

	// 当前位置使用目标类型（注：Type代表某个类型，但不含const、volatile、static等的修饰）
	void ParsingFile::UseType(SourceLocation loc, const Type *t)
	{
		if (nullptr == t || loc.isInvalid())
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

	// 新增使用前置声明记录（对于不必要添加的前置声明将在之后进行清理）
	inline void ParsingFile::UseForward(SourceLocation loc, const CXXRecordDecl *cxxRecord)
	{
		if (nullptr == cxxRecord)
		{
			return;
		}

		FileID by = GetFileID(loc);
		if (by.isInvalid())
		{
			return;
		}

		if (Project::IsCleanModeOpen(CleanMode_Need))
		{
			// 添加文件所使用的前置声明记录
			m_fileUseRecordPointers[by].insert(cxxRecord);

			if (Project::instance.m_logLvl >= LogLvl_3)
			{
				m_locUseRecordPointers[loc].insert(cxxRecord);
			}
		}
		else
		{
			// 优先引用该位置之前的前置声明则，尽量避免引用完整的class定义
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

		if (IsOuterFile(GetFileID(loc)))
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

		FileID by = GetFileID(loc);
		if (by.isInvalid())
		{
			return;
		}

		if (isa<ClassTemplateSpecializationDecl>(record))
		{
			const ClassTemplateSpecializationDecl *d = cast<ClassTemplateSpecializationDecl>(record);
			UseTemplateArgumentList(loc, &d->getTemplateArgs());
		}

		if (Project::IsCleanModeOpen(CleanMode_Need))
		{
			if (isa<CXXRecordDecl>(record))
			{
				const CXXRecordDecl *cxxRecord = cast<CXXRecordDecl>(record);
				m_fileUseRecords[by].insert(cxxRecord);
			}
		}

		Use(loc, record->getLocStart(), GetRecordName(*record).c_str());
		UseContext(loc, record->getDeclContext());
	}

	// 是否允许清理该c++文件（若不允许清理，则文件内容不会有任何变化）
	inline bool ParsingFile::CanClean(FileID file) const
	{
		return Project::CanClean(GetLowerFileNameInCache(file));
	}

	// 打印各文件的父文件
	void ParsingFile::PrintParent()
	{
		HtmlDiv &div = HtmlLog::instance.m_newDiv;
		div.AddRow(AddPrintIdx() + ". list of parent id: has parent file count = " + htmltool::get_number_html(m_parents.size()), 1);

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
	void ParsingFile::PrintKids()
	{
		HtmlDiv &div = HtmlLog::instance.m_newDiv;
		div.AddRow(AddPrintIdx() + ". list of kids : file count = " + strtool::itoa(m_kids.size()), 1);

		for (auto &itr : m_kids)
		{
			FileID parent = itr.first;
			const FileSet &kids = itr.second;

			div.AddRow(DebugParentFileText(parent, kids.size()), 2);

			for (FileID kid : kids)
			{
				div.AddRow("kid = " + DebugBeIncludeText(kid), 3);
			}

			div.AddRow("");
		}
	}

	// 打印各文件的孩子文件
	void ParsingFile::PrintUserKids()
	{
		HtmlDiv &div = HtmlLog::instance.m_newDiv;
		div.AddRow(AddPrintIdx() + ". list of user kids : file count = " + strtool::itoa(m_userKids.size()), 1);

		for (auto &itr : m_userKids)
		{
			FileID parent = itr.first;
			const FileSet &kids = itr.second;

			div.AddRow(DebugParentFileText(parent, kids.size()), 2);

			for (FileID child : kids)
			{
				div.AddRow("user kid = " + DebugBeIncludeText(child), 3);
			}

			div.AddRow("");
		}
	}

	void ParsingFile::PrintKidsBySame()
	{
		HtmlDiv &div = HtmlLog::instance.m_newDiv;
		div.AddRow(AddPrintIdx() + ". list of kids by same name : file count = " + strtool::itoa(m_kidsBySame.size()), 1);

		for (auto &itr : m_kidsBySame)
		{
			const std::string &parent = itr.first;
			const FileSet &kids = itr.second;

			div.AddRow("file = " + htmltool::get_file_html(parent.c_str()) + ", kid num = " + htmltool::get_number_html(kids.size()), 2);

			for (FileID kid : kids)
			{
				div.AddRow("kid by same = " + DebugBeIncludeText(kid), 3);
			}

			div.AddRow("");
		}
	}

	// 获取该文件的被包含信息，返回内容包括：该文件名、父文件名、被父文件#include的行号、被父文件#include的原始文本串
	string ParsingFile::DebugBeIncludeText(FileID file) const
	{
		const char *fileName = GetFileNameInCache(file);
		if (file == m_root)
		{
			return strtool::get_text(cn_main_file_debug_text, htmltool::get_file_html(fileName).c_str(),
			                         file.getHashValue(), htmltool::get_number_html(GetDeepth(file)).c_str());
		}
		else
		{
			stringstream ancestors;

			for (FileID parent = GetParent(file), child = file; parent.isValid();)
			{
				string includeLineText = strtool::get_text(cn_file_include_line, htmltool::get_min_file_name_html(GetFileNameInCache(parent)).c_str(),
				                         strtool::itoa(GetIncludeLineNo(child)).c_str(), htmltool::get_include_html(GetBeIncludeLineText(child)).c_str());

				ancestors << includeLineText;
				child = parent;

				parent = GetParent(parent);
				if (parent.isValid())
				{
					ancestors << "<-";
				}
			}

			return strtool::get_text(cn_file_debug_text, IsOuterFile(file) ? cn_outer_file_flag : "", htmltool::get_file_html(fileName).c_str(),
			                         file.getHashValue(), htmltool::get_number_html(GetDeepth(file)).c_str(), ancestors.str().c_str());
		}

		return "";
	}

	// 获取文件信息
	std::string ParsingFile::DebugParentFileText(FileID file, int n) const
	{
		return strtool::get_text(cn_parent_file_debug_text, DebugBeIncludeText(file).c_str(), htmltool::get_number_html(n).c_str());
	}

	// 获取该位置所在行的信息：所在行的文本、所在文件名、行号
	string ParsingFile::DebugLocText(SourceLocation loc) const
	{
		string lineText = GetSourceOfLine(loc);
		std::stringstream text;
		text << "[" << htmltool::get_include_html(lineText) << "] in [";
		text << htmltool::get_file_html(GetFileNameInCache(GetFileID(loc)));
		text << "] line = " << htmltool::get_number_html(GetLineNo(loc)) << " col = " << htmltool::get_number_html(m_srcMgr->getSpellingColumnNumber(loc));
		return text.str();
	}

	// 获取文件名（通过clang库接口，文件名未经过处理，可能是绝对路径，也可能是相对路径）
	// 例如：
	//     可能返回绝对路径：d:/a/b/hello.h
	//     也可能返回：./../hello.h
	inline const char* ParsingFile::GetFileName(FileID file) const
	{
		const FileEntry *fileEntry = m_srcMgr->getFileEntryForID(file);
		return fileEntry ? fileEntry->getName() : "";
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
	inline string ParsingFile::GetAbsoluteFileName(FileID file) const
	{
		const char* raw_file_name = GetFileName(file);
		return pathtool::get_absolute_path(raw_file_name);
	}

	// 获取文件的绝对路径（从缓存中获取，速度比每次重新计算快30倍）
	inline const char* ParsingFile::GetFileNameInCache(FileID file) const
	{
		auto itr = m_fileNames.find(file);
		return itr != m_fileNames.end() ? itr->second.c_str() : "";
	}

	// 获取文件的小写绝对路径
	inline const char* ParsingFile::GetLowerFileNameInCache(FileID file) const
	{
		auto itr = m_lowerFileNames.find(file);
		return itr != m_lowerFileNames.end() ? itr->second.c_str() : "";
	}

	// 用于调试：获取文件的绝对路径和相关信息
	string ParsingFile::GetDebugFileName(FileID file) const
	{
		stringstream name;
		stringstream ancestors;

		const char *fileNameInCache = GetFileNameInCache(file);
		string fileName = (IsOuterFile(file) ? fileNameInCache : pathtool::get_file_name(fileNameInCache));

		name << "[" << fileName << "]";

		for (FileID parent = GetParent(file), child = file; parent.isValid(); )
		{
			ancestors << pathtool::get_file_name(GetFileNameInCache(parent));
			ancestors << "=" << GetIncludeLineNo(child);

			child = parent;
			parent = GetParent(child);
			if (parent.isValid())
			{
				ancestors << ",";
			}
		}

		if (!ancestors.str().empty())
		{
			name << "(" << ancestors.str() << ")";
		}

		return name.str();
	}

	// 获取第1个文件#include第2个文件的文本串
	std::string ParsingFile::GetRelativeIncludeStr(FileID f1, FileID f2) const
	{
		// 若第2个文件的被包含串原本就是#include <xxx>的格式，则返回原本的#include串
		{
			string rawInclude2 = GetBeIncludeLineText(f2);
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

		string absolutePath1 = GetFileNameInCache(f1);
		string absolutePath2 = GetFileNameInCache(f2);

		string include2;

		// 优先判断2个文件是否在同一文件夹下
		if (tolower(get_dir(absolutePath1)) == tolower(get_dir(absolutePath2)))
		{
			include2 = "\"" + strip_dir(absolutePath2) + "\"";
		}
		else
		{
			// 在头文件搜索路径中搜寻第2个文件，若成功找到，则返回基于头文件搜索路径的相对路径
			include2 = GetQuotedIncludeStr(absolutePath2.c_str());

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

		if (Project::instance.m_canCleanAll)
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
	// （本接口参考了Rewriter::overwriteChangedFiles）
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
			static bool WriteToFile(const RewriteBuffer &rewriteBuffer, const char *fileName)
			{
				if (!EnableWrite(fileName))
				{
					LogError("overwrite file [" << fileName << "] failed: has no permission");
					return false;
				}

				std::ofstream fout;
				fout.open(fileName, ios_base::out | ios_base::binary);

				if (!fout.is_open())
				{
					LogError("overwrite file [" << fileName << "] failed: can not open file, error code = " << errno <<" "<<strerror(errno));
					return false;
				}

				std::stringstream ss;
				for (RopePieceBTreeIterator itr = rewriteBuffer.begin(), end = rewriteBuffer.end(); itr != end; itr.MoveToNextPiece())
				{
					ss << itr.piece().str();
				}

				fout << ss.str();
				fout.close();
				return true;
			}
		};

		bool isAllOk = true;
		for (Rewriter::buffer_iterator itr = m_rewriter.buffer_begin(), end = m_rewriter.buffer_end(); itr != end; ++itr)
		{
			FileID fileID = itr->first;

			const RewriteBuffer &rewriteBuffer	= itr->second;

			LogInfoByLvl(LogLvl_2, "overwriting " << GetDebugFileName(fileID) << " ...");

			bool ok = CxxCleanReWriter::WriteToFile(rewriteBuffer, GetFileNameInCache(fileID));
			if (!ok)
			{
				LogError("overwrite file" << GetDebugFileName(fileID) << " failed!");
				isAllOk = false;
			}
			else
			{
				LogInfoByLvl(LogLvl_2, "overwriting " << GetDebugFileName(fileID) << " success!");
			}
		}

		return !isAllOk;
	}

	// 替换指定范围文本
	void ParsingFile::ReplaceText(FileID file, int beg, int end, const char* text)
	{
		if (strtool::is_empty(text))
		{
			return;
		}

		SourceLocation fileBegLoc	= m_srcMgr->getLocForStartOfFile(file);
		SourceLocation begLoc		= fileBegLoc.getLocWithOffset(beg);
		SourceLocation endLoc		= fileBegLoc.getLocWithOffset(end);

		if (nullptr == GetSourceAtLoc(begLoc) || nullptr == GetSourceAtLoc(endLoc))
		{
			LogError("nullptr == GetSourceAtLoc(begLoc) || nullptr == GetSourceAtLoc(endLoc)");
			return;
		}

		LogInfoByLvl(LogLvl_2, "replace [" << GetFileNameInCache(file) << "]: [" << beg << "," << end << "] to text = [" << text << "]");

		bool err = m_rewriter.ReplaceText(begLoc, end - beg, text);
		if (err)
		{
			LogError("replace [" << GetDebugFileName(file) << "]: [" << beg << "," << end << "] to text = [" << text << "] failed");
		}
	}

	// 将文本插入到指定位置之前
	// 例如：假设有"abcdefg"文本，则在c处插入123的结果将是："ab123cdefg"
	void ParsingFile::InsertText(FileID file, int loc, const char* text)
	{
		if (strtool::is_empty(text))
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

		LogInfoByLvl(LogLvl_2, "insert [" << GetFileNameInCache(file) << "]: [" << loc << "] to text = [" << text << "]");

		bool err = m_rewriter.InsertText(insertLoc, text, false, false);
		if (err)
		{
			LogError("insert [" << GetDebugFileName(file) << "]: [" << loc << "] to text = [" << text << "] failed");
		}
	}

	// 移除指定范围文本，若移除文本后该行变为空行，则将该空行一并移除
	void ParsingFile::RemoveText(FileID file, int beg, int end)
	{
		SourceLocation fileBegLoc	= m_srcMgr->getLocForStartOfFile(file);
		if (fileBegLoc.isInvalid())
		{
			LogError("if (fileBegLoc.isInvalid()), remove text in [" << GetDebugFileName(file) << "] failed!");
			return;
		}

		SourceLocation begLoc	= fileBegLoc.getLocWithOffset(beg);
		SourceLocation endLoc	= fileBegLoc.getLocWithOffset(end);

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

		LogInfoByLvl(LogLvl_2, "remove [" << GetFileNameInCache(file) << "]: [" << beg << "," << end << "], text = [" << GetSourceOfRange(range) << "]");

		bool err = m_rewriter.RemoveText(range.getBegin(), end - beg, rewriteOption);
		if (err)
		{
			LogError("remove [" << GetDebugFileName(file) << "]: [" << beg << "," << end << "], text = [" << GetSourceOfRange(range) << "] failed");
		}
	}

	// 移除指定文件内的无用行
	void ParsingFile::CleanByDelLine(const FileHistory &history, FileID file)
	{
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

				InsertText(file, forwardLine.offset, text.str().c_str());
			}
		}
	}

	// 在指定文件内替换#include
	void ParsingFile::CleanByReplace(const FileHistory &history, FileID file)
	{
		const char *newLineWord = history.GetNewLineWord();

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

			ReplaceText(file, replaceLine.beg, replaceLine.end, text.str().c_str());
		}
	}

	// 在指定文件内新增行
	void ParsingFile::CleanByAdd(const FileHistory &history, FileID file)
	{
		for (auto &addItr : history.m_adds)
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

				InsertText(file, addLine.offset, text.str().c_str());
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
			const char *name = GetLowerFileNameInCache(file);

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
		// 仅取出主文件的待清理记录
		auto &rootItr = ProjectHistory::instance.m_files.find(GetLowerFileNameInCache(m_root));
		if (rootItr != ProjectHistory::instance.m_files.end())
		{
			FileHistoryMap historys;
			historys.insert(*rootItr);

			CleanByHistory(historys);
		}
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
			div.AddRow("search path = " + htmltool::get_file_html(path.m_dir.c_str()), 2);
		}

		div.AddRow("");
	}

	// 用于调试：打印各文件引用的文件集相对于该文件的#include文本
	void ParsingFile::PrintRelativeInclude() const
	{
		HtmlDiv &div = HtmlLog::instance.m_newDiv;
		div.AddRow(AddPrintIdx() + ". relative include list : use = " + htmltool::get_number_html(m_uses.size()), 1);

		for (auto &itr : m_uses)
		{
			FileID file = itr.first;

			if (!CanClean(file))
			{
				continue;
			}

			const FileSet &be_uses = itr.second;

			div.AddRow("file = " + DebugBeIncludeText(file), 2);

			for (FileID be_used_file : be_uses)
			{
				div.AddRow("old include = " + htmltool::get_include_html(GetBeIncludeLineText(be_used_file)), 3, 45);
				div.AddGrid("-> relative include = " + htmltool::get_include_html(GetRelativeIncludeStr(file, be_used_file)));
			}

			div.AddRow("");
		}
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

		div.AddTitle(strtool::get_text(cn_file_history_title,
		                               htmltool::get_number_html(ProjectHistory::instance.g_fileNum).c_str(),
		                               htmltool::get_number_html(Project::instance.m_cpps.size()).c_str(),
		                               htmltool::get_file_html(GetFileNameInCache(m_root)).c_str()));

		m_printIdx = 0;

		if (verbose >= LogLvl_1)
		{
			PrintCleanLog();
		}

		if (verbose >= LogLvl_3)
		{
			if (Project::IsCleanModeOpen(CleanMode_Need))
			{
				PrintMinUse();
				PrintMinKid();
				PrintUserUse();
				PrintForwardClass();
			}
			else
			{
				PrintTopRely();
				PrintRely();
				PrintRelyChildren();
			}

			PrintUse();
			PrintUseName();
			PrintSameFile();
		}

		if (verbose >= LogLvl_4)
		{
			if (Project::IsCleanModeOpen(CleanMode_Need))
			{
				PrintOutFileAncestor();
				PrintUserKids();
				PrintKidsBySame();
				PrintUseRecord();
			}

			PrintAllFile();
			PrintInclude();
			PrintHeaderSearchPath();
			PrintRelativeInclude();
			PrintParent();
			PrintKids();
			PrintNamespace();
			PrintUsingNamespace();
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

			if (is_same_ignore_case(oldFile.m_filename, GetLowerFileNameInCache(top)))
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

				const ReplaceMap &newReplaceMap	= newFileItr->second;

				// 找到该行旧的#include对应的FileID
				FileID beReplaceFileID;

				for (auto & childItr : newReplaceMap)
				{
					FileID child = childItr.first;
					if (GetLowerFileNameInCache(child) == newLine.oldFile)
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
			const char *rootFile = GetLowerFileNameInCache(m_root);

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

			auto &findItr = oldFiles.find(fileName);

			bool found = (findItr != oldFiles.end());
			if (found)
			{
				if (Project::IsCleanModeOpen(CleanMode_Need))
				{
					continue;
				}

				FileHistory &oldFile = findItr->second;

				MergeUnusedLine(newFile, oldFile);
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
		history.m_filename			= GetFileNameInCache(file);
		history.m_isSkip			= IsPrecompileHeader(file);
	}

	// 取出单个文件的可删除#include行
	void ParsingFile::TakeDel(FileHistory &history, const FileSet &dels) const
	{
		for (FileID del : dels)
		{
			int line = GetIncludeLineNo(del);
			if (line <= 0)
			{
				continue;
			}

			SourceRange lineRange	= GetIncludeRange(del);

			DelLine &delLine	= history.m_delLines[line];

			delLine.beg		= m_srcMgr->getFileOffset(lineRange.getBegin());
			delLine.end		= m_srcMgr->getFileOffset(lineRange.getEnd());
			delLine.text	= GetSourceOfLine(lineRange.getBegin());

			if (Project::instance.m_logLvl >= LogLvl_2)
			{
				SourceRange nextLine = GetNextLine(m_srcMgr->getIncludeLoc(del));
				LogInfo("TakeDel [" << history.m_filename << "]: line = " << line << "[" << delLine.beg << "," << m_srcMgr->getFileOffset(nextLine.getBegin())
				        << "," << delLine.end << "," << m_srcMgr->getFileOffset(nextLine.getEnd()) << "], text = [" << delLine.text << "]");
			}
		}
	}

	void ParsingFile::TakeReplaceLine(ReplaceLine &replaceLine, FileID from, FileID to) const
	{
		// 1. 该行的旧文本
		SourceRange	includeRange	= GetIncludeRange(from);
		replaceLine.oldFile			= GetLowerFileNameInCache(from);
		replaceLine.oldText			= GetBeIncludeLineText(from);
		replaceLine.beg				= m_srcMgr->getFileOffset(includeRange.getBegin());
		replaceLine.end				= m_srcMgr->getFileOffset(includeRange.getEnd());
		replaceLine.isSkip			= IsSkip(from);	// 记载是否是强制包含

		// 2. 该行可被替换成什么
		ReplaceTo &replaceTo	= replaceLine.replaceTo;

		// 记录[旧#include、新#include]
		replaceTo.oldText		= GetBeIncludeLineText(to);
		replaceTo.newText		= GetRelativeIncludeStr(GetParent(from), to);

		// 记录[所处的文件、所处行号]
		replaceTo.line			= GetIncludeLineNo(to);
		replaceTo.fileName		= GetFileNameInCache(to);
		replaceTo.inFile		= GetFileNameInCache(GetParent(to));
	}

	void ParsingFile::TakeForwardClass(FileHistory &history, FileID insertAfter, FileID top) const
	{
		auto &useRecordItr = m_fowardClass.find(top);
		if (useRecordItr == m_fowardClass.end())
		{
			return;
		}

		const RecordSet &cxxRecords = useRecordItr->second;
		for (const CXXRecordDecl *cxxRecord : cxxRecords)
		{
			SourceRange includeRange = GetIncludeRange(insertAfter);
			SourceLocation insertLoc = includeRange.getEnd();
			if (insertLoc.isInvalid())
			{
				LogErrorByLvl(LogLvl_2, "insertLoc.isInvalid(), " << GetDebugFileName(top) << ", record = " << GetRecordName(*cxxRecord));
				continue;
			}

			// 开始取出数据
			int line = GetLineNo(insertLoc);

			ForwardLine &forwardLine = history.m_forwards[line];
			forwardLine.offset	= m_srcMgr->getFileOffset(insertLoc);
			forwardLine.oldText	= GetSourceOfLine(includeRange.getBegin());

			forwardLine.classes.insert(GetRecordName(*cxxRecord));
		}
	}

	void ParsingFile::TakeAdd(FileHistory &history, FileID top, FileID insertAfter, const FileSet &adds) const
	{
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
				lv2 = insertAfter;
			}

			int line = GetIncludeLineNo(lv2);

			AddLine &addLine = history.m_adds[line];
			if (addLine.offset <= 0)
			{
				addLine.offset	= m_srcMgr->getFileOffset(GetIncludeRange(lv2).getEnd());
				addLine.oldText	= GetBeIncludeLineText(lv2);
			}

			BeAdd beAdd;
			beAdd.fileName	= GetFileNameInCache(add);
			beAdd.text		= GetRelativeIncludeStr(top, add);

			addLine.adds.push_back(beAdd);
		}
	}

	// 计算出应在哪个文件对应的#include后新增文本
	FileID ParsingFile::CalcInsertLoc(const FileSet &includes, const FileSet &dels, const std::map<FileID, FileID> &replaces) const
	{
		// 找到本文件第一个被修改的#include的位置
		FileID firstInclude;
		int firstIncludeLineNo = 0;

		auto SearchFirstInclude = [&](FileID a)
		{
			int line = GetIncludeLineNo(a);
			if ((firstIncludeLineNo == 0 || line < firstIncludeLineNo) && line > 0)
			{
				firstIncludeLineNo	= line;
				firstInclude		= a;
			}
		};

		for (FileID del : dels)
		{
			SearchFirstInclude(del);
		}

		for (auto &itr : replaces)
		{
			SearchFirstInclude(itr.first);
		}

		if (firstIncludeLineNo <= 0)
		{
			for (FileID a : includes)
			{
				SearchFirstInclude(a);
			}
		}

		return firstInclude;
	}

	// 取出记录，使得各文件仅包含自己所需要的头文件
	void ParsingFile::TakeNeed(FileID top, FileHistory &history) const
	{
		InitHistory(top, history);
		history.m_isSkip = false;

		auto includeItr = m_includes.find(top);
		if (includeItr == m_includes.end())
		{
			return;
		}

		//--------------- 一、先分析文件中哪些#include行需要被删除、替换，及需要新增哪些#include ---------------//

		// 最终应包含的文件列表
		FileSet kids;

		auto itr = m_min.find(top);
		if (itr != m_min.end())
		{
			kids = std::move(itr->second);
		}
		else
		{
			LogInfoByLvl(LogLvl_3, "not in m_min, don't need any include [top] = " << GetDebugFileName(top));
		}

		// 旧的包含文件列表
		FileSet oldIncludes	= includeItr->second;

		// 新的包含文件列表
		FileSet newIncludes	= kids;

		// 应添加
		FileSet adds;

		// 应删除
		FileSet dels;

		// 应替换，<被替换，应替换到>
		std::map<FileID, FileID> replaces;

		// 1. 找到新、旧文件包含列表中[id相同、或文件名相同的文件]，一对对消除
		for (FileID kid : kids)
		{
			bool isSame = false;

			if (oldIncludes.find(kid) != oldIncludes.end())
			{
				oldIncludes.erase(kid);
				newIncludes.erase(kid);
			}
			else
			{
				const std::string kidName = GetLowerFileNameInCache(kid);

				for (FileID beInclude : oldIncludes)
				{
					if (kidName == GetLowerFileNameInCache(beInclude))
					{
						oldIncludes.erase(beInclude);
						newIncludes.erase(kid);
						break;
					}
				}
			}
		}

		// 2. 找到新、旧文件包含列表中[存在祖先后代关系的文件]，一对对记入替换表中
		for (FileID beInclude : oldIncludes)
		{
			for (FileID kid : newIncludes)
			{
				FileID lv2Ancestor = GetLvl2AncestorBySame(kid, beInclude);
				if (lv2Ancestor.isValid())
				{
					replaces[beInclude] = kid;
					newIncludes.erase(kid);

					break;
				}
			}
		}

		Del(oldIncludes, replaces);

		// 3. 最后，新旧文件列表可能还剩余一些文件，处理方法是：直接删掉旧的、添加新的
		dels = std::move(oldIncludes);
		adds = std::move(newIncludes);

		//--------------- 二、开始取出分析结果 ---------------//

		// 1. 取出删除#include记录
		TakeDel(history, dels);

		// 2. 取出替换#include记录
		for (auto &itr : replaces)
		{
			FileID from	= itr.first;
			FileID to	= itr.second;

			int line	= GetIncludeLineNo(from);

			ReplaceLine &replaceLine = history.m_replaces[line];
			TakeReplaceLine(replaceLine, from, to);
		}

		// 3. 取出新增前置声明记录
		FileID insertAfter = CalcInsertLoc(includeItr->second, dels, replaces);

		TakeForwardClass(history, insertAfter, top);

		// 4. 取出新增#include记录
		TakeAdd(history, top, insertAfter, adds);
	}

	// 取出对当前cpp文件的分析结果
	void ParsingFile::TakeHistorys(FileHistoryMap &out) const
	{
		if (Project::IsCleanModeOpen(CleanMode_Need))
		{
			for (FileID top : m_files)
			{
				if (IsOuterFile(top))
				{
					continue;
				}

				const char *fileName = GetLowerFileNameInCache(top);
				if (!Has(out, fileName))
				{
					TakeNeed(top, out[fileName]);
				}
			}

			TakeCompileErrorHistory(out);
		}
		else
		{
			// 先将当前cpp文件使用到的文件全存入map中
			for (FileID file : m_relys)
			{
				const char *fileName = GetLowerFileNameInCache(file);
				if (Project::CanClean(fileName))
				{
					InitHistory(file, out[fileName]);
				}
			}

			// 1. 将可清除的行按文件进行存放
			TakeUnusedLine(out);

			// 2. 将可替换的#include按文件进行存放
			TakeReplace(out);

			// 3. 取出本文件的编译错误历史
			TakeCompileErrorHistory(out);

			// 4. 修复每个文件的历史，防止对同一行修改多次导致崩溃
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
		// 1. 将待删的文件按父文件归类
		std::map<FileID, FileSet> delsByFile;

		for (FileID unusedFile : m_unusedFiles)
		{
			FileID atFile = GetParent(unusedFile);
			delsByFile[atFile].insert(unusedFile);
		}

		// 2. 取出各文件中的可删除行
		for (auto &itr : delsByFile)
		{
			FileID atFile = itr.first;
			const FileSet &dels = itr.second;

			const char *atFileName = GetLowerFileNameInCache(atFile);
			if (!Project::CanClean(atFileName))
			{
				continue;
			}

			auto &historyItr = out.find(atFileName);
			if (historyItr == out.end())
			{
				continue;
			}

			FileHistory &history = historyItr->second;
			TakeDel(history, dels);
		}
	}

	// 该文件是否是被-include强制包含
	inline bool ParsingFile::IsForceIncluded(FileID file) const
	{
		if (file == m_root)
		{
			return false;
		}

		FileID parent = GetFileID(m_srcMgr->getIncludeLoc(file));
		return (m_srcMgr->getFileEntryForID(parent) == nullptr);
	}

	// 该文件是否是预编译头文件
	bool ParsingFile::IsPrecompileHeader(FileID file) const
	{
		const std::string fileName = pathtool::get_file_name(GetLowerFileNameInCache(file));
		return fileName == "stdafx.h";
	}

	// 取出指定文件的#include替换信息
	void ParsingFile::TakeBeReplaceOfFile(FileHistory &history, FileID top, const ReplaceMap &childernReplaces) const
	{
		// 依次取出每行#include的替换信息[行号 -> 被替换成的#include列表]
		for (auto &itr : childernReplaces)
		{
			FileID from	= itr.first;
			FileID to	= itr.second;

			int line					= GetIncludeLineNo(from);
			ReplaceLine &replaceLine	= history.m_replaces[line];
			TakeReplaceLine(replaceLine, from, to);

			// 2. 该行依赖于其他文件的哪些行
			ReplaceTo &replaceTo			= replaceLine.replaceTo;

			const char *replaceParentName	= GetLowerFileNameInCache(GetParent(to));
			int replaceLineNo				= GetIncludeLineNo(to);

			if (Project::CanClean(replaceParentName))
			{
				replaceTo.m_rely[replaceParentName].insert(replaceLineNo);
			}

			auto childItr = m_relyKids.find(to);
			if (childItr != m_relyKids.end())
			{
				const FileSet &relys = childItr->second;

				for (FileID rely : relys)
				{
					const char *relyParentName	= GetLowerFileNameInCache(GetParent(rely));
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
			FileID top							= itr.first;
			const ReplaceMap &childrenReplaces	= itr.second;

			auto outItr = out.find(GetLowerFileNameInCache(top));
			if (outItr == out.end())
			{
				continue;
			}

			// 新增替换记录
			FileHistory &newFile = outItr->second;
			TakeBeReplaceOfFile(newFile, top, childrenReplaces);
		}
	}

	// 取出本文件的编译错误历史
	void ParsingFile::TakeCompileErrorHistory(FileHistoryMap &out) const
	{
		FileHistory &history			= out[GetLowerFileNameInCache(m_root)];
		history.m_compileErrorHistory	= m_compileErrorHistory;
	}

	// 打印引用记录
	void ParsingFile::PrintUse() const
	{
		HtmlDiv &div = HtmlLog::instance.m_newDiv;
		div.AddRow(AddPrintIdx() + ". list of use : use count = " + htmltool::get_number_html(m_uses.size()), 1);

		for (const auto &itr : m_uses)
		{
			FileID file = itr.first;

			if (!IsNeedPrintFile(file))
			{
				continue;
			}

			div.AddRow(DebugParentFileText(file, itr.second.size()), 2);

			for (FileID beuse : itr.second)
			{
				div.AddRow("use = " + DebugBeIncludeText(beuse), 3);
			}

			div.AddRow("");
		}
	}

	// 打印#include记录
	void ParsingFile::PrintInclude() const
	{
		HtmlDiv &div = HtmlLog::instance.m_newDiv;
		div.AddRow(AddPrintIdx() + ". list of include : include count = " + htmltool::get_number_html(m_includes.size()), 1);

		for (auto & includeList : m_includes)
		{
			FileID file = includeList.first;

			if (!CanClean(file))
			{
				continue;
			}

			div.AddRow(DebugParentFileText(file, includeList.second.size()), 2);

			for (FileID beinclude : includeList.second)
			{
				div.AddRow("include = " + DebugBeIncludeText(beinclude), 3);
			}

			div.AddRow("");
		}
	}

	// 打印引用类名、函数名、宏名等的记录
	void ParsingFile::PrintUseName() const
	{
		HtmlDiv &div = HtmlLog::instance.m_newDiv;
		div.AddRow(AddPrintIdx() + ". list of use name : use count = " + htmltool::get_number_html(m_useNames.size()), 1);

		for (auto & useItr : m_useNames)
		{
			FileID file									= useItr.first;
			const std::vector<UseNameInfo> &useNames	= useItr.second;

			if (!IsNeedPrintFile(file))
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
		div.AddRow(DebugParentFileText(file, useNames.size()), 2);

		for (const UseNameInfo &beuse : useNames)
		{
			div.AddRow("use = " + DebugBeIncludeText(beuse.file), 3);

			for (const string& name : beuse.nameVec)
			{
				std::stringstream linesStream;

				auto & linesItr = beuse.nameMap.find(name);
				if (linesItr != beuse.nameMap.end())
				{
					const std::set<int> &lines = linesItr->second;
					int n = (int)lines.size();
					int i = 0;
					for (int line : lines)
					{
						linesStream << htmltool::get_number_html(line);
						if (++i < n)
						{
							linesStream << ", ";
						}
					}
				}

				std::string linesText = linesStream.str();
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

	// 打印主文件的依赖文件集
	void ParsingFile::PrintTopRely() const
	{
		HtmlDiv &div = HtmlLog::instance.m_newDiv;
		div.AddRow(AddPrintIdx() + ". list of top rely: file count = " + htmltool::get_number_html(m_topRelys.size()), 1);

		for (auto &file : m_topRelys)
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
		HtmlDiv &div = HtmlLog::instance.m_newDiv;
		div.AddRow(AddPrintIdx() + ". list of rely : file count = " + htmltool::get_number_html(m_relys.size()), 1);

		for (auto & file : m_relys)
		{
			if (!IsNeedPrintFile(file))
			{
				continue;
			}

			if (m_topRelys.find(file) == m_topRelys.end())
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
		HtmlDiv &div = HtmlLog::instance.m_newDiv;
		div.AddRow(AddPrintIdx() + ". list of rely children file: file count = " + htmltool::get_number_html(m_relyKids.size()), 1);

		for (auto & usedItr : m_relyKids)
		{
			FileID file = usedItr.first;

			if (!CanClean(file))
			{
				continue;
			}

			div.AddRow(DebugParentFileText(file, usedItr.second.size()), 2);

			for (auto & usedChild : usedItr.second)
			{
				div.AddRow("use children " + DebugBeIncludeText(usedChild), 3);
			}

			div.AddRow("");
		}
	}

	// 打印可转为前置声明的类指针或引用记录
	void ParsingFile::PrintUseRecord() const
	{
		// 1. 将文件前置声明记录按文件进行归类
		UseRecordsByFileMap recordMap;

		for (auto &itr : m_locUseRecordPointers)
		{
			SourceLocation loc	= itr.first;
			FileID file	= GetFileID(loc);
			recordMap[file].insert(itr);
		}
		recordMap.erase(FileID());

		// 2. 打印
		HtmlDiv &div = HtmlLog::instance.m_newDiv;
		div.AddRow(AddPrintIdx() + ". use records list: file count = " + htmltool::get_number_html(recordMap.size()), 1);

		for (auto &itr : recordMap)
		{
			FileID file = itr.first;
			const LocUseRecordsMap &locToRecords = itr.second;

			div.AddRow(DebugParentFileText(file, locToRecords.size()), 2);

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

	// 打印最终的前置声明记录
	void ParsingFile::PrintForwardClass() const
	{
		if (m_fowardClass.empty())
		{
			return;
		}

		HtmlDiv &div = HtmlLog::instance.m_newDiv;
		div.AddRow(AddPrintIdx() + ". final forward class list: file count = " + htmltool::get_number_html(m_fowardClass.size()), 1);

		for (auto &itr : m_fowardClass)
		{
			FileID by = itr.first;
			const RecordSet &records = itr.second;

			div.AddRow(DebugParentFileText(by, records.size()), 2);

			for (const CXXRecordDecl *record : records)
			{
				div.AddRow("add forward class = " + GetRecordName(*record), 3);
			}

			div.AddRow("");
		}
	}

	// 打印允许被清理的所有文件列表
	void ParsingFile::PrintAllFile() const
	{
		HtmlDiv &div = HtmlLog::instance.m_newDiv;
		div.AddRow(AddPrintIdx() + ". list of all file: file count = " + htmltool::get_number_html(m_files.size()), 1);

		for (FileID file : m_files)
		{
			if (!IsNeedPrintFile(file))
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
		HtmlDiv &div = HtmlLog::instance.m_newDiv;
		div.AddRow(AddPrintIdx() + ". each file's namespace: file count = " + htmltool::get_number_html(m_namespaces.size()), 1);

		for (auto &itr : m_namespaces)
		{
			FileID file = itr.first;
			const std::set<std::string> &namespaces = itr.second;

			if (!IsNeedPrintFile(file))
			{
				continue;
			}

			div.AddRow(DebugParentFileText(file, namespaces.size()), 2);

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

		HtmlDiv &div = HtmlLog::instance.m_newDiv;
		div.AddRow(AddPrintIdx() + ". each file's using namespace: file count = " + htmltool::get_number_html(nsByFile.size()), 1);

		for (auto &itr : nsByFile)
		{
			FileID file = itr.first;
			const std::set<std::string> &namespaces = itr.second;

			if (!IsNeedPrintFile(file))
			{
				continue;
			}

			div.AddRow(DebugParentFileText(file, namespaces.size()), 2);

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
		HtmlDiv &div = HtmlLog::instance.m_newDiv;
		div.AddRow(AddPrintIdx() + ". same file list: file count = " + htmltool::get_number_html(m_sameFiles.size()), 1);

		for (auto &itr : m_sameFiles)
		{
			const std::string &fileName			= itr.first;
			const FileSet sameFiles	= itr.second;

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

			div.AddRow(DebugParentFileText(by, kidItr.second.size()), 2);

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

			div.AddRow(DebugParentFileText(by, kidItr.second.size()), 2);

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

			div.AddRow(DebugParentFileText(file, itr.second.size()), 2);

			for (FileID beuse : itr.second)
			{
				div.AddRow("be user use = " + DebugBeIncludeText(beuse), 3);
			}

			div.AddRow("");
		}
	}
}