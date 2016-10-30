///<------------------------------------------------------------------------------
//< @file:   project.h
//< @author: 洪坤安
//< @brief:  本次清理c++的任务内容
//< Copyright (c) 2016. All rights reserved.
///<------------------------------------------------------------------------------

#ifndef _project_h_
#define _project_h_

#include <iterator>
#include <set>
#include <vector>

namespace cxxclean
{
	enum LogLvl
	{
		LogLvl_0 = 0,		// 仅打印最终的统计结果
		LogLvl_1 = 1,		// 默认：打印各文件的清理情况和最终的统计结果
		LogLvl_2,			// 用于调试：打印各文件的删改情况
		LogLvl_3,			// 用于调试：额外打印各文件引用到了其他文件的类名、函数名、宏名，项目成员文件等
		LogLvl_4,			// 用于调试：额外打印各文件直接或者间接依赖的文件集
		LogLvl_5,			// 用于调试：额外打印异常
		LogLvl_6,			// 用于调试：额外打印语法树
		LogLvl_Max
	};

	// 清理模式，不同清理模式间可互相结合使用
	enum CleanMode
	{
		CleanMode_Unused = 1,	// 清除多余的#include
		CleanMode_Replace,		// 有些#include包含了无用的其他文件，允许用包含更少文件的#include来取代这类#include
		CleanMode_Need,			// （默认）每个文件尽量只包含自己用到的文件（将自动生成前置声明）（注意：本模式仅能被单独使用）
		CleanMode_Max
	};

	// 项目配置
	class Project
	{
	public:
		Project()
			: m_canCleanAll(false)
			, m_isOverWrite(false)
			, m_isOnlyNeed1Step(false)
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

		// 指定的清理选项是否开启
		static bool IsCleanModeOpen(CleanMode);

		// 当前是否允许清理
		bool CanCleanNow();

		// 生成允许清理文件列表
		void GenerateAllowCleanList();

		// 移除非c++后缀的源文件
		void Fix();

		// 打印索引 + 1
		std::string AddPrintIdx() const;

		// 打印本次清理的文件列表
		void Print() const;

	public:
		static Project instance;

	public:
		// 允许被清理的文件夹（只有处于该文件夹下的c++文件才允许被改动）
		std::string					m_allowCleanDir;

		// 允许被清理的文件列表（只有属于本列表内的c++文件才允许被改动），注意：当允许清理文件夹不为空时，本项无意义
		std::set<std::string>		m_allowCleanList;

		// 待清理的c++源文件列表，只能是c++后缀的文件，如cpp、cxx等
		std::vector<std::string>	m_cpps;

		// 工作目录
		std::string					m_workingDir;

		// 当前项目是否仅需要解析1遍，由其他参数决定，含义：true仅解析1遍、false需要解析2遍
		bool						m_isOnlyNeed1Step;

		// 命令行选项：是否允许清理项目内的c++文件，建议为true（false表示仅清理cpp文件）
		bool						m_canCleanAll;

		// 命令行选项：是否覆盖原来的c++文件（当本选项被关闭时，项目内的c++文件不会有任何改动）
		bool						m_isOverWrite;

		// 命令行选项：打印的详细程度，0 ~ 9，0表示不打印，默认为1，最详细的是9
		LogLvl						m_logLvl;

		// 命令行选项：清理模式列表，对于不同清理模式的开关，见 CleanLvl 枚举，默认仅开启1和2（不同清理模式可结合起来使用）
		std::vector<bool>			m_cleanModes;

		// 当前打印索引，仅用于日志打印
		mutable int					m_printIdx;
	};
}

#endif // _project_h_