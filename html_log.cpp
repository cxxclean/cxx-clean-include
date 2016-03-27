///<------------------------------------------------------------------------------
//< @file:   html_log.cpp
//< @author: 洪坤安
//< @date:   2016年3月19日
//< @brief:
//< Copyright (c) 2016 game. All rights reserved.
///<------------------------------------------------------------------------------

#include "html_log.h"
#include "tool.h"

#include <llvm/Support/raw_ostream.h>

const char* g_beginHtml = R"--(
<html>
    <head>
        <meta http-equiv="Content-Type" content="text/html; charset=gb18030" />
        <title>
            #{title}
        </title>
        <style type="text/css">
            body, button, input, select, textarea {
                font: 12px/1.5 tahoma, arial, "宋体"!important;
            }
            #main{top:50px;left:0px;right:0px;width:80%;z-index:1;position:absolute;MARGIN-RIGHT:auto;MARGIN-LEFT:auto;}

			body,div,dl,dt,dd,ul,ol,li,h1,h2,h3,h4,h5,h6,pre,code,form,textarea,select,optgroup,option,fieldset,legend,p,blockquote,th,td {
				margin: 0;
				padding: 0
			}

			body {
				background: #fff
			}

			ul,li,ol {
				list-style: none
			}

			h1 {
				font-size: 3.0em;
				font-weight: bold;
				color: #0c3762;
				border-bottom: dotted #e0e0e0;
				margin-bottom: 20px;
				margin-top: 50px;
			}

			table {
				border-collapse: collapse;
				border-spacing: 0
			}

			body,button,input,select,textarea {
				font: 12px/1.5 "Microsoft YaHei",arial,SimSun,"宋体";
				color: #000
			}

			a {
				color: #06c;
				text-decoration: none;
				outline: 0
			}

			a:hover,a:active,a:focus {
				color: #06c;
				text-decoration: underline
			}

			.include-text {
				color: #743481;
				font-weight: bold;
			}

			.number-text {
				color: #FF6C00;
				font-weight: bold;
			}

			.box {
				margin-top: 30px;	
				margin-bottom: 150px;
				padding: 5px 5px;
				border: 3px solid #cce1ef;
				border-radius: 15px;
				background: #eff6fA;
				box-shadow: 1px 1px 0 rgba(255,255,255,.5) inset,5px 5px 0px rgba(0,0,0,.2);
			}

			.title {
				position: relative;
				z-index: 1;
				height: 43px;
				background: #f7f7f7	
			}

			.title ul {
				box-shadow: 1px 1px 0 rgba(255,255,255,.2) inset,3px 3px 0 rgba(0,0,0,.2);    
				border: 1px solid #0D6EB8;    
				background: #f7f7f7;
				border-radius: 2px;
				border: 2px solid #d2d2d2;
				color: #FF4C00;
				height: 41px;
				line-height: 41px;
				overflow: hidden;
				font-weight: bold;
				font-size: 1.7em;
				border-radius: 10px;
			}

			.title .col {
				float: left;
				border-left: 2px solid #fff;
				border-right: 2px solid #e5e5e5;
				cursor: pointer;
				text-indent: 10px
			}

			.title .col:hover {
				background: #e8f5fd
			}

			.chart {
				position: relative;
				overflow: hidden;
				*margin-left: -40px;
				margin-top: 8px;
				border-radius: 10px;
			}

			.chart .row {
				position: relative;
				height: 28px;
				line-height: 28px;
				border-bottom: 1px solid #ebebeb;
				white-space: nowrap;
				text-overflow: ellipsis;
				background: #FFF
			}

			.chart .row:hover {
				background: #DDD;
			}

			.chart .row .grid {
				position: relative;
				float: left;
				white-space: nowrap;
				overflow: hidden
				text-indent: 60px;
				text-overflow: ellipsis;
			}

			.chart .row .grid a {
				cursor: default;
				text-decoration: none
			}

			.chart .row .grid a:hover,.chart .row .grid a:active {
				cursor: pointer;
				text-decoration: underline
			}
        </style>
    </head>
    <body>
        <div id="main">
)--";

const char* g_endHtml = R"--(
        </div>
    </body>
</html>
)--";

const char* g_divHtml = R"--(
            <div class="box">
                <div class="title">
                    <ul>
                        #{div_titles}
                    </ul>
                </div>
                <div class="chart">
                    <dl>
                        #{div_rows}
                    </dl>
                </div>
            </div>
)--";

const char *g_titleHtml = R"--(
                        <li class="col" style="width:#{width}%;">
                            #{title}
                        </li>
)--";

const char* g_rowHtml = R"--(
                        <dd class="row"#{bold}>
                            #{row}
                        </dd>
)--";

const char* g_gridHtml = R"--(
                            <div class="grid" style="width:#{width}%;text-indent:#{tab_count}px;">
                                #{grid}
                            </div>
)--";

namespace cxxcleantool
{
	HtmlLog HtmlLog::instance;

	void HtmlDiv::AddTitle(const char* title, int width /* = 100 */)
	{
		titles.resize(titles.size() + 1);

		DivGrid &grid	= titles.back();
		grid.text		= title;
		grid.width		= width;
	}

	void HtmlDiv::AddRow(const char* text, int tabCount /* = 1 */, int width /* = 100 */, bool needEscape /* = false */)
	{
		rows.resize(rows.size() + 1);
		DivRow &row		= rows.back();
		row.tabCount	= tabCount;

		AddGrid(text, width, needEscape);
	}

	void HtmlDiv::AddGrid(const char* text, int width, bool needEscape /* = false */)
	{
		DivRow &row		= rows.back();
		row.grids.resize(row.grids.size() + 1);

		DivGrid &grid	= row.grids.back();
		grid.text		= text;
		grid.width		= width;

		if (needEscape)
		{
			htmltool::escape_html(grid.text);
		}
	}

	std::string HtmlLog::GetHtmlStart(const char* title)
	{
		std::string beginHtml(g_beginHtml);
		strtool::replace(beginHtml, "#{title}", title);

		return beginHtml;
	}

	void HtmlLog::SetHtmlTitle(const std::string &title)
	{
		if (!m_htmlTitle.empty())
		{
			return;
		}

		m_htmlTitle = strtool::get_text(cn_clean, title.c_str());
	}

	// 设置网页内大标题
	void HtmlLog::SetBigTitle(const std::string &title)
	{
		if (!m_bigTitle.empty())
		{
			return;
		}

		m_bigTitle = strtool::get_text(cn_clean, title.c_str());
	}

	void HtmlLog::BeginLog()
	{
		llvm::outs() << GetHtmlStart(m_htmlTitle.c_str());
	}

	void HtmlLog::EndLog()
	{
		llvm::outs() << g_endHtml;
	}

	void HtmlLog::AddDiv(const HtmlDiv &div)
	{
		std::string divHtml			= g_divHtml;
		std::string divTitlesHtml;
		std::string divRowsHtml;

		for (DivGrid grid : div.titles)
		{
			std::string title(g_titleHtml);

			strtool::replace(title, "#{title}",	grid.text.c_str());
			strtool::replace(title, "#{width}",	strtool::itoa(grid.width).c_str());

			divTitlesHtml += title;
		}

		for (DivRow row : div.rows)
		{
			std::string rowHtml(g_rowHtml);
			std::string gridsHtml;

			int i = 0;

			for (DivGrid grid : row.grids)
			{
				std::string gridHtml(g_gridHtml);

				int tabCount = ((i == 0) ? row.tabCount * 35 : 0);
				++i;

				strtool::replace(gridHtml, "#{tab_count}",	strtool::itoa(tabCount).c_str());
				strtool::replace(gridHtml, "#{grid}",		grid.text.c_str());
				strtool::replace(gridHtml, "#{width}",		strtool::itoa(grid.width).c_str());

				gridsHtml += gridHtml;
			}

			strtool::replace(rowHtml, "#{row}", gridsHtml.c_str());
			if (row.tabCount == 1)
			{
				strtool::replace(rowHtml, "#{bold}", " style=\"font-weight:bold\"");
			}
			else
			{
				strtool::replace(rowHtml, "#{bold}", "");
			}

			divRowsHtml += rowHtml;
		}

		strtool::replace(divHtml, "#{div_titles}",	divTitlesHtml.c_str());
		strtool::replace(divHtml, "#{div_rows}",	divRowsHtml.c_str());

		llvm::outs() << divHtml;
	}

	// 添加大标题
	void HtmlLog::AddBigTitle(const std::string &title)
	{
		llvm::outs() << "<h1>" << title << "</h1>";
	}
}