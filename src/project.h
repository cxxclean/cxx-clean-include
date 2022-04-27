///<------------------------------------------------------------------------------
//< @file:   project.h
//< @author: 洪坤安
//< @brief:  本次清理c++的任务内容
///<------------------------------------------------------------------------------

#pragma once

#include <iterator>
#include <set>
#include <vector>
#include <string>

enum LogLvl
{
	LogLvl_0 = 0,		// 仅打印最终的统计结果
	LogLvl_1 = 1,		// 默认：打印各文件的清理情况和最终的统计结果
	LogLvl_2,			// 用于调试：打印各文件的删改情况，打印各文件引用到了其他文件的类名、函数名、宏名，项目成员文件等
	LogLvl_3,			// 用于调试：额外打印各文件直接或者间接依赖的文件集
	LogLvl_Max			// 用于调试：额外打印异常、额外打印语法树
};

typedef std::set<std::string> FileNameSet;
typedef std::vector<std::string> FileNameVec;

// 项目配置
class Project
{
public:
	Project()
		: m_isOverWrite(false)
		, m_logLvl(LogLvl_0)
		, m_printIdx(0)
	{
	}

public:
	// 该文件是否允许被清理
	static inline bool CanClean(const std::string &filename)
	{
		return CanClean(filename.c_str());
	}

	// 该文件是否允许被清理
	static bool CanClean(const char* filename);

	// 是否应忽略该文件
	static bool IsSkip(const char* filename);

	// 移除非c++后缀的源文件
	void Fix();

	// 打印索引 + 1
	std::string AddPrintIdx() const;

	// 打印本次清理的文件列表
	void Print() const;

public:
	static Project instance;

public:
	// 允许被清理的文件列表（只有属于本列表内的c++文件才允许被改动）
	FileNameSet					m_canCleanFiles;

	// 待清理的c++源文件列表，只能是c++后缀的文件，如cpp、cxx等
	FileNameVec					m_cpps;

	// 忽略文件列表
	FileNameSet					m_skips;

	// 工作目录
	std::string					m_workingDir;

	// 命令行选项：是否覆盖原来的c++文件（当本选项被关闭时，项目内的c++文件不会有任何改动）
	bool						m_isOverWrite;

	// 命令行选项：打印的详细程度，0 ~ 9，0表示不打印，默认为1，最详细的是9
	LogLvl						m_logLvl;

	// 当前打印索引，仅用于日志打印
	mutable int					m_printIdx;
};