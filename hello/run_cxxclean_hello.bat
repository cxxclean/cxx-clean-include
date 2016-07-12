@echo off
:: 使用说明：
:: 
:: 1.  若你的项目是visual studio项目(2005及以上版本，工程文件是vcproj或vcxproj后缀），则使用如下命令：
::         ..\cxxclean.exe -clean 你的vs项目
::
::     例如：..\cxxclean.exe -clean ./hello.vcxproj
::         
::
:: 2.  若你的项目不是vs工程，但放在一个文件夹底下，则使用如下命令：
::         ..\cxxclean.exe -clean 你的项目所在文件夹
::
::     例如：..\cxxclean.exe -clean ./
::
::
:: 3.  当你查看html日志，若发现有红色编译错误，提示找不到头文件路径、或者找不到某个宏时，可使用如下方式（注意添加--号）：
::         ..\cxxclean -clean 文件夹路径 -- -I"头文件搜索路径" -D 需要预定义的宏 -include 需要强制包含的文件
::         （其中：-I、-D、-include均可使用多次）
::
::     例如：cxxclean -clean d:/a/b/hello/ -- -I"../../" -I"../" -I"./" -I"../include" -D DEBUG -D WIN32 -include platform.h > cxxclean_hello.html

..\cxxclean.exe -clean ./hello.vcxproj