cxx-clean-include
=====

cxx-clean-include是一个用于清理c++文件中多余#include的工具。

举个例子，假设有一个文件hello.cpp，里面的内容是：

```cpp
#include "a.h"
#include "b.h"
#include "c.h"
#include "d.h"
#include "useful.h"
```

而其中只有useful.h被用到，那么a.h、b.h、c.h、d.h的#include语句将被移除掉，最终hello.cpp变为：

```cpp
#include "useful.h"
```

cxx-clean-include目前支持清理visual studio项目（vs2005及以上版本），同时支持清理指定文件夹下的c++文件，

## 使用方法

* 1. 对于visual studio项目，可以使用以下命令（注意包含--号）：

```cpp
cxxclean -clean vs项目名称 --

// vs项目名称最好是绝对路径，如: d:/vs2005/hello.vcproj、d:/vs2008/hello.vcxproj
```

该命令将清理整个vs项目内的c++文件

比如：cxxclean -clean d:/vs2005/hello.vcproj --

* 2. 对于单个文件夹，可以使用一下命令

```cpp
cxxclean -clean 文件夹路径 --

// 文件夹路径最好是绝对路径，如: d:/a/b/hello/、/home/proj/hello/
```

该命令将清理该文件夹内的c++文件

比如：cxxclean -clean d:/a/b/hello/ --

## 仔细看代码，里面有一个cxxclean.rar文件，还有一个hello文件夹

cxxclean.rar里面有cxxclean.exe文件，即cxx-clean-include工具在windows下的执行文件（linux下后续仅会提供CentOs下的编译文件）

hello则是一个visual studio项目，用于测试cxx-clean-include是否正常工作的，可以下载下来使用cxxclean -clean hello.vcxproj命令看执行后的清理效果

## 注意：使用之前请备份你的c++代码

## 具体参数

未完待续

这个月一直加班完全没时间，详细说明将在3月13号补上

代码没有整理，有点乱，注释后面补上