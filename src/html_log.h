///<------------------------------------------------------------------------------
//< @file:   html_log.h
//< @author: 洪坤安
//< @brief:  html日志类，用来美化打印日志的
//< Copyright (c) 2016 game. All rights reserved.
///<------------------------------------------------------------------------------

#ifndef _html_log_h_
#define _html_log_h_

#include <iterator>
#include <vector>

namespace cxxclean
{
	// 1. 中文
	static const char* cn_log							= "清理%s的日志-%s.html";
	static const char* cn_time							= "%04d年%02d月%02d日%02d时%02d分%02d秒";
	static const char* cn_cpp_file						= "[ %s ] c++ 文件";
	static const char* cn_folder						= "[ %s ] 文件夹";
	static const char* cn_project						= "[ %s ] visual studio工程";
	static const char* cn_project_1						= "%s工程";
	static const char* cn_clean							= "本页面是对 %s 的分析日志，最终结果以本页面最底部的统计结果为准";
	static const char* cn_project_text					= "允许清理的c++文件列表以及待分析的c++源文件列表";
	static const char* cn_project_allow_files			= "允许清理的c++文件列表：文件个数 = %s（不属于该列表的c++文件不允许被改动）";
	static const char* cn_project_allow_file			= "允许清理的文件 = %s";
	static const char* cn_project_source_list			= "待分析的c++源文件列表：文件个数 = %s（不属于该列表的c++文件不会被分析）";
	static const char* cn_project_source				= "待分析的c++源文件 = %s";
	static const char* cn_project_allow_dir				= "允许清理文件夹";

	static const char* cn_file_history					= "第%s个文件%s可被清理，分析结果如下：";
	static const char* cn_file_history_compile_error	= "第%s个文件%s发生了严重编译错误，无法被清理，一部分日志如下：";
	static const char* cn_file_history_title			= "%s/%s. 单独分析%s文件的日志";
	static const char* cn_file_skip						= "注意：检测到本文件为预编译文件，本文件将不会被改动";

	static const char* cn_error							= "错误：编译本文件时产生了以下编译错误：";
	static const char* cn_error_num_tip					= "产生了第%s个编译错误，编译错误号 = %s";
	static const char* cn_fatal_error_num_tip			= "产生了第%s个编译错误，编译错误号 = %s，属于严重编译错误";
	static const char* cn_error_fatal					= "==> 注意：由于发生严重错误[错误号=%s]，本文件的分析结果将被丢弃";
	static const char* cn_error_too_many				= "==> 注意：至少产生了%s个编译错误，由于编译错误数过多，本文件的分析结果将被丢弃";
	static const char* cn_error_ignore					= "==> 编译结果：共产生了%s个编译错误，由于错误较少或不严重，本文件的分析结果仍将被统计";

	static const char* cn_file_unused_count				= "该文件中有%s行多余的#include";
	static const char* cn_file_unused_line				= "可移除第%s行";
	static const char* cn_file_unused_include			= "该行原来的#include文本 = %s";

	static const char* cn_file_can_replace_num			= "该文件中有%s个#include可被替换";
	static const char* cn_file_can_replace_line			= "第%s行可以被替换，该行原来的内容 = %s";
	static const char* cn_file_replace_same_text		= "可以被替换为新的 = %s";
	static const char* cn_file_replace_old_text			= "原本的#include = %s";
	static const char* cn_file_replace_new_text			= "根据路径搜索得出的新的#include = %s";
	static const char* cn_file_force_include_text		= " ==>  [注意: 本次替换将被跳过，因为该行可能已被强制包含]";
	static const char* cn_file_replace_in_file			= "（注：新的#include来自于%s文件的第%s行）";

	static const char* cn_file_add_forward_num			= "该文件中可以新增%s个前置声明";
	static const char* cn_file_add_forward_line			= "可在第%s行新增前置声明，该行原来的内容 = %s";
	static const char* cn_file_add_forward_old_text		= "该行原来的内容 = %s";
	static const char* cn_file_add_forward_new_text		= "新增前置声明 = %s";
	static const char* cn_file_add_line_num				= "该文件中可以新增%s行";
	static const char* cn_file_add_line					= "可在第%s行新增行，该行原来的内容 = %s";
	static const char* cn_file_add_line_new				= "新增行 = %s(对应文件 = %s)";

	static const char* cn_file_min_use					= "%s. 各文件的最小引用文件集，文件数 = %s";
	static const char* cn_file_min_kid					= "%s. 各文件的最小引用后代文件集，文件数 = %s";
	static const char* cn_file_sys_ancestor				= "%s. 各系统文件的祖先文件，文件数 = %s";
	static const char* cn_file_user_use					= "%s. 各用户文件的引用记录，文件数 = %s";

	static const char* cn_project_history_title			= "统计结果";
	static const char* cn_project_history_clean_count	= "清理结果：共有%s个c++文件可被清理";
	static const char* cn_project_history_src_count		= "本次共分析了%s个cpp（或cxx、cc）源文件";

	static const char* cn_parent_file_debug_text		= "文件 = %s, 子文件数 = %s";
	static const char* cn_file_debug_text				= "%s[%s](文件ID = %d)(层数 = %s){该文件来自于%s}";
	static const char* cn_file_include_line				= "[%s第%s行%s]";
	static const char* cn_main_file_debug_text			= "[%s](文件ID = %d)(层数 = %s)";
	static const char* cn_outer_file_flag				= "[外部文件]";

	// 行类型
	enum RowType
	{
		Row_None	= 0,	// 正常
		Row_Error	= 1,	// 错误
	};

	// 格子类型
	enum GridType
	{
		Grid_None	= 0,	// 正常
		Grid_Ok		= 1,	// 正确
		Grid_Error	= 2,	// 错误
	};

	struct DivGrid
	{
		DivGrid()
			: width(0)
			, gridType(Grid_None)
		{
		}

		std::string text;
		int			width;
		GridType	gridType;	// 本格子的类型
	};

	struct DivRow
	{
		DivRow()
			: tabCount(0)
			, rowType(Row_None)
		{
		}

		RowType					rowType;	// 本行的类型
		int						tabCount;
		std::vector<DivGrid>	grids;
	};

	struct HtmlDiv
	{
		HtmlDiv()
			: hasErrorTip(false)
		{
		}

		void Clear()
		{
			titles.clear();
			rows.clear();
			hasErrorTip = false;
		}

		void AddTitle(const char* title, int width = 100);

		void AddTitle(const std::string &title, int width = 100);

		void AddRow(const char* text, int tabCount = 0, int width = 100, bool needEscape = false, RowType rowType = Row_None, GridType gridType = Grid_None);

		void AddRow(const std::string &text, int tabCount = 0 /* 缩进tab数 */, int width = 100, bool needEscape = false, RowType rowType = Row_None, GridType gridType = Grid_None);

		void AddGrid(const char* text, int width = 0, bool needEscape = false, GridType gridType = Grid_None);

		void AddGrid(const std::string &text, int width = 0, bool needEscape = false, GridType gridType = Grid_None);

		std::vector<DivGrid>	titles;
		std::vector<DivRow>		rows;
		bool					hasErrorTip;
	};

	// 用于将日志转成html格式，方便查看
	class HtmlLog
	{
	public:
		// 设置网页文件的标题
		void SetHtmlTitle(const std::string &title);

		// 设置网页内的文字提示
		void SetTip(const std::string &tip);

		void BeginLog();

		void EndLog();

		void AddDiv(const HtmlDiv &div);

		// 添加大标题
		void AddBigTitle(const std::string &title);

	private:
		static std::string GetHtmlStart(const char* title, const char* time, const char *tip);

	public:
		static HtmlLog instance;

	public:
		// 网页文件标题
		std::string		m_htmlTitle;

		// 网页内的提示
		std::string		m_tip;

		// 当前的div
		HtmlDiv			m_newDiv;
	};
}

#endif // _html_log_h_