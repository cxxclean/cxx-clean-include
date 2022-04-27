//------------------------------------------------------------------------------
// �ļ�: parser.cpp
// ����: ������
// ˵��: ������ǰcpp�ļ�
//------------------------------------------------------------------------------

#include "parser.h"
#include <sstream>
#include <fstream>

#include <clang/AST/DeclTemplate.h>
#include <clang/Lex/HeaderSearch.h>
#include <clang/Lex/MacroInfo.h>
#include <clang/Lex/Lexer.h>
#include <clang/Lex/Preprocessor.h>
#include <clang/Rewrite/Core/Rewriter.h>
#include "clang/Frontend/CompilerInstance.h"

// ����3��#include��_chmod������Ҫ�õ�
#include <io.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "tool.h"
#include "project.h"
#include "html_log.h"

ParsingFile* ParsingFile::g_nowFile = nullptr;

// set��ȥset
template <typename T>
inline void Del(std::set<T> &a, const std::set<T> &b)
{
	for (const T &t : b)
	{
		a.erase(t);
	}
}

// set��ȥmap
template <typename Key, typename Val>
inline void Del(std::set<Key> &a, const std::map<Key, Val> &b)
{
	for (const auto &itr : b)
	{
		a.erase(itr.first);
	}
}

// map��ȥset
template <typename Key, typename Val>
inline void Del(std::map<Key, Val> &a, const std::set<Key> &b)
{
	for (const Key &t : b)
	{
		a.erase(t);
	}
}

// set����set�з��������ĳ�Ա
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

// ɾ��set�з���ָ��������Ԫ��
template <typename Container, typename Op>
inline void EraseIf(Container& container, const Op& op)
{
	for (auto it = container.begin(); it != container.end(); )
	{
		if (op(*it)) container.erase(it++);
		else ++it;
	}
}

// ɾ��map�з���ָ��������Ԫ��
template <typename Container, typename Op>
inline void MapEraseIf(Container& container, const Op& op)
{
	for (auto it = container.begin(); it != container.end(); )
	{
		if (op(it->first, it->second)) container.erase(it++);
		else ++it;
	}
}

// ��ѯmap��a����Ӧ��ֵ�Ƿ������b
template <typename Container, typename Key1, typename Key2>
inline bool HasInMap(const Container &container, const Key1 &a, const Key2 &b)
{
	auto itr = container.find(a);
	if (itr == container.end())
	{
		return false;
	}

	return Has(itr->second, b);
}

// ��ȡ�����ļ����������������ļ�������a����b1��b2��b1��b2��������b3 ~ b100��������a����b1 ~ b100��
template <typename T, typename AddTodoFunc>
void GetChain(std::set<T> &chain, T top, const AddTodoFunc& expand)
{
	std::set<T> todo;
	std::set<T> &done = chain;

	todo.insert(top);

	while (!todo.empty())
	{
		const T &cur = *todo.begin();

		if (done.find(cur) == done.end())
		{
			done.insert(cur);

			std::set<T> more;
			expand(done, more, cur);

			todo.erase(todo.begin());
			Add(todo, more);
		}
		else
		{
			todo.erase(todo.begin());
		}
	}
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

// ��ӳ�Ա�ļ�
void ParsingFile::AddFile(FileID file)
{
	if (file.isInvalid())
	{
		return;
	}

	m_files.insert(file);

	// ��¼�ļ���
	const std::string fileName = GetAbsoluteFileName(file);
	if (!fileName.empty())
	{
		const std::string lowerFileName = strtool::tolower(fileName);

		m_fileNames.insert(std::make_pair(file, fileName));
		m_lowerFileNames.insert(std::make_pair(file, lowerFileName));
		m_sameFiles[lowerFileName].insert(file);

		if (!Has(m_fileNameToFileIDs, lowerFileName))
		{
			m_fileNameToFileIDs.insert(std::make_pair(lowerFileName, file));
		}

		if (Project::instance.IsSkip(lowerFileName.c_str()))
		{
			m_skips.insert(file);
		}
	}

	// ��Ӱ����ļ���Ϣ
	FileID parent = m_srcMgr->getFileID(m_srcMgr->getIncludeLoc(file));
	if (parent.isValid())
	{
		if (IsForceInclude(file))
		{
			parent = m_root;
		}

		if (file != parent)
		{
			const std::string parentName = strtool::tolower(GetAbsoluteFileName(parent));

			m_parents[file] = parent;
			m_includes[parentName].insert(file);
		}
	}
}

// ��ȡͷ�ļ�����·��
vector<ParsingFile::HeaderSearchDir> ParsingFile::TakeHeaderSearchPaths(const clang::HeaderSearch &headerSearch) const
{
	typedef clang::ConstSearchDirIterator search_iterator;

	IncludeDirMap dirs;

	auto AddIncludeDir = [&](search_iterator beg, search_iterator end, SrcMgr::CharacteristicKind includeKind)
	{
		// ��ȡϵͳͷ�ļ�����·��
		for (auto itr = beg; itr != end; ++itr)
		{
			if (const DirectoryEntry* entry = itr->getDir())
			{
				const string path = pathtool::fix_path(entry->getName().data());
				dirs.insert(make_pair(path, includeKind));
			}
		}
	};

	// ��ȡϵͳͷ�ļ�����·��
	AddIncludeDir(headerSearch.system_dir_begin(), headerSearch.system_dir_end(), SrcMgr::C_System);
	AddIncludeDir(headerSearch.search_dir_begin(), headerSearch.search_dir_end(), SrcMgr::C_User);

	return SortHeaderSearchPath(dirs);
}

// ��ͷ�ļ�����·�����ݳ����ɳ���������
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

	// ���ݳ����ɳ���������
	sort(dirs.begin(), dirs.end(), [](const ParsingFile::HeaderSearchDir& left, const ParsingFile::HeaderSearchDir& right)
	{
		return left.m_dir.length() > right.m_dir.length();
	});

	return dirs;
}

// ����ͷ�ļ�����·����������·��ת��Ϊ˫���Ű�Χ���ı�����
string ParsingFile::GetQuotedIncludeStr(const char *absoluteFilePath) const
{
	string path = pathtool::simplify_path(absoluteFilePath);

	for (const HeaderSearchDir &itr : m_headerSearchPaths)
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

// 2���ļ��Ƿ��ļ���һ��
inline bool ParsingFile::IsSameName(FileID a, FileID b) const
{
	return (std::string(GetLowerFileNameInCache(a)) == GetLowerFileNameInCache(b));
}

// ����
void ParsingFile::Analyze()
{
	LogInfoByLvl(LogLvl_3, "------ Analyze ------");

	// ������δ����Ǳ����ߵ���Ҫ����˼·

	// 1. ��¼��ÿ���û��ļ���ʹ�õ������û��ļ�
	LogInfoByLvl(LogLvl_3, "<<generate user use>>");
	GenerateUserUse();

	// 2. ������ÿ���ļ�Ӧ�������ļ��б�
	LogInfoByLvl(LogLvl_3, "<<generate minimum include>>");
	GenerateMinInclude();

	// 3. ��¼��Ӧ���ɵ�ǰ�������б�
	LogInfoByLvl(LogLvl_3, "<<generate forward class>>");
	GenerateForwardClass();

	// ���������ȡ��
	TakeHistorys(m_historys);
	MergeTo(ProjectHistory::instance.m_files);
}

// ��ǰcpp�ļ�������ʼ
void ParsingFile::Begin()
{
	LogInfoByLvl(LogLvl_3, "------ Begin ------");

	// 1. ����ÿ���ļ��ĺ���ļ�����������������Ҫ�õ���
	m_files.erase(FileID());

	for (const auto &itr : m_includes)
	{
		const std::string &top = itr.first;
		FileNameSet &kids = m_kidsByName[top];
		kids.clear();
		GetChain(kids, top, [&](const FileNameSet &done, FileNameSet &todo, const std::string &cur)
		{
			auto includeItr = m_includes.find(cur);
			if (includeItr != m_includes.end())
			{
				const FileSet &includeList = includeItr->second;
				for (FileID beInclude : includeList)
				{
					todo.insert(GetLowerFileNameInCache(beInclude));
				}
			}
		});

		kids.erase(top);
	}
	
	// 2. ɾ��ֻ������һ�ε��ļ����������������������ļ�
	MapEraseIf(m_sameFiles, [&](const std::string&, const FileSet &sameFiles)
	{
		return sameFiles.size() <= 1;
	});


	// 3. ��¼��ǿ�ư����ļ��б���Ϊǿ�ư����ļ��������ļ���Ӧ���Ķ���
	LogInfoByLvl(LogLvl_3, "<<generate default includes>>");
	GenerateDefaultIncludes();

	// 4. ��¼��ÿ���ⲿ�ļ����ⲿ����
	LogInfoByLvl(LogLvl_3, "<<generate out files>>");
	GenerateOutFileAncestor();
}

// ��ǰcpp�ļ���������
void ParsingFile::End()
{
	Analyze();
	Print();
	Clean();

	LogInfoByLvl(LogLvl_3, "------ End ------");
}

// ɾ�������ļ�������ֵ��true������ɾ����false����δɾ��
bool ParsingFile::CutInclude(FileID top, FileSet &done, FileSet &kids)
{
	// ÿ���ļ������������ļ��Ƚϣ�������һ����������һ������ɾ����������һ��
	for (FileID cur : kids)
	{
		if (Has(done, cur))
		{
			// �����Ѵ�������ļ�
			continue;
		}

		done.insert(cur);

		FileSet eraseList;

		// ��ʼ�Ƚ�
		for (FileID other : kids)
		{
			if (cur == other)
			{
				continue;
			}

			// ͬ��
			if (IsSameName(cur, other))
			{
				LogInfoByLvl(LogLvl_2, "[cur]'name = [other]'name: erase [other](top = " << GetDebugFileName(top) << ", cur = " << GetDebugFileName(cur) << ", other = " << GetDebugFileName(other) << ")");
				eraseList.insert(other);
			}
			// ��ǰ�ļ����������ļ�
			else if (Contains(cur, other))
			{
				LogInfoByLvl(LogLvl_2, "[cur] > [other]: erase [other](top = " << GetDebugFileName(top) << ", cur = " << GetDebugFileName(cur) << ", other = " << GetDebugFileName(other) << ")");
				eraseList.insert(other);
			}
			// �����ļ��Ѿ���clangǿ�ư�����
			else if (IsAncestorDefaultInclude(other))
			{
				LogInfoByLvl(LogLvl_2, "default includes: erase [other](top = " << GetDebugFileName(top) << ", other = " << GetDebugFileName(other) << ")");
				eraseList.insert(other);
			}
		}

		if (!eraseList.empty())
		{
			// ɾ�������ļ�
			Del(kids, eraseList);
			return true;
		}
	}

	return false;
}

// �ϲ������ļ���ÿ���ļ���¼��Ӧ���������е��ļ���������һ�����Ѿ������ļ��������ˣ������Ƴ�����
bool ParsingFile::MergeMinInclude()
{
	// ɾ���ռ�¼
	MapEraseIf(m_minInclude, [&](FileID, const FileSet &minIncludes)
	{
		return minIncludes.empty();
	});

	// �ϲ�
	for (auto &itr : m_minInclude)
	{
		FileID top		= itr.first;
		FileSet &kids	= itr.second;

		FileSet done;
		while (CutInclude(top, done, kids)){}
	}

	return false;
}

// �Ƿ��û��ļ����ɱ��޸ĵ��ļ���Ϊ�û��ļ����������Ϊ�ⲿ�ļ���
inline bool ParsingFile::IsUserFile(FileID file) const
{
	bool isUserFile = !IsSystemHeader(file)
		&& CanClean(file)
		&& !IsAncestorDefaultInclude(file)
		&& !IsAncestorSkip(file);

	return isUserFile;
}

// �Ƿ��ⲿ�ļ����ɱ��޸ĵ��ļ���Ϊ�û��ļ����������Ϊ�ⲿ�ļ���
inline bool ParsingFile::IsOuterFile(FileID file) const
{
	return !Has(m_userFiles, file) && file.isValid();
}

// ��ȡ�ⲿ�ļ�����
inline FileID ParsingFile::GetOuterFileAncestor(FileID file) const
{
	auto itr = m_outFileAncestor.find(file);
	return itr != m_outFileAncestor.end() ? itr->second : file;
}

// ����Ĭ�ϱ��������ļ��б�
void ParsingFile::GenerateDefaultIncludes()
{
	for (FileID file : m_files)
	{
		if (IsDefaultIncluded(file))
		{
			m_defaultIncludes.insert(file);
		}
	}
}

// ��ȡ�ⲿ�ļ�����
void ParsingFile::GenerateOutFileAncestor()
{
	// 1. �����û��ļ��б�
	for (FileID file : m_files)
	{
		if (IsUserFile(file))
		{
			m_userFiles.insert(file);
		}
	}

	// 2. ��¼ÿ���ⲿ�ļ����ⲿ�ļ�����
	for (FileID file : m_files)
	{
		if (!IsOuterFile(file))
		{
			continue;
		}

		FileID outerFileAncestor = file;

		for (FileID parent = GetParent(file); IsOuterFile(parent); parent = GetParent(parent))
		{
			outerFileAncestor = parent;
		}

		if (outerFileAncestor.isValid() && outerFileAncestor != file)
		{
			m_outFileAncestor[file] = outerFileAncestor;
		}
	}
}

// �����û��ļ���������¼
void ParsingFile::GenerateUserUse()
{
	// ���δ���ÿ��ԭʼ��������ϵ�������������û��ļ�����һ�㣩
	for (const auto &itr : m_uses)
	{
		FileID by				= itr.first;
		const FileSet &useList	= itr.second;

		// ���Ա�Ĭ�ϰ������ļ�
		if (IsAncestorDefaultInclude(by))
		{
			continue;
		}

		// �Ƿ��ⲿ�ļ�
		bool isByOuter = IsOuterFile(by);

		// �ⲿ�ļ�����
		FileID byAncestor = GetOuterFileAncestor(by);

		FileSet userUseList;
		for (FileID beUse : useList)
		{
			// �����ⲿ�ļ����õĺ���ļ�
			if (isByOuter)
			{
				if (IsAncestorByName(beUse, by))
				{
					continue;
				}
			}

			FileID bestAncestor = GetBestAncestor(by, beUse);
			userUseList.insert(bestAncestor);
		}

		userUseList.erase(byAncestor);
		if (!userUseList.empty())
		{	
			// �ϲ������еļ�¼
			Add(m_userUses[GetLowerFileNameInCache(byAncestor)], userUseList);
		}
	}
}

// �����ÿ���ļ�Ӧ�����������ļ�
void ParsingFile::GenerateMinInclude()
{
	// ��ͳ�Ƴ�ÿ���û��ļ����������������ļ�
	for (auto &itr : m_userUses)
	{
		FileSet &useFiles = itr.second;
		FileSet copy = useFiles;

		useFiles.clear();
		for (FileID beUse : copy)
		{
			FileID better = GetFirstFileID(beUse);
			if (better.isValid())
			{
				useFiles.insert(better);
			}
		}
	}

	for (const auto &itr : m_userUses)
	{
		const std::string &topName = itr.first;
		FileID top = GetFileIDByFileName(topName.c_str());

		FileSet chain;
		GetChain(chain, top, [&](const FileSet &done, FileSet &todo, FileID cur)
		{
			// �����µ�ǰ�ļ������������ļ�
			auto useItr = m_userUses.find(GetLowerFileNameInCache(cur));
			if (useItr != m_userUses.end())
			{
				const FileSet &useFiles = useItr->second;
				for (FileID beUse : useFiles)
				{
					// ֻ��չ����ļ�
					if (!IsAncestorByName(beUse, top))
					{
						continue;
					}

					todo.insert(beUse);
				}
			}
		});

		chain.erase(top);

		if (!chain.empty())
		{
			Add(m_minInclude[top], chain);
		}
	}

	m_minKids = m_minInclude;

	// 2. �ϲ�
	MergeMinInclude();
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

// �ļ��Ƿ���Ĭ�ϱ�����
bool ParsingFile::IsDefaultIncluded(FileID file) const
{
	return IsForceInclude(file) || IsPrecompileHeader(file);
}

// �����ս���У��ļ�a�Ƿ�������ļ�b
inline bool ParsingFile::Contains(FileID top, FileID kid) const
{
	if (IsOuterFile(top) && IsAncestorByName(kid, top))
	{
		return true;
	}

	auto itr = m_minKids.find(top);
	if (itr == m_minKids.end())
	{
		return false;
	}

	const FileSet &minKids = itr->second;
	if (Has(minKids, kid))
	{
		return true;
	}

	for (FileID minKid : minKids)
	{
		if (IsOuterFile(minKid) && IsAncestorByName(kid, minKid))
		{
			return true;
		}
	}

	return false;
}

// ��ȡ���ļ���һ�α�����ʱ���ļ�ID��ͬһ�ļ����ܰ�����Σ���Ӧ���ж���ļ�ID��
FileID ParsingFile::GetFirstFileID(FileID file) const
{
	return GetFileIDByFileName(GetLowerFileNameInCache(file));
}

// ��ȡ�ļ�����Ӧ���ļ�ID��ͬһ�ļ����ܰ�����Σ���Ӧ���ж���ļ�ID������ȡ��һ����
FileID ParsingFile::GetFileIDByFileName(const char *fileName) const
{
	auto itr = m_fileNameToFileIDs.find(fileName);
	if (itr != m_fileNameToFileIDs.end())
	{
		return itr->second;
	}

	return FileID();
}

// a�ļ��Ƿ���bλ��֮ǰ
bool ParsingFile::IsFileBeforeLoc(FileID a, SourceLocation b) const
{
	SourceLocation includeLoc = m_srcMgr->getIncludeLoc(a);
	return isBeforeInTranslationUnit(includeLoc, b);
}

// a�ļ��Ƿ���b�ļ�֮ǰ
bool ParsingFile::IsFileBeforeFile(FileID a, FileID b) const
{
	SourceLocation aIncludeLoc = m_srcMgr->getIncludeLoc(a);
	SourceLocation bIncludeLoc = m_srcMgr->getIncludeLoc(b);
	return isBeforeInTranslationUnit(aIncludeLoc, bIncludeLoc);
}

// �����ļ��Ƿ�Ĭ�ϱ�����
inline bool ParsingFile::IsAncestorDefaultInclude(FileID file) const
{
	for (FileID defaultInclude : m_defaultIncludes)
	{
		if (file == defaultInclude || IsAncestorByName(file, defaultInclude))
		{
			return true;
		}
	}

	return false;
}

// �����ļ��Ƿ�ǿ�ƺ���
inline bool ParsingFile::IsAncestorSkip(FileID file) const
{
	for (FileID skip : m_skips)
	{
		if (file == skip || IsAncestorByName(file, skip))
		{
			return true;
		}
	}

	return false;
}

// ��ȡ�ļ�����ȣ������ļ������Ϊ0��
int ParsingFile::GetDepth(FileID child) const
{
	int depth = 0;

	for (FileID parent; (parent = GetParent(child)).isValid(); child = parent)
	{
		++depth;
	}

	return depth;
}

// ���ļ��Ƿ�Ӧ���������õ�class��struct��union��ǰ������
bool ParsingFile::IsShouldKeepForwardClass(FileID by, const CXXRecordDecl &cxxRecord) const
{
	auto IsAnyKidHasRecord = [&](FileID recordAtFile) -> bool
	{
		if (Contains(by, recordAtFile))
		{
			LogInfoByLvl(LogLvl_2, "[skip record]: record has been contained. by = " << GetDebugFileName(by) << ", file = " << GetDebugFileName(recordAtFile) << ", record = " << cxxRecord.getNameAsString());
			return true;
		}
		else if (IsAncestorDefaultInclude(recordAtFile))
		{
			LogInfoByLvl(LogLvl_2, "[skip record]: record is default included: record = [" <<  GetRecordName(cxxRecord) << "], by = " << GetDebugFileName(by) << ", record file = " << GetDebugFileName(recordAtFile));
			return true;
		}
		else if (IsSameName(by, recordAtFile))
		{
			LogInfoByLvl(LogLvl_2, "[skip record]: record is at same file: record = [" <<  GetRecordName(cxxRecord) << "], by = " << GetDebugFileName(by) << ", record file = " << GetDebugFileName(recordAtFile));
			return true;
		}

		return false;
	};

	// ����������������ļ���������������һ��������������Ҫ�ټ�ǰ������
	const TagDecl *first = cxxRecord.getFirstDecl();
	for (const TagDecl *next : first->redecls())
	{
		FileID recordAtFile = GetFileID(next->getLocation());
		if (IsAnyKidHasRecord(recordAtFile))
		{
			return false;
		}
	}

	return true;
}

// ��������ǰ�������б�
void ParsingFile::GenerateForwardClass()
{
	// 1. ���һЩ����ȷ֪����Ҫ�����ඨ�������ǰ������
	for (auto &itr : m_fileUseRecordPointers)
	{
		FileID by = itr.first;
		RecordSet &records = itr.second;

		auto beUseItr = m_fileUseRecords.find(by);
		if (beUseItr != m_fileUseRecords.end())
		{
			const RecordSet &beUseRecords = beUseItr->second;
			Del(records, beUseRecords);
		}

		m_fowardClass[GetFirstFileID(by)] = records;
	}

	// 2. ɾ���ظ�ǰ������
	MinimizeForwardClass();

	// 3. �����ļ��޷�����ǰ�����������ǰ������ֱ��һ�������Ų
	bool isAnyForwardError = true;
	while (isAnyForwardError)
	{
		isAnyForwardError = false;

		for (auto &forwardClassItr : m_fowardClass)
		{
			FileID by = forwardClassItr.first;
			
			// ���ļ�����#include˵�����Է�ǰ�����������Ժ���
			if (Has(m_includes, GetLowerFileNameInCache(by)))
			{
				continue;
			}

			// ע���������
			const RecordSet records = forwardClassItr.second;

			for (auto &itr : m_minInclude)
			{
				FileID at = itr.first;
				const FileSet &includes = itr.second;

				if (Has(includes, by))
				{
					Add(m_fowardClass[at], records);
				}
			}

			isAnyForwardError = true;

			m_fowardClass.erase(by);
			break;
		}
	};

	MinimizeForwardClass();
}

// �ü�ǰ�������б�ɾ���ظ��ģ�
void ParsingFile::MinimizeForwardClass()
{
	MapEraseIf(m_fowardClass, [&](FileID by, RecordSet &records)
	{
		EraseIf(records, [&](const CXXRecordDecl* record)
		{
			bool should_keep = g_nowFile->IsShouldKeepForwardClass(by, *record);
			if (should_keep)
			{
				LogErrorByLvl(LogLvl_2, "IsShouldKeepForwardClass = true: " << GetDebugFileName(by) << "," << GetRecordName(*record));
			}

			return !should_keep;
		});

		return records.empty();
	});

	// 1. ��ͳ�Ƴ�ÿ���ļ�����������ǰ��������[�ļ�] -> [���ļ�������������ǰ������]
	FileSet all;
	Add(all, m_fileUseRecordPointers);
	Add(all, m_minInclude);

	FileUseRecordsMap bigForwards;

	for (FileID by : all)
	{
		by = GetFirstFileID(by);
		GetAllForwardsInKids(by, bigForwards[by]);
	}

	m_fowardClass.clear();

	// 2. ɾ������ǰ������������Ҫ��������ļ����е�ǰ��������
	for (auto &itr : bigForwards)
	{
		FileID by = itr.first;
		RecordSet smallForwards = itr.second;	// ����������

		// ���ļ�Ӧ������ǰ������ = [���ļ�������ǰ������]��ȥ[���������ǰ������]
		auto includeItr = m_minInclude.find(by);
		if (includeItr != m_minInclude.end())
		{
			const FileSet &minIncludes = includeItr->second;

			for (FileID minInclude : minIncludes)
			{
				auto recordItr = bigForwards.find(minInclude);
				if (recordItr != bigForwards.end())
				{
					const RecordSet &records = recordItr->second;
					Del(smallForwards, records);
				}
			}
		}

		if (!smallForwards.empty())
		{
			m_fowardClass[by] = smallForwards;
		}
	}
}

// ��ȡָ���ļ��������ļ�������ǰ�������б�
void ParsingFile::GetAllForwardsInKids(FileID top, RecordSet &forwards)
{
	if (top.isInvalid())
	{
		return;
	}

	// 1. ͳ�Ƶ�ǰ�ļ����հ��������к���ļ�
	FileSet chain;
	GetChain(chain, top, [&](const FileSet &done, FileSet &todo, FileID cur)
	{
		auto useItr = m_minInclude.find(cur);
		if (useItr != m_minInclude.end())
		{
			const FileSet &minIncludes = useItr->second;
			AddIf(todo, minIncludes, [&done](FileID cur)
			{
				return done.find(cur) == done.end();
			});
		}
	});

	// 2. ����Щ����ļ�������ǰ�������ϵ�һ��
	for (FileID file : chain)
	{
		auto itr = m_fowardClass.find(file);
		if (itr != m_fowardClass.end())
		{
			Add(forwards, itr->second);
		}
	}
}

// ��ȡָ����Χ���ı�
std::string ParsingFile::GetSourceOfRange(SourceRange range) const
{
	if (range.isInvalid())
	{
		return "";
	}

	range = m_srcMgr->getExpansionRange(range).getAsRange();

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
		// ע�⣺����������ж�end < beg��������ܻ������Ϊ�п���ĩβ��������ʼ�ַ�ǰ�棬�����ں�֮��
		return "";
	}

	return string(beg, end);
}

// ��ȡָ��λ�õ��ı�
const char* ParsingFile::GetSourceAtLoc(SourceLocation loc) const
{
	bool err = true;

	const char* str = m_srcMgr->getCharacterData(loc, &err);
	return err ? nullptr : str;
}

// ��ȡָ��λ�������е��ı�
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

// ���ݴ���Ĵ���λ�÷�����һ�еķ�Χ
SourceRange ParsingFile::GetNextLine(SourceLocation loc) const
{
	SourceRange curLine			= GetCurLine(loc);
	SourceLocation lineEnd		= curLine.getEnd();
	SourceLocation fileEndLoc	= m_srcMgr->getLocForEndOfFile(GetFileID(loc));

	if (isBeforeInTranslationUnit(fileEndLoc, lineEnd) || fileEndLoc == lineEnd)
	{
		return SourceRange(fileEndLoc, fileEndLoc);
	}

	const char* c1 = GetSourceAtLoc(lineEnd);
	const char* c2 = GetSourceAtLoc(lineEnd.getLocWithOffset(1));

	if (nullptr == c1 || nullptr == c2)
	{
		LogErrorByLvl(LogLvl_2, "GetNextLine = null");
		return SourceRange(fileEndLoc, fileEndLoc);
	}

	int skip = 0;

	// windows���и�ʽ����
	if (*c1 == '\r' && *c2 == '\n')
	{
		skip = 2;
	}
	// ��Unix���и�ʽ����
	else if (*c1 == '\n')
	{
		skip = 1;
	}

	SourceRange nextLine= GetCurLine(lineEnd.getLocWithOffset(skip));
	return nextLine;
}

// ��ȡ�к�
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

// ��ȡ�ļ���Ӧ�ı������к�
int ParsingFile::GetIncludeLineNo(FileID file) const
{
	if (IsForceInclude(file))
	{
		return 0;
	}

	return GetLineNo(m_srcMgr->getIncludeLoc(file));
}

// ��ȡ�ļ���Ӧ��#include����Χ
SourceRange ParsingFile::GetIncludeRange(FileID file) const
{
	SourceLocation includeLoc = m_srcMgr->getIncludeLoc(file);
	return GetCurFullLine(includeLoc);
}

// �Ƿ��ǻ��з�
bool ParsingFile::IsNewLineWord(SourceLocation loc) const
{
	string text = GetSourceOfRange(SourceRange(loc, loc.getLocWithOffset(1)));
	return text == "\r" || text == "";
}

// ��ȡ�ļ���Ӧ��#include���ڵ��У��������з���
std::string ParsingFile::GetBeIncludeLineText(FileID file) const
{
	SourceLocation loc = m_srcMgr->getIncludeLoc(file);
	return GetSourceOfLine(loc);
}

// aλ�õĴ���ʹ��bλ�õĴ���
inline void ParsingFile::Use(SourceLocation a, SourceLocation b, const char* name /* = nullptr */)
{
	if (IsInSystemHeader(a))
	{
		return;
	}

	a = GetExpasionLoc(a);
	b = GetExpasionLoc(b);

	UseInclude(GetFileID(a), GetFileID(b), name, GetLineNo(a));
}

// ��¼����������ϵ�������ļ�a�������ļ�b��ĳ�е�ĳ����������������
inline void ParsingFile::UseName(FileID file, FileID beusedFile, const char* name /* = nullptr */, int line)
{
	if (Project::instance.m_logLvl < LogLvl_2)
	{
		return;
	}

	if (nullptr == name)
	{
		return;
	}

	// �ҵ����ļ���ʹ��������ʷ��¼����Щ��¼���ļ�������
	std::vector<UseNameInfo> &useNames = m_useNames[file];

	bool found = false;

	// �����ļ����ҵ���Ӧ�ļ��ı�ʹ��������ʷ������������ʹ�ü�¼
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
		useNames.resize(useNames.size() + 1);

		UseNameInfo &info = useNames.back();
		info.file = beusedFile;
		info.AddName(name, line);
	}
}

// ��2���ļ��Ƿ��ǵ�1���ļ�������
inline bool ParsingFile::IsAncestorByName(FileID young, FileID old) const
{
	return IsAncestorByName(GetLowerFileNameInCache(young), GetLowerFileNameInCache(old));
}

// ��2���ļ��Ƿ��ǵ�1���ļ�������
inline bool ParsingFile::IsAncestorByName(const char *young, const char *old) const
{
	return HasInMap(m_kidsByName, old, young);
}

// ��ȡ���ļ������ļ�û�и��ļ���
inline FileID ParsingFile::GetParent(FileID child) const
{
	if (child == m_root)
	{
		return FileID();
	}

	auto itr = m_parents.find(child);
	if (itr != m_parents.end())
	{
		return itr->second;
	}

	return FileID();
}

// a�ļ�ʹ��b�ļ�
inline void ParsingFile::UseInclude(FileID a, FileID b, const char* name /* = nullptr */, int line)
{
	if (a == b || a.isInvalid() || b.isInvalid())
	{
		return;
	}

	if (nullptr == m_srcMgr->getFileEntryForID(a) || nullptr == m_srcMgr->getFileEntryForID(b))
	{
		LogErrorByLvl(LogLvl_Max, "m_srcMgr->getFileEntryForID(a) failed!" << m_srcMgr->getFilename(m_srcMgr->getLocForStartOfFile(a)) << ":" << m_srcMgr->getFilename(m_srcMgr->getLocForStartOfFile(b)));
		LogErrorByLvl(LogLvl_Max, "m_srcMgr->getFileEntryForID(b) failed!" << GetSourceOfLine(m_srcMgr->getLocForStartOfFile(a)) << ":" << GetSourceOfLine(m_srcMgr->getLocForStartOfFile(b)));

		return;
	}

	if (!IsSameName(a, b))
	{
		m_uses[a].insert(b);
		UseName(a, b, name, line);
	}
}

// ��ǰλ��ʹ��ָ���ĺ�
void ParsingFile::UseMacro(SourceLocation loc, const MacroDefinition &macro, const Token &macroNameTok, const MacroArgs *args /* = nullptr */)
{
	if (IsInSystemHeader(loc))
	{
		return;
	}

	MacroInfo *info = macro.getMacroInfo();
	if (info)
	{
		std::string name;
		GetNameForLog(name, macroNameTok.getIdentifierInfo()->getName().str() << "[macro]");

		Use(loc, info->getDefinitionLoc(), name.c_str());
	}
}

void ParsingFile::UseContext(SourceLocation loc, const DeclContext *context)
{
	while (context && context->isNamespace())
	{
		const NamespaceDecl *ns = cast<NamespaceDecl>(context);
		context = context->getParent();
	}
}

// ����Ƕ���������η�
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

// ���������ռ�����
void ParsingFile::UseNamespaceDecl(SourceLocation loc, const NamespaceDecl *ns)
{
	UseNameDecl(loc, ns);
}

bool ParsingFile::isBeforeInTranslationUnit(SourceLocation LHS, SourceLocation RHS) const
{
	if (LHS == RHS)
		return false;

	// �ں����ļ����ҳ�b������
	auto CompareLocInSameFile = [&](bool isLeftLow, SourceLocation lowLoc, SourceLocation highLoc)
	{
		int lowOffset = m_srcMgr->getFileOffset(lowLoc);
		int highOffset = m_srcMgr->getFileOffset(highLoc);

		return isLeftLow ? lowOffset < highOffset : highOffset < lowOffset;
	};

	bool isLeftLow = true;

	SourceLocation lowLoc = LHS;
	SourceLocation highLoc = RHS;

	FileID lowFile = GetFileID(LHS);
	FileID highFile = GetFileID(RHS);

	int low = GetDepth(lowFile);
	int high = GetDepth(highFile);

	//LogInfo("===>" << GetLowerFileNameInCache(lowFile) << ", deep = " << low);
	//LogInfo("===>" << GetLowerFileNameInCache(highFile) << ", deep = " << high);

	if (low < high)
	{
		isLeftLow = false;

		std::swap(low, high);
		std::swap(lowLoc, highLoc);
		std::swap(lowFile, highFile);
	}

	if (lowFile == highFile)
	{
		return CompareLocInSameFile(isLeftLow, lowLoc, highLoc);
	}

	for (int up = 0; high + up < low ; ++up)
	{
		FileID lowParent = GetParent(lowFile);
		if (lowParent == highFile)
		{
			return CompareLocInSameFile(isLeftLow, m_srcMgr->getIncludeLoc(lowFile), highLoc);
		}

		lowFile = lowParent;
	}

	for (int up = 0; up < high; ++up)
	{
		FileID lowParent = GetParent(lowFile);
		FileID highParent = GetParent(highFile);

		if (lowParent == highParent)
		{
			return CompareLocInSameFile(isLeftLow, m_srcMgr->getIncludeLoc(lowFile), m_srcMgr->getIncludeLoc(highFile));
		}

		lowFile = lowParent;
		highFile = highParent;
	}

	return false;
}

// ����using namespace����
bool ParsingFile::UseUsingNamespace(SourceLocation loc, const NamespaceDecl *beUseNs, bool mustAncestor)
{
	if (m_usingNamespaces.empty())
	{
		return false;
	}

	loc = GetExpasionLoc(loc);
	
	FileID file = GetFileID(loc);
	const std::string beUseNsName = beUseNs->getQualifiedNameAsString();

	SourceLocation bestUsingLoc;
	const NamespaceDecl	*bestNs = nullptr;

	if (mustAncestor)
	{
		// 1. ���Ȳ���ͬ�ļ��ڵ�using namespace����
		auto itr = m_usingNamespacesByFile.find(file);
		if (itr != m_usingNamespacesByFile.end())
		{
			const UsingNamespaceLocMap &nsMap = itr->second;

			for (auto nsItr : nsMap)
			{
				SourceLocation usingLoc = nsItr.first;
				const NamespaceDecl	*ns = nsItr.second;

				if (ns->getQualifiedNameAsString() == beUseNsName && isBeforeInTranslationUnit(usingLoc, loc))
				{
					bestUsingLoc = usingLoc;
					bestNs = ns;
					break;
				}
			}
		}

		// 2. ���򣬲��Һ���ļ���using namespace����
		if (bestNs == nullptr)
		{
			auto includeItr = m_includes.find(GetLowerFileNameInCache(file));
			if (includeItr != m_includes.end())
			{
				const FileSet &includeList = includeItr->second;
				for (FileID beInclude : includeList)
				{
					for (auto nsByFileItr : m_usingNamespacesByFile)
					{
						FileID usingAtFile = nsByFileItr.first;

						if (!IsAncestorByName(usingAtFile, file))
						{
							continue;
						}

						const UsingNamespaceLocMap &nsMap = nsByFileItr.second;

						for (auto nsItr : nsMap)
						{
							SourceLocation usingLoc = nsItr.first;
							const NamespaceDecl	*ns = nsItr.second;

							if (ns->getQualifiedNameAsString() == beUseNsName && isBeforeInTranslationUnit(usingLoc, loc))
							{
								bestUsingLoc = usingLoc;
								bestNs = ns;
								break;
							}
						}
					}
				}
			}
		}
	}
	else
	{
		// ����֮ǰ�ļ���using namespace����
		for (auto itr : m_usingNamespaces)
		{
			SourceLocation usingLoc = itr.first;
			const NamespaceDecl	*ns = itr.second;

			if (ns->getQualifiedNameAsString() == beUseNsName && isBeforeInTranslationUnit(usingLoc, loc))
			{
				bestUsingLoc = usingLoc;
				bestNs = ns;
				break;
			}
		}
	}

	// ���ҳɹ�
	if (bestNs)
	{
		std::string name;
		GetNameForLog(name, bestNs->getQualifiedNameAsString() << "[" << ((Decl*)bestNs)->getDeclKindName() << "]");

		Use(loc, bestUsingLoc, name.c_str());
		return true;
	}

	return false;
}

// ����using����
bool ParsingFile::UseUsing(SourceLocation loc, const NamedDecl *nameDecl, bool mustAncestor)
{
	const UsingVec &usingVec = m_usings;
	if (usingVec.empty())
	{
		return false;
	}

	FileID file = GetFileID(loc);

	// �����ҵ���õ�using����
	const UsingShadowDecl *bestUsingDecl = nullptr;

	const std::string beUsingName = nameDecl->getQualifiedNameAsString();

	if (mustAncestor)
	{
		// 1. ���Ȳ���ͬ�ļ��ڵ�using����
		auto itr = m_usingsByFile.find(file);
		if (itr != m_usingsByFile.end())
		{
			const UsingVec &vecUsingAtFile = itr->second;

			for (const UsingShadowDecl *usingDecl : vecUsingAtFile)
			{
				NamedDecl *usingNameDecl = usingDecl->getTargetDecl();
				SourceLocation usingLoc = usingDecl->getLocation();

				if (usingNameDecl->getQualifiedNameAsString() == beUsingName && isBeforeInTranslationUnit(usingLoc, loc))
				{
					bestUsingDecl = usingDecl;
					break;
				}
			}
		}

		// 2. ���Һ���ļ���using����
		if (nullptr == bestUsingDecl)
		{
			auto includeItr = m_includes.find(GetLowerFileNameInCache(file));
			if (includeItr != m_includes.end())
			{
				const FileSet &includeList = includeItr->second;
				for (FileID beInclude : includeList)
				{
					for (auto usingByFileItr : m_usingsByFile)
					{
						FileID usingAtFile = usingByFileItr.first;

						if (!IsAncestorByName(usingAtFile, file))
						{
							continue;
						}

						const UsingVec &vecUsingAtFile = usingByFileItr.second;

						for (const UsingShadowDecl *usingDecl : vecUsingAtFile)
						{
							NamedDecl *usingNameDecl = usingDecl->getTargetDecl();
							SourceLocation usingLoc = usingDecl->getLocation();

							if (usingNameDecl->getQualifiedNameAsString() == beUsingName && isBeforeInTranslationUnit(usingLoc, loc))
							{
								bestUsingDecl = usingDecl;
								break;
							}
						}
					}
				}
			}
		}
	}
	else
	{
		// ����֮ǰ�ļ���using����
		for (const UsingShadowDecl *usingDecl : usingVec)
		{
			NamedDecl *usingNameDecl = usingDecl->getTargetDecl();
			SourceLocation usingLoc = usingDecl->getLocation();

			if (usingNameDecl->getQualifiedNameAsString() == beUsingName && isBeforeInTranslationUnit(usingLoc, loc))
			{
				bestUsingDecl = usingDecl;
				break;
			}
		}
	}

	// ���ҳɹ�
	if (bestUsingDecl)
	{
		std::string name;
		GetNameForLog(name, "using " << bestUsingDecl->getQualifiedNameAsString() << "[" << bestUsingDecl->getDeclKindName() << "]");

		// ���ø�using
		Use(loc, bestUsingDecl->getLocation(), name.c_str());
		return true;
	}

	return false;
}

// ���������ռ����
void ParsingFile::UseNamespaceAliasDecl(SourceLocation loc, const NamespaceAliasDecl *ns)
{
	UseNameDecl(loc, ns);
}

// �����������ռ�
void ParsingFile::DeclareNamespace(const NamespaceDecl *d)
{
	if (Project::instance.m_logLvl >= LogLvl_2)
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

// using�������ռ䣬���磺using namespace std;
void ParsingFile::UsingNamespace(const UsingDirectiveDecl *d)
{
	const NamespaceDecl *nominatedNs = d->getNominatedNamespace();
	if (nullptr == nominatedNs)
	{
		return;
	}

	SourceLocation usingLoc = GetSpellingLoc(d->getUsingLoc());
	if (usingLoc.isInvalid())
	{
		return;
	}

	FileID atFileID = GetFileID(usingLoc);
	
	const NamespaceDecl *bestNs = nullptr;

	// �����ұ��ļ��ڵ�namespace����
	for (const NamespaceDecl *ns : nominatedNs->redecls())
	{
          SourceLocation nsLoc = GetSpellingLoc(ns->getBeginLoc());
		if (nsLoc.isValid() && isBeforeInTranslationUnit(nsLoc, usingLoc) && m_srcMgr->isWrittenInSameFile(nsLoc, usingLoc))
		{
			bestNs = ns;
			break;
		}
	}

	// �����ҵ�֮ǰ�ļ���namespace����
	if (bestNs == nullptr)
	{
		for (const NamespaceDecl *ns : nominatedNs->redecls())
		{
            SourceLocation nsLoc = GetSpellingLoc(ns->getBeginLoc());
			if (nsLoc.isValid() && isBeforeInTranslationUnit(nsLoc, usingLoc) && IsAncestorByName(GetFileID(nsLoc), atFileID))
			{
				bestNs = ns;
				break;
			}
		}
	}

	if (bestNs == nullptr)
	{
		bestNs = nominatedNs;
	}

	if (bestNs)
	{
		SourceLocation nsLoc = GetSpellingLoc(bestNs->getBeginLoc());

		std::string name;
		GetNameForLog(name, bestNs->getQualifiedNameAsString());

		// ���������ռ����ڵ��ļ���ע�⣺using namespaceʱ�������ҵ���Ӧ��namespace���������磬using namespace Aǰһ��Ҫ��namespace A{}�������ᱨ��
		Use(usingLoc, nsLoc, name.c_str());

		m_usingNamespaces[usingLoc] = bestNs;
		m_usingNamespacesByFile[GetFileID(usingLoc)][usingLoc] = bestNs;
	}
}

// using�������ռ��µ�ĳ�࣬���磺using std::string;
void ParsingFile::UsingXXX(const UsingDecl *d)
{
	SourceLocation usingLoc = d->getUsingLoc();

	for (const UsingShadowDecl *shadowDecl : d->shadows())
	{
		NamedDecl *nameDecl = shadowDecl->getTargetDecl();
		if (nullptr == nameDecl)
		{
			continue;
		}

		m_usings.push_back(shadowDecl);
		m_usingsByFile[GetFileID(usingLoc)].push_back(shadowDecl);

		std::string name;
		GetNameForLog(name, "using " << shadowDecl->getQualifiedNameAsString() << "[" << nameDecl->getQualifiedNameAsString() << "]" << "[" << nameDecl->getDeclKindName() << "]");

		Use(usingLoc, nameDecl->getEndLoc(), name.c_str());
	}
}

// ��ȡ�����ռ��ȫ��·�������磬����namespace A{ namespace B{ class C; }}
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
		}
		else
		{
			break;
		}
	}

	return name;
}

// ��aʹ��bʱ���跨�ҵ�һ��������a���Ϲ�ϵ��b���ⲿ����
FileID ParsingFile::GetBestAncestor(FileID a, FileID b) const
{
	if (!IsOuterFile(b))
	{
		return b;
	}

	if (GetOuterFileAncestor(b) == b)
	{
		return b;
	}

	if (IsAncestorByName(b, a))
	{
		FileID search = a;

		FileSet done;

		// �ں����ļ����ҳ�b������
		auto SearchInKid = [&](FileID now, FileID b)
		{
			auto itr = m_includes.find(GetLowerFileNameInCache(now));
			if (itr == m_includes.end())
			{
				return FileID();
			}

			const FileSet &includes = itr->second;
			for (FileID beInclude : includes)
			{
				bool isDone = false;
				for (FileID doneFile : done)
				{
					if (IsSameName(doneFile, beInclude))
					{
						isDone = true;
						break;
					}
				}

				if (isDone)
				{
					continue;
				}

				if (IsAncestorByName(b, beInclude) || IsSameName(b, beInclude))
				{
					return beInclude;
				}
			}

			return FileID();
		};

		while (search.isValid())
		{
			FileID kid = SearchInKid(search, b);
			if (kid.isInvalid())
			{
				break;
			}

			search = kid;
			done.insert(search);

			if (IsOuterFile(search))
			{
				break;
			}
		}

		if (search == a)
		{
			search = b;
		}

		return search;
	}
	else
	{
		return GetOuterFileAncestor(b);
	}
}

// ��ǰλ��ʹ��Ŀ�����ͣ�ע��Type����ĳ�����ͣ�������const��volatile��static�ȵ����Σ�
void ParsingFile::UseType(SourceLocation loc, const Type *t, const NestedNameSpecifier *specifier /* = nullptr */)
{
	if (nullptr == t || loc.isInvalid())
	{
		return;
	}

	// ʹ�õ�typedef���ͣ����磺typedef int dword����dword����TypedefType
	if (isa<TypedefType>(t))
	{
		const TypedefType *typedefType = cast<TypedefType>(t);
		const TypedefNameDecl *typedefNameDecl = typedefType->getDecl();

		// ע������typedef��ԭ����Ȼ����������typedef�������ɣ�����Ҫ�����ֽ�
		if (typedefNameDecl)
		{
			SearchUsingAny(loc, specifier, typedefNameDecl->getUnderlyingDecl());
			UseNameDecl(loc, typedefNameDecl);
			//UseVarType(loc, typedefNameDecl->getUnderlyingType(), specifier);
		}
	}
	// ĳ�����͵Ĵ��ţ��磺struct S��N::M::type
	else if (isa<ElaboratedType>(t))
	{
		const ElaboratedType *elaboratedType = cast<ElaboratedType>(t);
		UseQualType(loc, elaboratedType->getNamedType(), elaboratedType->getQualifier());

		// Ƕ����������
		// UseQualifier(loc, elaboratedType->getQualifier());
	}
	else if (isa<TemplateSpecializationType>(t))
	{
		const TemplateSpecializationType *templateType = cast<TemplateSpecializationType>(t);
		//templateType->dump();
		UseTemplateDecl(loc, templateType->getTemplateName().getAsTemplateDecl());

		SearchUsingAny(loc, specifier, templateType->getTemplateName().getAsTemplateDecl());

		//templateType->getLocallyUnqualifiedSingleStepDesugaredType().dump();
		for (int i = 0, size = templateType->getNumArgs(); i < size; ++i)
		{
			const TemplateArgument &arg = templateType->getArg((unsigned)i);
			UseTemplateArgument(loc, arg);
		}
	}
	// ����ȡ�����ģ�����Ͳ���
	else if (isa<SubstTemplateTypeParmType>(t))
	{
		const SubstTemplateTypeParmType *substTemplateTypeParmType = cast<SubstTemplateTypeParmType>(t);
		UseType(loc, substTemplateTypeParmType->getReplacedParameter(), specifier);
	}
	else if (isa<AttributedType>(t))
	{
		const AttributedType *attributedType = cast<AttributedType>(t);
		UseQualType(loc, attributedType->getModifiedType(), specifier);
	}
	else if (isa<FunctionType>(t))
	{
		const FunctionType *functionType = cast<FunctionType>(t);

		// ʶ�𷵻�ֵ����
		QualType returnType = functionType->getReturnType();
		UseQualType(loc, returnType, specifier);
	}
	else if (isa<MemberPointerType>(t))
	{
		const MemberPointerType *memberPointerType = cast<MemberPointerType>(t);
		UseQualType(loc, memberPointerType->getPointeeType(), specifier);
	}
	// ģ��������ͣ����磺template <typename T>�����T
	else if (isa<TemplateTypeParmType>(t))
	{
		const TemplateTypeParmType *templateTypeParmType = cast<TemplateTypeParmType>(t);

		TemplateTypeParmDecl *decl = templateTypeParmType->getDecl();
		if (nullptr == decl)
		{
			return;
		}

		// ��ģ�������Ĭ�ϲ���
		if (decl->hasDefaultArgument())
		{
			UseQualType(loc, decl->getDefaultArgument(), specifier);
		}

		UseNameDecl(loc, decl);
	}
	else if (isa<ParenType>(t))
	{
		const ParenType *parenType = cast<ParenType>(t);
		UseQualType(loc, parenType->getInnerType(), specifier);
	}
	else if (isa<InjectedClassNameType>(t))
	{
		const InjectedClassNameType *injectedClassNameType = cast<InjectedClassNameType>(t);
		UseQualType(loc, injectedClassNameType->getInjectedSpecializationType(), specifier);
	}
	else if (isa<PackExpansionType>(t))
	{
		const PackExpansionType *packExpansionType = cast<PackExpansionType>(t);
		UseQualType(loc, packExpansionType->getPattern(), specifier);
	}
	else if (isa<DecltypeType>(t))
	{
		const DecltypeType *decltypeType = cast<DecltypeType>(t);
		UseQualType(loc, decltypeType->getUnderlyingType(), specifier);
	}
	// �ࡢstruct��union
	else if (isa<RecordType>(t))
	{
		const RecordType *recordType = cast<RecordType>(t);

		const RecordDecl *recordDecl = recordType->getDecl();
		if (nullptr == recordDecl)
		{
			return;
		}

		SearchUsingAny(loc, specifier, recordDecl);
		UseRecord(loc, recordDecl);
	}
	else if (t->isArrayType())
	{
		const ArrayType *arrayType = cast<ArrayType>(t);
		UseQualType(loc, arrayType->getElementType(), specifier);
	}
	else if (t->isVectorType())
	{
		const VectorType *vectorType = cast<VectorType>(t);
		UseQualType(loc, vectorType->getElementType(), specifier);
	}
	else if (t->isPointerType() || t->isReferenceType())
	{
		QualType pointeeType = t->getPointeeType();

		// �����ָ�����;ͻ�ȡ��ָ������(PointeeType)
		while (pointeeType->isPointerType() || pointeeType->isReferenceType())
		{
			pointeeType = pointeeType->getPointeeType();
		}

		UseQualType(loc, pointeeType, specifier);
	}
	else if (t->isEnumeralType())
	{
		TagDecl *decl = t->getAsTagDecl();
		if (nullptr == decl)
		{
			return;
		}

		SearchUsingAny(loc, specifier, decl);
		UseNameDecl(loc, decl);
	}
	/*
	else if (isa<UnaryTransformType>(t))
	{
		// t->dump();
	}
	else if (t->isBuiltinType())
	{
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
	else
	{
		LogInfo(""-------------- haven't support type --------------");
		t->dump();
	}
	*/
}

// ��ǰλ��ʹ��Ŀ�����ͣ�ע��QualType������ĳ�����͵�const��volatile��static�ȵ����Σ�
void ParsingFile::UseQualType(SourceLocation loc, const QualType &t, const NestedNameSpecifier *specifier)
{
	if (t.isNull())
	{
		return;
	}


	const Type *pType = t.getTypePtr();
	UseType(loc, pType, specifier);
}

// ��ȡc++��class��struct��union��ȫ������������������ռ�
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

// ����ʹ��ǰ��������¼�����ڲ���Ҫ��ӵ�ǰ����������֮���������
inline void ParsingFile::UseForward(SourceLocation loc, const CXXRecordDecl *cxxRecord, const NestedNameSpecifier *specifier)
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

	bool isOpenForward = false;
    if (isOpenForward)
	{
		// ����ļ���ʹ�õ�ǰ��������¼
		m_fileUseRecordPointers[by].insert(cxxRecord);

		SearchUsingAny(loc, specifier, cxxRecord);
		UseQualifier(loc, cxxRecord->getQualifier());

		if (Project::instance.m_logLvl >= LogLvl_2)
		{
			m_locUseRecordPointers[loc].insert(cxxRecord);
		}  
	}
	else
	{
		SearchUsingAny(loc, specifier, cxxRecord);
		UseQualifier(loc, cxxRecord->getQualifier());

		if (Project::instance.m_logLvl >= LogLvl_2)
		{
			m_locUseRecordPointers[loc].insert(cxxRecord);
		}
    }
}

// �Ƿ�Ϊ��ǰ������������
bool ParsingFile::IsForwardType(const QualType &var)
{
	return false;

	if (!var->isPointerType() && !var->isReferenceType())
	{
		return false;
	}

	// ȥ��ָ�룬��ȡ��������ָ�������
	QualType pointeeType = GetPointeeType(var);

	if (!isa<RecordType>(pointeeType))
	{
		return false;
	}

	const CXXRecordDecl *cxxRecordDecl = pointeeType->getAsCXXRecordDecl();
	if (nullptr == cxxRecordDecl)
	{
		return false;
	}

	// ģ���ػ�
	if (isa<ClassTemplateSpecializationDecl>(cxxRecordDecl))
	{
		return false;
	}

	if (!IsAllQualifierNamespace(cxxRecordDecl->getQualifier()))
	{
		return false;
	}

	return true;
}

// �Ƿ����е��޶������������ռ䣨����::std::vector<int>::�е�vector�Ͳ��������ռ䣩
bool ParsingFile::IsAllQualifierNamespace(const NestedNameSpecifier *specifier)
{
	while (specifier)
	{
		NestedNameSpecifier::SpecifierKind kind = specifier->getKind();
		if (kind != NestedNameSpecifier::Namespace)
		{
			return false;
		}

		specifier = specifier->getPrefix();
	}

	return true;
}

// ȥ��ָ�룬��ȡ��������ָ�������
QualType ParsingFile::GetPointeeType(const QualType &var)
{
	QualType pointeeType = var->getPointeeType();
	while (pointeeType->isPointerType() || pointeeType->isReferenceType())
	{
		pointeeType = pointeeType->getPointeeType();
	}

	return pointeeType;
}

// ����ʹ�ñ�����¼
void ParsingFile::UseVarType(SourceLocation loc, const QualType &var, const NestedNameSpecifier *specifier)
{
	// �������Ϳ�ǰ������
	if (IsForwardType(var) )
	{		
		QualType pointeeType = GetPointeeType(var); 
		const CXXRecordDecl *cxxRecordDecl = pointeeType->getAsCXXRecordDecl();

		// �ⲿ�ļ��в�����ǰ������
		if (IsOuterFile(GetFileID(loc)))
		{
			FileID at = GetFileID(loc);

			for (const TagDecl *redecl : cxxRecordDecl->redecls())
			{
				SourceLocation recordLoc = redecl->getBeginLoc();
				FileID recordFile = GetFileID(recordLoc);

				if (isBeforeInTranslationUnit(recordLoc, loc))
				{
					if (IsAncestorByName(recordFile, at) || IsSameName(recordFile, at))
					{
						return;
					}
				}
			}

			UseRecord(loc, cxxRecordDecl);
		}
		else
		{
			UseForward(loc, cxxRecordDecl, specifier);
		}
	}
	else
	{
		//var->dump();
		UseQualType(loc, var, specifier);
	}
}

// ���ù��캯��
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

// ���ñ�������
void ParsingFile::UseVarDecl(SourceLocation loc, const VarDecl *var, const NestedNameSpecifier *specifier)
{
	if (nullptr == var)
	{
		return;
	}

	UseValueDecl(loc, var, specifier);
}

// ���ã�����������Ϊ��ֵ����������ʶ��enum����
void ParsingFile::UseValueDecl(SourceLocation loc, const ValueDecl *valueDecl, const NestedNameSpecifier *specifier)
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
		std::string name;
		GetNameForLog(name, valueDecl->getQualifiedNameAsString() << "[" << valueDecl->getDeclKindName() << "]");

		Use(loc, valueDecl->getEndLoc(), name.c_str());
	}

	UseVarType(loc, valueDecl->getType(), specifier);
}

// ���ô������Ƶ�����
void ParsingFile::UseNameDecl(SourceLocation loc, const NamedDecl *nameDecl)
{
	if (nullptr == nameDecl)
	{
		return;
	}

	std::string name;
	GetNameForLog(name, nameDecl->getQualifiedNameAsString() << "[" << nameDecl->getDeclKindName() << "]");

	Use(loc, nameDecl->getEndLoc(), name.c_str());
}

// ����ʹ�ú���������¼
void ParsingFile::UseFuncDecl(SourceLocation loc, const FunctionDecl *f)
{
	if (nullptr == f)
	{
		return;
	}

	FileID file = GetFileID(loc);

	// ��ʱ����ֺ����ظ���������ʱ�������ú���ļ��еĺ���
	for (const FunctionDecl *redeclFunc : f->redecls())
	{
		if (IsAncestorByName(GetFileID(redeclFunc->getLocation()), file) && isBeforeInTranslationUnit(redeclFunc->getLocation(), loc))
		{
			f = redeclFunc;
			break;
		}
	}

	// Ƕ����������
	UseQualifier(loc, f->getQualifier());

	// �����ķ���ֵ
	QualType returnType = f->getReturnType();
	UseVarType(loc, returnType);

	// ���α����������������ù�ϵ
	for (FunctionDecl::param_const_iterator itr = f->param_begin(), end = f->param_end(); itr != end; ++itr)
	{
		ParmVarDecl *vardecl = *itr;
		//vardecl->dump();

		// UseQualType(loc, vardecl->getType(), vardecl->getQualifier());
		UseVarDecl(loc, vardecl, vardecl->getQualifier());
	}

	if (f->getTemplateSpecializationArgs())
	{
		const TemplateArgumentList *args = f->getTemplateSpecializationArgs();
		UseTemplateArgumentList(loc, args);
	}

	std::string name;
	GetNameForLog(name, f->getQualifiedNameAsString() << "[" << f->clang::Decl::getDeclKindName() << "]");

	Use(loc, f->getTypeSpecStartLoc(), name.c_str());

	// ����������ģ���й�
	FunctionDecl::TemplatedKind templatedKind = f->getTemplatedKind();
	if (templatedKind != FunctionDecl::TK_NonTemplate)
	{
		// [����ģ�崦] ���� [ģ�嶨�崦]
		Use(f->getBeginLoc(), f->getLocation(), name.c_str());
	}
}

// ����ģ�����
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
		Use(loc, arg.getAsExpr()->getBeginLoc(), "[ParsingFile::UseTemplateArgument][case TemplateArgument::Expression]");
		break;

	case TemplateArgument::Template:
		UseNameDecl(loc, arg.getAsTemplate().getAsTemplateDecl());
		break;

	default:
		break;
	}
}

// ����ģ������б�
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

// ����ģ�嶨��
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

// ����ʹ��class��struct��union��¼
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

	if (isa<CXXRecordDecl>(record))
	{
		const CXXRecordDecl *cxxRecord = cast<CXXRecordDecl>(record);
		m_fileUseRecords[by].insert(cxxRecord);
	}

	std::string name;
	GetNameForLog(name, GetRecordName(*record) << "[" << ((Decl*)record)->getDeclKindName() << "]");

	Use(loc, record->getBeginLoc(), name.c_str());
}

// �Ƿ�Ϊϵͳͷ�ļ�������<vector>��<iostream>�Ⱦ���ϵͳ�ļ�
inline bool ParsingFile::IsSystemHeader(FileID file) const
{
	SourceLocation fileBeginLoc = m_srcMgr->getLocForStartOfFile(file);
	return IsInSystemHeader(fileBeginLoc);
}

// ָ��λ���Ƿ���ϵͳͷ�ļ��ڣ�����<vector>��<iostream>�Ⱦ���ϵͳ�ļ���
bool ParsingFile::IsInSystemHeader(SourceLocation loc) const
{
	return m_srcMgr->isInSystemHeader(loc);
}

// �Ƿ����������c++�ļ������������������ļ����ݲ������κα仯��
inline bool ParsingFile::CanClean(FileID file) const
{
	return CanCleanByName(GetLowerFileNameInCache(file));
}

inline bool ParsingFile::CanCleanByName(const char *fileName) const
{
	return Project::CanClean(fileName);
}

// ��ӡ���ļ��ĸ��ļ�
void ParsingFile::PrintParent()
{
	HtmlDiv &div = HtmlLog::instance->m_newDiv;
	div.AddRow(AddPrintIdx() + ". list of parent id: has parent file count = " + get_number_html(m_parents.size()), 1);

	for (auto &itr : m_parents)
	{
		FileID child	= itr.first;
		FileID parent	= itr.second;

		// ����ӡ����Ŀ���ļ���ֱ�ӹ������ļ�
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

// ��ӡÿ���ļ���ԭʼ����ļ���¼
void ParsingFile::PrintKidsByName()
{
	HtmlDiv &div = HtmlLog::instance->m_newDiv;
	div.AddRow(AddPrintIdx() + ". list of kids by same name : file count = " + strtool::itoa(m_kidsByName.size()), 1);

	for (auto &itr : m_kidsByName)
	{
		const std::string &parent = itr.first;
		if (!IsNeedPrintFile(GetFileIDByFileName(parent.c_str())))
		{
			continue;
		}

		const FileNameSet &kids = itr.second;

		div.AddRow("file = " + get_file_html(parent.c_str()) + ", kid num = " + get_number_html(kids.size()), 2);

		for (const std::string &kid : kids)
		{
			div.AddRow("kid by same = " + get_file_html(kid.c_str()), 3);
		}

		div.AddRow("");
	}
}

// ��ȡ���ļ��ı�������Ϣ���������ݰ��������ļ��������ļ����������ļ�#include���кš������ļ�#include��ԭʼ�ı���
string ParsingFile::DebugBeIncludeText(FileID file) const
{
	const char *fileName = GetFileNameInCache(file);
	if (file == m_root)
	{
		return strtool::get_text(cn_main_file_debug_text, get_file_html(fileName).c_str(),
		                         file.getHashValue(), get_number_html(GetDeepth(file)).c_str());
	}
	else
	{
		stringstream ancestors;

		for (FileID parent = GetParent(file), child = file; parent.isValid();)
		{
			string includeLineText = strtool::get_text(cn_file_include_line, get_short_file_name_html(GetFileNameInCache(parent)).c_str(),
			                         strtool::itoa(GetIncludeLineNo(child)).c_str(), get_include_html(GetBeIncludeLineText(child)).c_str());

			ancestors << includeLineText;
			child = parent;

			parent = GetParent(parent);
			if (parent.isValid())
			{
				ancestors << "<-";
			}
		}

		const char *str_file_type = "";
		if (IsSystemHeader(file))
		{
			str_file_type = cn_system_file_type;
		}
		else if (IsOuterFile(file))
		{
			str_file_type = cn_outer_file_type;
		}

		return strtool::get_text(cn_file_debug_text, str_file_type, get_file_html(fileName).c_str(),
		                         file.getHashValue(), get_number_html(GetDeepth(file)).c_str(), ancestors.str().c_str());
	}

	return "";
}

// ��Ѱ�����using namespace����
bool ParsingFile::SearchUsingNamespace(SourceLocation loc, const NestedNameSpecifier *specifier, const DeclContext *context, bool mustAncestor)
{
	while (specifier && context)
	{
		specifier = specifier->getPrefix();
		context = context->getParent();
	}

	if (context && context->isNamespace())
	{
		const NamespaceDecl *namespaceDecl = cast<NamespaceDecl>(context);
		return UseUsingNamespace(loc, namespaceDecl, mustAncestor);
	}

	return false;
}

// ��Ѱ�����using����
bool ParsingFile::SearchUsingXXX(SourceLocation loc, const NestedNameSpecifier *specifier, const NamedDecl *nameDecl, bool mustAncestor)
{
	const DeclContext *context = nameDecl->getDeclContext();
	while (specifier && context)
	{
		specifier = specifier->getPrefix();
		context = context->getParent();
	}

	if (context && !context->isTranslationUnit())
	{
		return UseUsing(loc, nameDecl, mustAncestor);
	}

	return false;
}

// ��Ѱ�����using namespace��using����
void ParsingFile::SearchUsingAny(SourceLocation loc, const NestedNameSpecifier *specifier, const NamedDecl *nameDecl)
{
	if (nullptr == nameDecl)
	{
		return;
	}

	bool mustAncestor = true;

	if (SearchUsingXXX(loc, specifier, nameDecl, mustAncestor))
	{
		return;
	}

	SearchUsingNamespace(loc, specifier, nameDecl->getDeclContext(), mustAncestor);
}

// ��ȡ�ļ���Ϣ
std::string ParsingFile::DebugParentFileText(FileID file, int n) const
{
	return strtool::get_text(cn_parent_file_debug_text, DebugBeIncludeText(file).c_str(), get_number_html(n).c_str());
}

// ��ȡ��λ�������е���Ϣ�������е��ı��������ļ������к�
string ParsingFile::DebugLocText(SourceLocation loc) const
{
	string lineText = GetSourceOfLine(loc);
	std::stringstream text;
	text << "[" << get_include_html(lineText) << "] in [";
	text << get_file_html(GetFileNameInCache(GetFileID(loc)));
	text << "] line = " << get_number_html(GetLineNo(loc)) << " col = " << get_number_html(m_srcMgr->getSpellingColumnNumber(loc));
	return text.str();
}

// ��ȡ�ļ�����ͨ��clang��ӿڣ��ļ���δ�������������Ǿ���·����Ҳ���������·����
// ���磺���ܷ��أ�d:/hello.h��Ҳ���ܷ��أ�./hello.h
inline const char* ParsingFile::GetFileName(FileID file) const
{
	const FileEntry *fileEntry = m_srcMgr->getFileEntryForID(file);
	return fileEntry ? fileEntry->getName().data() : "";
}

// ��ȡƴдλ��
inline SourceLocation ParsingFile::GetSpellingLoc(SourceLocation loc) const
{
	return m_srcMgr->getSpellingLoc(loc);
}

// ��ȡ��������չ���λ��
inline SourceLocation ParsingFile::GetExpasionLoc(SourceLocation loc) const
{
	return m_srcMgr->getExpansionLoc(loc);
}

// ��ȡ�ļ�ID
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

// ��ȡ�ļ��ľ���·��
inline string ParsingFile::GetAbsoluteFileName(FileID file) const
{
	const char* raw_file_name = GetFileName(file);
	return pathtool::get_absolute_path(raw_file_name);
}

// ��ȡ�ļ��ľ���·��
inline const char* ParsingFile::GetFileNameInCache(FileID file) const
{
	auto itr = m_fileNames.find(file);
	return itr != m_fileNames.end() ? itr->second.c_str() : "";
}

// ��ȡ�ļ���Сд����·��
inline const char* ParsingFile::GetLowerFileNameInCache(FileID file) const
{
	auto itr = m_lowerFileNames.find(file);
	return itr != m_lowerFileNames.end() ? itr->second.c_str() : "";
}

// ���ڵ��ԣ���ȡ�ļ��ľ���·���������Ϣ
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

// ��ȡ��1���ļ�#include��2���ļ����ı���
std::string ParsingFile::GetRelativeIncludeStr(FileID f1, FileID f2) const
{
	// 1. ����2���ļ��ı�������ԭ������#include <xxx>�ĸ�ʽ���򷵻�ԭ����#include��
	std::string rawInclude2 = GetBeIncludeLineText(f2);
	if (rawInclude2.empty())
	{
		return "";
	}

	trim(rawInclude2);

	// �Ƿ񱻼����Ű������磺<stdio.h>
	bool isAngled = strtool::contain(rawInclude2.c_str(), '<');
	if (isAngled)
	{
		return rawInclude2;
	}

	std::string absolutePath1 = GetFileNameInCache(f1);
	std::string absolutePath2 = GetFileNameInCache(f2);

	std::string include2;

	// 2. �����ж�2���ļ��Ƿ���ͬһ�ļ�����
	if (tolower(get_dir(absolutePath1)) == tolower(get_dir(absolutePath2)))
	{
		include2 = "\"" + strip_dir(absolutePath2) + "\"";
	}
	else
	{
		// 3. ��ͷ�ļ�����·������Ѱ��2���ļ������ɹ��ҵ����򷵻ػ���ͷ�ļ�����·�������·��
		include2 = GetQuotedIncludeStr(absolutePath2.c_str());

		// ��δ��ͷ�ļ�����·����Ѱ����2���ļ����򷵻ػ��ڵ�1���ļ������·��
		if (include2.empty())
		{
			include2 = "\"" + pathtool::get_relative_path(absolutePath1.c_str(), absolutePath2.c_str()) + "\"";
		}
	}

	return "#include " + include2;
}

// ��ʼ�����ļ������Ķ�c++Դ�ļ���
void ParsingFile::Clean()
{
	// �Ƿ񸲸�c++Դ�ļ�
	if (!Project::instance.m_isOverWrite)
	{
		return;
	}

	LogInfoByLvl(LogLvl_3, "------ Clean ------");

	// ��������c++�ļ�
	CleanByHistory(ProjectHistory::instance.m_files);

	// ���䶯��д��c++�ļ�
	Overwrite();
}

// ����������д��c++Դ�ļ������أ�true��д�ɹ���false��дʧ��
bool ParsingFile::Overwrite()
{
	class CxxCleanReWriter
	{
	public:
		// ���ļ���ӿ�дȨ��
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

		// �����ļ�
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

	if (!isAllOk)
	{
		LogError("overwrite some changed files failed!");
	}

	return isAllOk;
}

// �滻ָ����Χ�ı�
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

// ���ı����뵽ָ��λ��֮ǰ
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

// �Ƴ�ָ����Χ�ı�
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
	rewriteOption.RemoveLineIfEmpty				= false;	// ���Ƴ��ı�����б�Ϊ���У������ÿ���һ���Ƴ�

	LogInfoByLvl(LogLvl_2, "remove [" << GetFileNameInCache(file) << "]: [" << beg << "," << end << "], text = [" << GetSourceOfRange(range) << "]");

	bool err = m_rewriter.RemoveText(range.getBegin(), end - beg, rewriteOption);
	if (err)
	{
		LogError("remove [" << GetDebugFileName(file) << "]: [" << beg << "," << end << "], text = [" << GetSourceOfRange(range) << "] failed");
	}
}

// �Ƴ�ָ���ļ��ڵ�������
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

// ��ָ���ļ������ǰ������
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

// ��ָ���ļ����滻#include
void ParsingFile::CleanByReplace(const FileHistory &history, FileID file)
{
	const char *newLineWord = history.GetNewLineWord();

	for (auto &itr : history.m_replaces)
	{
		int line = itr.first;
		const ReplaceLine &replaceLine	= itr.second;

		// ���Ǳ�-include����ǿ�����룬����������Ϊ�滻��û��Ч��
		if (replaceLine.isSkip || line <= 0)
		{
			continue;
		}

		// �滻#include
		std::stringstream text;

		const ReplaceTo &replaceInfo = replaceLine.replaceTo;
		text << replaceInfo.newText;
		text << newLineWord;

		ReplaceText(file, replaceLine.beg, replaceLine.end, text.str().c_str());
	}
}

// ��ָ���ļ���������
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

// ������ʷ����ָ���ļ�
void ParsingFile::CleanByHistory(const FileHistoryMap &historys)
{
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

		if (history.m_isSkip || history.HaveFatalError())
		{
			continue;
		}

		// ���������ڵ�ǰcpp���ļ����ҵ���Ӧ���ļ�ID��ע�⣺ͬһ���ļ����ܱ�������Σ�FileID�ǲ�һ���ģ�����ȡ����������С��FileID��
		FileID file	= GetFileIDByFileName(fileName.c_str());
		if (file.isInvalid())
		{
			continue;
		}

		if (IsOuterFile(file))
		{
			continue;
		}

		CleanByReplace(history, file);
		CleanByForward(history, file);
		CleanByDelLine(history, file);
		CleanByAdd(history, file);

		ProjectHistory::instance.OnCleaned(fileName);
	}
}

// ��ӡͷ�ļ�����·��
void ParsingFile::PrintHeaderSearchPath() const
{
	if (m_headerSearchPaths.empty())
	{
		return;
	}

	HtmlDiv &div = HtmlLog::instance->m_newDiv;
	div.AddRow(AddPrintIdx() + ". header search path list : path count = " + get_number_html(m_headerSearchPaths.size()), 1);

	for (const HeaderSearchDir &path : m_headerSearchPaths)
	{
		div.AddRow("search path = " + get_file_html(path.m_dir.c_str()), 2);
	}

	div.AddRow("");
}

// ���ڵ��ԣ���ӡ���ļ����¼�����#include�ı�
void ParsingFile::PrintRelativeInclude() const
{
	HtmlDiv &div = HtmlLog::instance->m_newDiv;
	div.AddRow(AddPrintIdx() + ". relative include list : use = " + get_number_html(m_uses.size()), 1);

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
			div.AddRow("old include = " + get_include_html(GetBeIncludeLineText(be_used_file)), 3, 45);
			div.AddGrid("-> relative include = " + get_include_html(GetRelativeIncludeStr(file, be_used_file)));
		}

		div.AddRow("");
	}
}

// ��ӡ���� + 1
std::string ParsingFile::AddPrintIdx() const
{
	return strtool::itoa(++m_printIdx);
}

// ��ӡ��Ϣ
void ParsingFile::Print()
{
	LogLvl verbose = Project::instance.m_logLvl;
	if (verbose <= LogLvl_0)
	{
		return;
	}

	HtmlDiv &div = HtmlLog::instance->m_newDiv;

	div.AddTitle(strtool::get_text(cn_file_history_title,
	                               get_number_html(ProjectHistory::instance.g_fileNum).c_str(),
	                               get_number_html(Project::instance.m_cpps.size()).c_str(),
	                               get_file_html(GetFileNameInCache(m_root)).c_str()));

	m_printIdx = 0;

	if (verbose >= LogLvl_1)
	{
		PrintHistory();
	}

	if (verbose >= LogLvl_2)
	{
		PrintMinInclude();
		PrintMinKid();
		PrintUserUse();
		PrintForwardClass();

		PrintUse();
		PrintUseName();
		PrintSameFile();
	}

	if (verbose >= LogLvl_3)
	{
		PrintOutFileAncestor();
		PrintKidsByName();
		PrintUseRecord();

		PrintAllFile();
		PrintInclude();
		PrintHeaderSearchPath();
		PrintRelativeInclude();
		PrintParent();
		PrintNamespace();
		PrintUsingNamespace();
		PrintUsingXXX();
	}

	HtmlLog::instance->AddDiv(div);
}

// ����ǰcpp�ļ������Ĵ������¼��֮ǰ����cpp�ļ������Ĵ������¼�ϲ�
void ParsingFile::MergeTo(FileHistoryMap &oldFiles) const
{
	const FileHistoryMap &newFiles = m_historys;

	// �����ļ������ر������������������࣬����ϲ����������ʷ�����ڴ�ӡ
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

		auto findItr = oldFiles.find(fileName);

		bool found = (findItr != oldFiles.end());
		if (!found)
		{
			oldFiles[fileName] = newFile;
		}
	}
}

// �ļ���ʽ�Ƿ���windows��ʽ�����з�Ϊ[\r\n]����Unix��Ϊ[\n]
bool ParsingFile::IsWindowsFormat(FileID file) const
{
	SourceLocation fileStart = m_srcMgr->getLocForStartOfFile(file);
	if (fileStart.isInvalid())
	{
		return false;
	}

	// ��ȡ��һ�����ķ�Χ
	SourceRange firstLine		= GetCurLine(fileStart);
	SourceLocation firstLineEnd	= firstLine.getEnd();

	{
		const char* c1	= GetSourceAtLoc(firstLineEnd);
		const char* c2	= GetSourceAtLoc(firstLineEnd.getLocWithOffset(1));

		// ˵����һ��û�л��з������ߵ�һ�����Ľ�����ֻ����һ��\r��\n�ַ�
		if (nullptr == c1 || nullptr == c2)
		{
			return false;
		}

		// windows���и�ʽ
		if (*c1 == '\r' && *c2 == '\n')
		{
			return true;
		}
		// ��Unix���и�ʽ
		else if (*c1 == '\n')
		{
			return false;
		}
	}

	return false;
}

// ȡ�������ļ��Ŀ�ɾ��#include��
void ParsingFile::TakeDel(FileHistory &history, const FileSet &dels) const
{
	for (FileID del : dels)
	{
		int line = GetIncludeLineNo(del);
		if (line <= 0)
		{
			continue;
		}

		SourceRange lineRange = GetIncludeRange(del);

		DelLine &delLine = history.m_delLines[line];

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

// ȡ��ͷ�ļ��滻��Ϣ
void ParsingFile::TakeReplaceLine(ReplaceLine &replaceLine, FileID from, FileID to) const
{
	// 1. ���еľ��ı�
	SourceRange	includeRange	= GetIncludeRange(from);
	replaceLine.oldFile			= GetLowerFileNameInCache(from);
	replaceLine.oldText			= GetBeIncludeLineText(from);
	replaceLine.beg				= m_srcMgr->getFileOffset(includeRange.getBegin());
	replaceLine.end				= m_srcMgr->getFileOffset(includeRange.getEnd());
	replaceLine.isSkip			= Has(m_defaultIncludes, from);	// �����Ƿ���Ĭ�ϱ�����

	// 2. ���пɱ��滻��ʲô
	ReplaceTo &replaceTo	= replaceLine.replaceTo;

	// ��¼[��#include����#include]
	replaceTo.oldText		= GetBeIncludeLineText(to);
	replaceTo.newText		= GetRelativeIncludeStr(GetParent(from), to);

	// ��¼[�������ļ��������к�]
	replaceTo.line			= GetIncludeLineNo(to);
	replaceTo.fileName		= GetFileNameInCache(to);
	replaceTo.inFile		= GetFileNameInCache(GetParent(to));
}

// ȡ������ǰ��������Ϣ
void ParsingFile::TakeForwardClass(FileHistory &history, FileID insertAfter, FileID top) const
{
	auto useRecordItr = m_fowardClass.find(top);
	if (useRecordItr == m_fowardClass.end())
	{
		return;
	}

	SourceRange includeRange = GetIncludeRange(insertAfter);
	SourceLocation insertLoc = includeRange.getEnd();
	if (insertLoc.isInvalid())
	{
		LogErrorByLvl(LogLvl_2, "insertLoc.isInvalid(): " << GetDebugFileName(top));
		return;
	}

	int line = GetLineNo(insertLoc);

	ForwardLine &forwardLine = history.m_forwards[line];
	forwardLine.offset = m_srcMgr->getFileOffset(insertLoc);
	forwardLine.oldText = GetSourceOfLine(includeRange.getBegin());

	// ��ʼȡ��ǰ��������Ϣ
	const RecordSet &cxxRecords = useRecordItr->second;
	for (const CXXRecordDecl *cxxRecord : cxxRecords)
	{
		forwardLine.classes.insert(GetRecordName(*cxxRecord));
	}
}

// ȡ�����������ļ���Ϣ
void ParsingFile::TakeAdd(FileHistory &history, FileID top, const std::map<FileID, FileVec> &inserts) const
{	
	for (const auto &itr : inserts)
	{
		FileID insertAfter = itr.first;
		const FileVec &adds = itr.second;

		int line = GetIncludeLineNo(insertAfter);
		
		AddLine &addLine = history.m_adds[line];
		if (addLine.offset <= 0)
		{
			addLine.offset = m_srcMgr->getFileOffset(GetIncludeRange(insertAfter).getEnd());
			addLine.oldText = GetBeIncludeLineText(insertAfter);
		}

		for (FileID add : adds)
		{
			BeAdd beAdd;
			beAdd.fileName = GetFileNameInCache(add);
			beAdd.text = GetRelativeIncludeStr(top, add);

			addLine.adds.push_back(beAdd);
		}
	}
}

// ������һ�������
FileID ParsingFile::GetSecondAncestor(FileID top, FileID child) const
{
	auto includeItr = m_includes.find(GetLowerFileNameInCache(top));
	if (includeItr == m_includes.end())
	{
		return FileID();
	}

	return FileID();
}

// ���°������ļ��������򣬼����ÿ���ļ�Ӧ�����λ��
void ParsingFile::SortAddFiles(FileID top, FileSet &adds, FileSet &keeps, FileSet &dels, FileID insertAfter, std::map<FileID, FileVec> &inserts) const
{
	if (insertAfter.isInvalid())
	{
		LogErrorByLvl(LogLvl_2, "insertAfter.isInvalid(): " << GetDebugFileName(top));
		return;
	}


	const char* topFileName = GetLowerFileNameInCache(top);

	LogInfoByLvl(LogLvl_3, "top --------> " << topFileName);

	// ��ǰ�ѱ������ĺ���ļ��б�
	FileSet alreadyIncludes;

	auto AddAlreadyIncludes = [&](FileID file)
	{
		file = GetFirstFileID(file);
		alreadyIncludes.insert(file);

		auto itr = m_minKids.find(file);
		if (itr != m_minKids.end())
		{
			const FileSet &kids = itr->second;
			Add(alreadyIncludes, kids);
		}
	};

	// ��ǰ�ļ��Ƿ�ɱ����루Ҫ��ǰ�ļ��������������ļ����ѱ�������
	auto CanInsert = [&](FileID file) -> bool
	{
		auto useItr = m_userUses.find(GetLowerFileNameInCache(file));
		auto kidItr = m_minKids.find(file);

		if (useItr != m_userUses.end())
		{
			const FileSet &useList = useItr->second;
			for (FileID beUse : useList)
			{
				bool can = Has(alreadyIncludes, beUse);
				if (!can)
				{
					if (kidItr != m_minKids.end())
					{
						can = Has(kidItr->second, beUse);
					}
				}

				if (!can)
				{
					LogInfoByLvl(LogLvl_3, "CanInsert = false, " << GetLowerFileNameInCache(file));
					return false;
				}
			}

			LogInfoByLvl(LogLvl_3, "CanInsert = true, " << GetLowerFileNameInCache(file));
			return true;
		}
		else
		{
			LogInfoByLvl(LogLvl_3, "CanInsert = true, " << GetLowerFileNameInCache(file));
			return true;
		}

		return false;
	};

	auto GetInsertFile = [&](FileID file, const FileSet &finalKeeps) -> FileID
	{
		auto useItr = m_userUses.find(GetLowerFileNameInCache(file));
		if (useItr == m_userUses.end())
		{
			return FileID();
		}

		const FileSet &useList = useItr->second;

		FileID insertFile;

		for (FileID beUse : useList)
		{
			FileID sameNameFile;

			for (FileID keep : finalKeeps)
			{
				if (IsSameName(keep, beUse))
				{
					LogInfoByLvl(LogLvl_3, "IsSameName = " << GetLowerFileNameInCache(keep) << ", at = " << DebugBeIncludeText(keep));
					sameNameFile = keep;
					break;
				}

				auto kidItr = m_minKids.find(GetFirstFileID(keep));
				if (kidItr != m_minKids.end())
				{
					const FileSet &kids = kidItr->second;

					for (FileID kid : kids)
					{
						if (IsSameName(kid, beUse))
						{
							LogInfoByLvl(LogLvl_3, "IsSameName kid = " << GetLowerFileNameInCache(kid) << ", at = " << DebugBeIncludeText(keep));
							sameNameFile = keep;
							break;
						}
					}
				}

				if (sameNameFile.isValid())
				{
					break;
				}
			}

			if (sameNameFile.isInvalid())
			{
				continue;
			}

			LogInfoByLvl(LogLvl_3, "IsFileBeforeFile(insertFile, sameNameFile) = " << IsFileBeforeFile(insertFile, sameNameFile) << ", " << DebugBeIncludeText(insertFile));
			LogInfoByLvl(LogLvl_3, "IsFileBeforeFile(insertFile, sameNameFile) = " << IsFileBeforeFile(insertFile, sameNameFile) << ", " << DebugBeIncludeText(sameNameFile));
			
			if (insertFile.isInvalid() || IsFileBeforeFile(insertFile, sameNameFile))
			{
				LogInfoByLvl(LogLvl_3, "temp insert file = " << DebugBeIncludeText(sameNameFile));
				insertFile = sameNameFile;
			}
		}

		return insertFile;
	};

	//------ ��ʼ����ÿ���ļ����Լ��������ļ�����һ�����Թ�ȥ�����������ļ������������������� ------//

	FileSet &remainAdds = adds;

	FileID lastInsert = insertAfter;
	FileSet finalKeeps;

	for (FileID keep : keeps)
	{
		if (!CanInsert(keep))
		{
			remainAdds.insert(keep);
		}
		else
		{
			LogInfoByLvl(LogLvl_3, "ok keep = " << GetLowerFileNameInCache(keep));
			AddAlreadyIncludes(keep);
			finalKeeps.insert(keep);
			// lastInsert = keep;
		}
	}

	while (!remainAdds.empty())
	{
		int before_size = (int)remainAdds.size();
		LogInfoByLvl(LogLvl_3, "before_size = " << before_size);

		for (FileID add : remainAdds)
		{
			if (CanInsert(add))
			{
				FileID insertFile = GetInsertFile(add, finalKeeps);
				if (insertFile.isValid())
				{
					lastInsert = insertFile;
				}

				LogInfoByLvl(LogLvl_3, "ok add = " << GetLowerFileNameInCache(add) << ", insert at = " << GetLowerFileNameInCache(lastInsert));

				if (Has(keeps, add))
				{
					dels.insert(add);
				}

				remainAdds.erase(add);

				inserts[lastInsert].push_back(add);
				AddAlreadyIncludes(add);
				break;
			}
		}

		int after_size = (int)remainAdds.size();
		LogInfoByLvl(LogLvl_3, "after_size = " << after_size);

		if (after_size >= before_size)
		{
			break;
		}
	}

	Del(remainAdds, keeps);

	LogInfoByLvl(LogLvl_3, "");

	// ��㴦�����������#include
	for (FileID add : remainAdds)
	{
		LogInfoByLvl(LogLvl_3, "remain add &&&&&&&&&&&&>" << GetLowerFileNameInCache(add));
		inserts[lastInsert].push_back(add);
	}
}

// �����Ӧ����һ�о�#include�������#include
FileID ParsingFile::CalcInsertLoc(const FileSet &includes, const FileSet &dels) const
{
	// �ҵ����ļ���һ�����޸ĵ�#include��λ��
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

	if (firstIncludeLineNo <= 0)
	{
		for (FileID a : includes)
		{
			SearchFirstInclude(a);
		}
	}

	return firstInclude;
}

// ȡ����ָ���ļ��ķ������
void ParsingFile::TakeHistory(FileID top, FileHistory &history) const
{
	history.m_isWindowFormat = IsWindowsFormat(top);
	history.m_filename = GetFileNameInCache(top);
	history.m_isSkip = IsPrecompileHeader(top);

	auto includeItr = m_includes.find(GetLowerFileNameInCache(top));
	if (includeItr == m_includes.end())
	{
		return;
	}

	//--------------- һ���ȷ����ļ�����Щ#include����Ҫ��ɾ�����滻������Ҫ������Щ#include ---------------//

	// ����Ӧ�������ļ��б�
	FileSet empty;
	auto itr = m_minInclude.find(top);

	const FileSet &finalIncludes = (itr != m_minInclude.end() ? itr->second : empty);
	if (finalIncludes.empty())
	{
		LogInfoByLvl(LogLvl_2, "don't need any include [top] = " << GetDebugFileName(top));
	}

	// �ɵİ����ļ��б�
	FileSet oldIncludes	= includeItr->second;

	// �µİ����ļ��б�
	FileSet newIncludes	= finalIncludes;

	// Ӧ�����İ����ļ��б�
	FileSet keeps;

	// 1. �ҵ��¡����ļ������б���[id��ͬ�����ļ�����ͬ���ļ�]��һ�Զ�����
	for (FileID finalInclude : finalIncludes)
	{
		const std::string finalIncludeFileName = GetLowerFileNameInCache(finalInclude);

		for (FileID oldInclude : oldIncludes)
		{
			if (finalIncludeFileName == GetLowerFileNameInCache(oldInclude))
			{
				oldIncludes.erase(oldInclude);
				newIncludes.erase(finalInclude);
				keeps.insert(oldInclude);
				break;
			}
		}
	}	

	// 2. ����¾��ļ��б���ܻ�ʣ��һЩ�ļ����������ǣ�ֱ��ɾ���ɵġ�����µ�

	// Ӧ���
	FileSet &dels = oldIncludes;

	// Ӧɾ��
	FileSet &adds = newIncludes;

	//--------------- ������ʼȡ��������� ---------------//

	FileID insertAfter = CalcInsertLoc(includeItr->second, dels);

	std::map<FileID, FileVec> inserts;
	SortAddFiles(top, adds, keeps, dels, insertAfter, inserts);

	// 1. ȡ��ɾ��#include��¼
	TakeDel(history, dels);

	// 2. ȡ������ǰ��������¼
	TakeForwardClass(history, insertAfter, top);

	// 3. ȡ������#include��¼
	TakeAdd(history, top, inserts);
}

// ȡ���Ե�ǰcpp�ļ��ķ������
void ParsingFile::TakeHistorys(FileHistoryMap &out) const
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
			TakeHistory(top, out[fileName]);
		}
	}

	TakeCompileErrorHistory(out);
}

// ���ļ��Ƿ��Ǳ�-includeǿ�ư���
inline bool ParsingFile::IsForceInclude(FileID file) const
{
	if (file == m_root)
	{
		return false;
	}

	FileID parent = GetFileID(m_srcMgr->getIncludeLoc(file));
	return (m_srcMgr->getFileEntryForID(parent) == nullptr);
}

// ���ļ��Ƿ���Ԥ����ͷ�ļ�
bool ParsingFile::IsPrecompileHeader(FileID file) const
{
	const std::string fileName = pathtool::get_file_name(GetLowerFileNameInCache(file));
	return strtool::start_with(fileName, "stdafx");
}

// ȡ�����ļ��ı��������ʷ
void ParsingFile::TakeCompileErrorHistory(FileHistoryMap &out) const
{
	FileHistory &history			= out[GetLowerFileNameInCache(m_root)];
	history.m_compileErrorHistory	= m_compileErrorHistory;
}

// ��ӡ���ü�¼
void ParsingFile::PrintUse() const
{
	HtmlDiv &div = HtmlLog::instance->m_newDiv;
	div.AddRow(AddPrintIdx() + ". list of use : use count = " + get_number_html(m_uses.size()), 1);

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

// ��ӡ#include��¼
void ParsingFile::PrintInclude() const
{
	HtmlDiv &div = HtmlLog::instance->m_newDiv;
	div.AddRow(AddPrintIdx() + ". list of include : include count = " + get_number_html(m_includes.size()), 1);

	for (auto &itr : m_includes)
	{
		const std::string &fileName = itr.first;

		if (!CanCleanByName(fileName.c_str()))
		{
			continue;
		}

		div.AddRow("parent = " + fileName, 2);

		for (FileID beinclude : itr.second)
		{
			div.AddRow("include = " + DebugBeIncludeText(beinclude), 3);
		}

		div.AddRow("");
	}
}

// ��ӡ�����������������������ȵļ�¼
void ParsingFile::PrintUseName() const
{
	HtmlDiv &div = HtmlLog::instance->m_newDiv;
	div.AddRow(AddPrintIdx() + ". list of use name : use count = " + get_number_html(m_useNames.size()), 1);

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

// ��ȡ�ļ���ʹ��������Ϣ���ļ�������ʹ�õ����������������������Լ���Ӧ�к�
void ParsingFile::DebugUsedNames(FileID file, const std::vector<UseNameInfo> &useNames) const
{
	HtmlDiv &div = HtmlLog::instance->m_newDiv;
	div.AddRow(DebugParentFileText(file, useNames.size()), 2);

	for (const UseNameInfo &beuse : useNames)
	{
		div.AddRow("use = " + DebugBeIncludeText(beuse.file), 3);

		for (const string& name : beuse.nameVec)
		{
			std::stringstream linesStream;

			auto linesItr = beuse.nameMap.find(name);
			if (linesItr != beuse.nameMap.end())
			{
				const std::set<int> &lines = linesItr->second;
				int n = (int)lines.size();
				int i = 0;
				for (int line : lines)
				{
					linesStream << get_number_html(line);
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

// �Ƿ��б�Ҫ��ӡ���ļ�
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

// ��ӡ��תΪǰ����������ָ������ü�¼
void ParsingFile::PrintUseRecord() const
{
	// 1. ���ļ�ǰ��������¼���ļ����й���
	UseRecordsByFileMap recordMap;

	for (auto &itr : m_locUseRecordPointers)
	{
		SourceLocation loc	= itr.first;
		FileID file	= GetFileID(loc);
		recordMap[file].insert(itr);
	}
	recordMap.erase(FileID());

	// 2. ��ӡ
	HtmlDiv &div = HtmlLog::instance->m_newDiv;
	div.AddRow(AddPrintIdx() + ". use records list: file count = " + get_number_html(recordMap.size()), 1);

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

// ��ӡ���յ�ǰ��������¼
void ParsingFile::PrintForwardClass() const
{
	if (m_fowardClass.empty())
	{
		return;
	}

	HtmlDiv &div = HtmlLog::instance->m_newDiv;
	div.AddRow(AddPrintIdx() + ". final forward class list: file count = " + get_number_html(m_fowardClass.size()), 1);

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

// ��ӡ��������������ļ��б�
void ParsingFile::PrintAllFile() const
{
	HtmlDiv &div = HtmlLog::instance->m_newDiv;
	div.AddRow(AddPrintIdx() + ". list of all file: file count = " + get_number_html(m_files.size()), 1);

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

// ��ӡ������־
void ParsingFile::PrintHistory() const
{
	m_compileErrorHistory.Print();

	int i = 0;

	for (auto &itr : m_historys)
	{
		const FileHistory &history = itr.second;
		if (!history.IsNeedClean())
		{
			continue;
		}

		if (ProjectHistory::instance.HasCleaned(history.m_filename))
		{
			continue;
		}

		if (Project::instance.m_logLvl < LogLvl_2)
		{
			if (!cpptool::is_cpp(history.m_filename))
			{
				continue;
			}
		}

		history.Print(++i, false);
	}
}

// ��ӡ���ļ��ڵ������ռ�
void ParsingFile::PrintNamespace() const
{
	HtmlDiv &div = HtmlLog::instance->m_newDiv;
	div.AddRow(AddPrintIdx() + ". each file's namespace: file count = " + get_number_html(m_namespaces.size()), 1);

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
			div.AddRow("declare namespace = " + get_include_html(ns), 3);
		}

		div.AddRow("");
	}
}

// ��ӡ���ļ���Ӧ������using namespace
void ParsingFile::PrintUsingNamespace() const
{
	std::map<FileID, std::set<std::string>>	nsByFile;
	for (auto &itr : m_usingNamespaces)
	{
		SourceLocation loc		= itr.first;
		const NamespaceDecl	*ns	= itr.second;

		nsByFile[GetFileID(loc)].insert(ns->getQualifiedNameAsString());
	}

	HtmlDiv &div = HtmlLog::instance->m_newDiv;
	div.AddRow(AddPrintIdx() + ". each file's using namespace: file count = " + get_number_html(nsByFile.size()), 1);

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
			div.AddRow("using namespace = " + get_include_html(ns), 3);
		}

		div.AddRow("");
	}
}

// ��ӡ���ļ��ڵ�using
void ParsingFile::PrintUsingXXX() const
{
	HtmlDiv &div = HtmlLog::instance->m_newDiv;
	div.AddRow(AddPrintIdx() + ". each file's using xxx", 1);

	for (const UsingShadowDecl* usingDecl : m_usings)
	{
		SourceLocation loc = usingDecl->getLocation();
		FileID file = GetFileID(loc);
		
		if (!IsNeedPrintFile(file))
		{
			continue;
		}

		NamedDecl *usingNameDecl = usingDecl->getTargetDecl();

		div.AddRow("at loc = " + DebugLocText(loc), 2);
		div.AddRow("using xxx = " + get_include_html(usingNameDecl->getQualifiedNameAsString()), 3);
	}
}

// ��ӡ��������ε��ļ�
void ParsingFile::PrintSameFile() const
{
	HtmlDiv &div = HtmlLog::instance->m_newDiv;
	div.AddRow(AddPrintIdx() + ". same file list: file count = " + get_number_html(m_sameFiles.size()), 1);

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

// ��ӡÿ���ļ�����Ӧ�������ļ���������ǰ������
void ParsingFile::PrintMinInclude() const
{
	FileSet all;
	Add(all, m_minInclude);
	Add(all, m_fowardClass);

	HtmlDiv &div = HtmlLog::instance->m_newDiv;
	div.AddRow(strtool::get_text(cn_file_min_use, get_number_html(++m_printIdx).c_str(), get_number_html(all.size()).c_str()), 1);	

	for (FileID by : all)
	{
		if (!CanClean(by))
		{
			continue;
		}

		div.AddRow(DebugParentFileText(by, 0), 2);

		auto includeItr = m_minInclude.find(by);
		if (includeItr != m_minInclude.end())
		{
			const FileSet &includeList = includeItr->second;
			for (FileID kid : includeList)
			{
				div.AddRow("min use = " + DebugBeIncludeText(kid), 3);
			}
		}

		auto forwardItr = m_fowardClass.find(by);
		if (forwardItr != m_fowardClass.end())
		{
			const RecordSet &forwards = forwardItr->second;
			for (const CXXRecordDecl *record : forwards)
			{
				div.AddRow("add forward class = " + GetRecordName(*record), 3);
			}
		}

		div.AddRow("");
	}
}

void ParsingFile::PrintMinKid() const
{
	HtmlDiv &div = HtmlLog::instance->m_newDiv;
	div.AddRow(strtool::get_text(cn_file_min_kid, get_number_html(++m_printIdx).c_str(), get_number_html(m_minKids.size()).c_str()), 1);

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

// ��ӡ
void ParsingFile::PrintOutFileAncestor() const
{
	HtmlDiv &div = HtmlLog::instance->m_newDiv;
	div.AddRow(strtool::get_text(cn_file_sys_ancestor, get_number_html(++m_printIdx).c_str(), get_number_html(m_outFileAncestor.size()).c_str()), 1);

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
	HtmlDiv &div = HtmlLog::instance->m_newDiv;
	div.AddRow(strtool::get_text(cn_file_user_use, get_number_html(++m_printIdx).c_str(), get_number_html(m_userUses.size()).c_str()), 1);

	for (auto &itr : m_userUses)
	{
		const std::string &top = itr.first;

		div.AddRow("fileName = [" + top + "]", 2);

		for (FileID beuse : itr.second)
		{
			div.AddRow("be user use = " + DebugBeIncludeText(beuse), 3);
		}

		div.AddRow("");
	}
}