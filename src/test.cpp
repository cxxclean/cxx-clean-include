///<------------------------------------------------------------------------------
//< @file:   test.cpp
//< @author: 洪坤安
//< @date:   2016年3月5日
//< @brief:	 
//< Copyright (c) 2016. All rights reserved.
///<------------------------------------------------------------------------------

#include "clang/Rewrite/Core/RewriteBuffer.h"

#include <llvm/Support/raw_ostream.h>

using namespace llvm;
using namespace clang;

namespace test
{
	static void expand(unsigned Offset, unsigned Len, StringRef tagName,
	                     RewriteBuffer &Buf)
	{
		std::string BeginTag;
		raw_string_ostream(BeginTag) << '<' << tagName << '>';
		std::string EndTag;
		raw_string_ostream(EndTag) << "</" << tagName << '>';

		Buf.InsertTextAfter(Offset, BeginTag);
		Buf.InsertTextBefore(Offset+Len, EndTag);
	}
	
	void test1()
	{
		StringRef Input = "hello world";
		const char *Output = "<outer><inner>hello</inner></outer> ";

		RewriteBuffer Buf;
		Buf.Initialize(Input);
		StringRef RemoveStr = "world";
		size_t Pos = Input.find(RemoveStr);
		Buf.RemoveText(Pos, RemoveStr.size());

		StringRef TagStr = "hello";
		Pos = Input.find(TagStr);
		expand(Pos, TagStr.size(), "outer", Buf);
		expand(Pos, TagStr.size(), "inner", Buf);

		std::string Result;
		raw_string_ostream OS(Result);
		Buf.write(OS);
		OS.flush();

		llvm::outs() << "Output = " << Output << "\n";
		llvm::outs() << "Result = " << Result << "\n";
	}

	void test2()
	{
		StringRef oldText =
		    "#include 'a.h'\r\n"
		    "#include 'b.h'\r\n"
		    "#include 'c.h'\r\n"
		    "#include 'd.h'\r\n"
		    "#include 'e.h'\r\n"
		    "#include 'f.h'\r\n"
		    "#include 'g.h'\r\n"
		    "#include 'h.h'\r\n"
		    "#include 'i.h'\r\n";

		std::string newText;

		{
			RewriteBuffer Buf;
			Buf.Initialize(oldText);

			{
				StringRef RemoveStr = "#include 'a.h'\r\n";
				size_t Pos = oldText.find(RemoveStr);
				Buf.RemoveText(Pos, RemoveStr.size());
			}

			{
				StringRef replaceStr = "#include 'b.h'\r\n";
				size_t Pos = oldText.find(replaceStr);
				Buf.ReplaceText(Pos, replaceStr.size(), "#include 'b_1.h'\r\n");
			}

			{
				StringRef RemoveStr = "#include 'c.h'\r\n";
				size_t Pos = oldText.find(RemoveStr);
				Buf.RemoveText(Pos, RemoveStr.size());
			}

			{
				StringRef insertStr = "#include 'c.h'\r\n";
				size_t Pos = oldText.find(insertStr);
				Buf.InsertText(Pos, "class C;\n");
			}

			{
				StringRef replaceStr = "#include 'd.h'\r\n";
				size_t Pos = oldText.find(replaceStr);
				Buf.ReplaceText(Pos, replaceStr.size(), "#include 'd_1.h'\r\n");
			}

			raw_string_ostream OS(newText);
			Buf.write(OS);
			OS.flush();
		}

		llvm::outs() << "old =\n" << oldText << "\n";
		llvm::outs() << "new =\n" << newText << "\n";
	}
	
	void test()
	{
		test1();
		test2();
	}
}