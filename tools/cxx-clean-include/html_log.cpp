///<------------------------------------------------------------------------------
//< @file:   html_log.cpp
//< @author: ������
//< @brief:  html��־�࣬����������ӡ��־��
///<------------------------------------------------------------------------------

#include "html_log.h"
#include "tool.h"
#include <sstream>
#include <llvm/Support/raw_ostream.h>

const char* g_beginHtml = R"--(
<html>
	<head>
		<meta http-equiv="Content-Type" content="text/html; charset=gb18030" />
		<title>${title}</title>
		<style type="text/css">
			body{background:#2e3842;align:center;}
			body,button,input,select,textarea{font:12px/1.5 tahoma,arial,"����"!important;}			
			body,div,dl,dt,dd,ul,ol,li,h1,h2,h3,h4,h5,h6,pre{margin:0;padding:0}
			body,button,input,select,textarea{font:12px/1.5 "Microsoft YaHei",arial,SimSun,"����";color:#000}			
			#main{top:70px;left:0px;right:0px;width:86%;z-index:1;position:absolute;MARGIN-RIGHT:auto;MARGIN-LEFT:auto;}			
			ul,li,ol{list-style:none}
			h1{font-size:3em;font-family:"Microsoft YaHei";color:#fff;border-bottom:dotted #e0e0e0;margin-bottom:20px;margin-top:50px;text-align:center;}
			h1:before{content:"\2714";color:#00FF00;right:5px;top:3px;position:relative;font-size:60px;}			
			a{color:#06c;text-decoration:none;outline:0}
			a:hover,a:active,a:focus{color:#06c;text-decoration:underline}
			.src{color:#743481;}
			.num{color:#FF6C00;}
			.box{margin-top:30px;margin-bottom:5em;background:#fff;box-shadow:0 4px 8px 0 rgba(0,0,0,.1);border-radius:2px;padding:14px 0 0px;min-width:960px;}
			.title ul{border-top:1px solid #e0e0e0;border-bottom:1px solid #e0e0e0;height:41px;line-height:41px;font-size:1.4em;font-family:Microsoft YaHei;}
			.title .col{float:left;margin-left:-2px;border-left:2px solid #e5e5e5;border-right:2px solid #e5e5e5;text-indent:10px;white-space:nowrap;}
			.title .col:hover{background:#e8f5fd }
			.chart{padding:14px 0 10px;min-width:960px;}
			.chart .row{position:relative;height:28px;line-height:28px;border-bottom:1px solid #ebebeb;white-space:nowrap;text-overflow:ellipsis;background:#FFF }
			.chart .row:hover{background:#DDD;}
			.chart .error_row{position:relative;line-height:20px;white-space:nowrap;text-overflow:ellipsis;background:#FFF;color:#FF0000;border-bottom:2px dashed #ebebeb;}
			.chart .warn_row{position:relative;line-height:20px;white-space:nowrap;text-overflow:ellipsis;background:#FFF;color:#FF6600;border-bottom:2px dashed #ebebeb;}			
			.chart .error_row:hover{background:#DDD;}
			.chart .warn_row:hover{background:#DDD;}
			.chart .row div{float:left;height:100%;}
			.chart a{cursor:default;text-decoration:none }
			.chart a:hover,.chart a:active{cursor:pointer;text-decoration:underline }
			.chart .ok:before{margin-right:5px;vertical-align:middle;float:left;content:"";width:20px;height:28px;background-repeat:no-repeat;background-position:50%;background-image:url(data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAACgAAAAoCAYAAACM/rhtAAAAAXNSR0IArs4c6QAAAZZJREFUWAnt2DFLxDAUAOBXz+t64ujSxUkc5MT/II7+Ch1cXFwUB5dbXMTBxR/iKk6CCgoqCNJuLi5Otlet9w5f9UKbpM3LkSGBI9f2Jfl4SRNoEMdxAQ6XGYdtY9osAaMoor9O1EmSjB3OZ9ADTdeLz6DPoGkGTNv7NShm8OIth53bFIpC74QtTxKxIxvXiNu8SiH9BshGv7PVEIIgkA41tSn+j0PR+WsO2zeZFIcPpwIUcaRamVMPr46g3lrWdbjTfghbi11lr1aBpjjUWwNy4BoB9x8yuH7/Uk4JBnDhtIG7dykMnoawfvmpRHLitICIO3nJMRY+hiBFcuO0gL3u5EZah7SB0wIeLodwsDS5HYhIWzgtIAbJkMfPWXl8YSwV3X2O4utq7bMYkViOHkcL8bdgJvfu/67pPhcO+2u0D1ZlklBUc+IaA7GBDMmNawWsQ9rAtQaKSFs4HEf7JcFgseB0byx0YG2+Iz5iu270klSNahOH4xkDq9Cc9zzQNJs+gz6DphkwbV9u1PRN2LRD7vbOvyQ//6y3go2ZKqgAAAAASUVORK5CYII=);background-size:20px 20px;}
			.chart .err:before{content:"\2718";color:#FF0000;right:5px;position:relative;font-size:2.1em;}
			.header_bar{height:48px;background-color:#333;color:#fff;position:relative;z-index:1100;font-size:30px;padding-left:8px;border-bottom:dotted #e0e0e0;}
			.header_bar .small_text{margin-top:26px;margin-right:10px;float:right;font:10px/1.5 "Microsoft YaHei";}
			.time_bar{width:100%;height:35px;background-color:#fff;position:relative;z-index:2;text-align:left;box-shadow:2px 2px 2px rgba(1,1,1,.1);}
			.chart .col1{padding:0 10px;}.chart .col2{padding:0 55px;}.chart .col3{padding:0 90px;}.chart .col4{padding:0 125px;}.chart .col5{padding:0 160px;}.chart .col6{padding:0 195px;}.chart .col7{padding:0 230px;}.chart .col8{padding:0 265px;}
			.bold{font-weight:bold;}
		</style>
	</head>
	<body>
		<div class="header_bar">cxx-clean-include<span class="small_text">${time} ${tip}</span></div>
		<div id="main">)--";

const char* g_endHtml = R"--(
		</div>
	</body>
</html>)--";

const char* g_divHtml = R"--(
			<div class="box">
				<div class="title"><ul>${div_titles}					
				</ul></div>
				<div class="chart"><dl>${div_rows}					
				</dl></div>
			</div>)--";

const char *g_titleHtml = R"--(
					<li class="col" style="width:${width}%;">${title}</li>)--";

const char* g_rowHtml = R"--(
					<dd class="row${other_class}">${row}</dd>)--";

const char* g_warnRowHtml = R"--(
					<dd class="warn_row${other_class}">${row}</dd>)--";

const char* g_errorRowHtml = R"--(
					<dd class="error_row${other_class}">${row}</dd>)--";

const char* g_gridHtml = R"--(<div${class} style="${width}">${text}</div>)--";

const char* g_errorGridHtml = R"--(<div><pre${class}>${text}</pre></div>)--";

HtmlLog* HtmlLog::instance = nullptr;

void HtmlDiv::AddTitle(const char* title, int width /* = 100 */)
{
	titles.resize(titles.size() + 1);

	DivGrid &grid	= titles.back();
	grid.text		= title;
	grid.width		= width;
}

void HtmlDiv::AddTitle(const std::string &title, int width /* = 100 */)
{
	AddTitle(title.c_str(), width);
}

void HtmlDiv::AddRow(const char* text, int tabCount /* = 0 */, int width /* = 100 */, bool needEscape /* = false */, RowType rowType /*= Row_None */, GridType gridType /* = Grid_None */)
{
	rows.resize(rows.size() + 1);

	DivRow &row		= rows.back();
	row.tabCount	= __max(tabCount, 0);
	row.rowType		= rowType;

	AddGrid(text, width, needEscape, gridType);
}

void HtmlDiv::AddRow(const std::string &text, int tabCount /* = 0 /* ����tab�� */, int width /*= 100*/, bool needEscape /*= false*/, RowType rowType /*= Row_None*/, GridType gridType /*= Grid_None*/)
{
	AddRow(text.c_str(), tabCount, width, needEscape, rowType, gridType);
}

void HtmlDiv::AddGrid(const char* text, int width, bool needEscape /* = false */, GridType gridType /* = Grid_None */)
{
	DivRow &row = rows.back();
	auto &grids = row.grids;
	grids.resize(grids.size() + 1);

	DivGrid &grid	= grids.back();
	grid.text		= (needEscape ? escape_html(text) : text);
	grid.width		= width;
	grid.gridType	= gridType;
}

void HtmlDiv::AddGrid(const std::string &text, int width /*= 0*/, bool needEscape /*= false*/, GridType gridType /*= Grid_None*/)
{
	AddGrid(text.c_str(), width, needEscape, gridType);
}

HtmlLog::HtmlLog()
	: m_log(nullptr)
{}

bool HtmlLog::Init(const std::wstring &htmlPath, const std::string &htmlTitle, const std::string &tip)
{
	m_htmlPath = htmlPath;

	for (wchar_t &c : m_htmlPath)
	{
		if (c == L'.' || c == L'/' || c == L'\\' || c == L':' || c == L'-')
		{
			c = L'_';
		}
	}

	strtool::wide_replace(m_htmlPath, L"[", L"");
	strtool::wide_replace(m_htmlPath, L"]", L"");
	strtool::wide_replace(m_htmlPath, L" ", L"");

	std::string str_now = timetool::get_now(cn_time);
	std::wstring wstr_now = strtool::s2ws(str_now);

	m_htmlPath = cn_log1 + m_htmlPath + cn_log2 + wstr_now + cn_log3;
	m_htmlTitle = strtool::get_text(cn_clean, htmlTitle.c_str());
	m_tip = strtool::get_text(cn_clean, tip.c_str());

	m_htmlPath = s2ws(pathtool::get_absolute_path(ws2s(m_htmlPath).c_str()));
	return true;
}

void HtmlLog::Open()
{
	// �ļ��򿪷�ʽ������ԭ������
	FILE *file = _wfopen(m_htmlPath.c_str(), L"w");
	if (file)
	{
		m_log = new llvm::raw_fd_ostream(_fileno(file), true);
	}

	std::string beginHtml(g_beginHtml);
	strtool::replace(beginHtml, "${title}", m_htmlTitle.c_str());
	strtool::replace(beginHtml, "${time}", timetool::get_now().c_str());
	strtool::replace(beginHtml, "${tip}", m_tip.c_str());

	log() << beginHtml;
}

void HtmlLog::Close()
{
	log() << g_endHtml;
	log().flush();

	if (m_log)
	{
		delete m_log;
		m_log = nullptr;
	}
}

void HtmlLog::AddDiv(const HtmlDiv &div)
{
	// ����div�ı���
	std::string divTitlesHtml;

	for (const DivGrid &grid : div.titles)
	{
		std::string title(g_titleHtml);

		strtool::replace(title, "${title}",	grid.text.c_str());
		strtool::replace(title, "${width}",	strtool::itoa(grid.width).c_str());

		divTitlesHtml += title;
	}

	// ����
	std::stringstream divRowsHtml;

	for (const DivRow &row : div.rows)
	{
		std::string rowHtml = g_rowHtml;
		if (row.rowType == Row_Error)
		{
			rowHtml = g_errorRowHtml;
		}
		else if (row.rowType == Row_Warn)
		{
			rowHtml = g_warnRowHtml;
		}

		std::stringstream gridsHtml;

		for (const DivGrid &grid : row.grids)
		{
			std::string gridHtml		= (row.rowType == Row_Error || row.rowType == Row_Warn ? g_errorGridHtml : g_gridHtml);
			std::string widthHtml		= (grid.width == 100 || grid.width == 0) ? "" : strtool::get_text("width:%d%%;", grid.width);
			std::string gridClassHtml	= (grid.gridType == Grid_Ok ? " class=\"ok\"" : "");
			gridClassHtml += (grid.gridType == Grid_Error ? " class=\"err\"" : "");

			strtool::replace(gridHtml, "${class}",		gridClassHtml.c_str());
			strtool::replace(gridHtml, "${width}",		widthHtml.c_str());
			strtool::replace(gridHtml, " style=\"\"",	"");
			strtool::replace(gridHtml, "${text}",		grid.text.c_str());

			gridsHtml << gridHtml;
		}

		string rowClassHtml;
		if (row.tabCount > 0)
		{
			rowClassHtml = " col" + strtool::itoa(row.tabCount);
			if (row.tabCount == 1)
			{
				rowClassHtml += " bold";
			}
		}

		strtool::replace(rowHtml, "${other_class}", rowClassHtml.c_str());
		strtool::replace(rowHtml, "${row}", gridsHtml.str().c_str());
		divRowsHtml << rowHtml;
	}

	// ����div
	std::string divHtml = g_divHtml;
	strtool::replace(divHtml, "${div_titles}", divTitlesHtml.c_str());
	strtool::replace(divHtml, "${div_rows}",	divRowsHtml.str().c_str());

	log() << divHtml;
	log().flush();

	m_newDiv.Clear();
}

// ��Ӵ����
void HtmlLog::AddBigTitle(const std::string &title)
{
	log() << "\n			<h1>" << title << "</h1>";
}