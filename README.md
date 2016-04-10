cxx-clean-include说明
=====

cxx-clean-include是一个用于清理c++文件中多余#include的工具，可以对visual studio项目（2005及以上版本）进行清理，也可以对单个文件夹（含子文件夹）的c++源文件进行清理。

## 使用方法

使用前，请确保已安装visual studio 2013的32位运行时组件，可到官网：https://www.microsoft.com/zh-cn/download/details.aspx?id=40784 ，下载其中的vcredist_x86.exe并运行安装

然后：

* 1. 下载cxxclean.rar文件并解压，将得到可执行文件cxxclean.exe和一个文件夹hello（hello里面是示例代码）
* 2. 双击运行hello文件夹下的run_cxxclean_hello.bat
* 3. 如果成功的话，将生成cxxclean_hello.html日志，并且hello项目已被清理成功

将run_cxxclean_hello.bat里面的./hello.vcxproj改成你自己的vs工程文件，就可以清理你的工程了

注意：使用前请备份你的代码

## cxx-clean-include的作用

由于cxx-clean-include基于llvm+clang库编写而成，依托于clang库对现有编译器的兼容，所以本项目也支持大部分msvc、gcc/g++的语法，并完整支持c++11标准。

cxx-clean-include可以做到

- [x] 清除c++文件中多余的#include（不仅可以清理cpp、cxx等后缀的源文件，也可以清理源文件包含的h、hxx、hh等后缀的头文件）
- [x] 自动生成前置声明（某些时候，会出现包含了某个文件，却仅使用了该文件内的类的指针或引用，而未访问其成员的情况，此时cxx-clean-include将移除相应的#include语句，并添加前置声明）
- [x] 自动替换文件（某些时候，会出现#include了某个文件a，却仅使用到该文件内包含的其中一个文件b的情况，此时cxx-clean-include将遵循头文件路径搜索规则把原有的#include a语句替换成#include b语句）
- [x] 针对整个项目进行分析，而不仅分析单个c++源文件，通过采用合适的冲突处理规则，尽可能清理源文件和头文件，并尽可能保证清理整个项目后，仍然没有编译错误

关于cxx-clean-include的实际作用，举个例子，假设有一个文件hello.cpp，里面的内容是：

```cpp
#include "a.h"                    // a.h文件的内容是：class A{};
#include "b.h"                    // b.h文件的内容是：#include <stdio.h>
#include "c.h"                    // c.h文件的内容是：class C{};
#include "d.h"                    // d.h文件未被使用：class D{};

A *a;                             // 类A来自于a.h
void test_b() { printf(""); }     // 函数printf来自于stdio.h
C c;                              // 类C来自于c.h
```

cxx-clean-include将对hello.cpp文件进行分析：

~ 1. 首先，由于hello.cpp仅使用到a.h、b.h、c.h的内容，因此，可移除#include "d.h"语句

~ 2. 其次，经过分析，hello.cpp仅使用了a.h中类A的指针，因此，可新增前置声明class A，并移除#include "a.h"语句

~ 3. 最后，hello.cpp虽然包含了b.h，却仅使用到b.h所包含的stdio.h文件，因此，可将#include "b.h"语句替换为`#include <stdio.h>`

于是，在使用cxx-clean-include对hello.cpp进行清理后，hello.cpp将变为

```cpp
class A;
#include <stdio.h>
#include "c.h"                    // c.h文件的内容是：class C{};

A *a;                             // 类A来自于a.h
void test_b() { printf(""); }     // 函数printf来自于stdio.h
C c;                              // 类C来自于c.h
```

可以看出，hello.cpp第1行和第2行均被替换为更合适的语句。第4行则被移除

注意：本项目在windows和linux系统下均可进行编译，具体编译过程可参考clang插件的编译方法。

## cxx-clean-include的命令

cxx-clean-include目前支持清理visual studio项目（vs2005及以上版本），同时支持清理指定文件夹下的c++文件，同时输出结果是html格式，方便查看

* 1. 对于visual studio项目，可以使用以下命令：

```cpp
cxxclean -clean vs项目名称

// 比如：cxxclean -clean d:/vs2005/hello.vcproj > cxxclean_hello.html
// vs项目名称最好是绝对路径，如: d:/vs2005/hello.vcproj、d:/vs2008/hello.vcxproj
```

该命令将清理整个vs项目内的c++文件，同时日志存入cxxclean_hello.html

* 2. 对于单个文件夹，可以使用以下命令

```cpp
cxxclean -clean 文件夹路径

// 比如：cxxclean -clean d:/a/b/hello/ > cxxclean_hello.html
// 文件夹路径最好是绝对路径，如: d:/a/b/hello/、/home/proj/hello/
```

该命令将清理该文件夹内的c++文件，同时日志存入cxxclean_hello.html

但很多情况下需要指定更详细的编译条件，如指定头文件路径、预定义宏等，clang库已内置提供了相应的命令行参数供使用，可使用如下方式（注意添加--号）：

```cpp
cxxclean -clean 文件夹路径 -- -I 头文件搜索路径 -D 需要预定义的宏 -include 需要强制包含的文件
（其中：-I、-D、-include均可使用多次）

// 例如：cxxclean -clean d:/a/b/hello/ -- -I ../../ -I ../ -I ./ -D DEBUG -D WIN32 -include platform.h
```

## cxx-clean-include的命令行参数

在命令行中输入cxxclean -help可获取详细的命令行参数信息

cxx-clean-include提供以下选项：

```

  -clean=<string> - 该选项可以用来：
                        1. 清理指定的文件夹, 例如: 
						       cxxclean -clean ../hello/
						   该命令将清理../hello/文件夹（包括子文件夹）下的c++文件

                        2. 清理指定的visual studio项目(vs2005版本及以上): 例如: 
						       cxxclean -clean ./hello.vcproj （或 cxxclean -clean ./hello.vcxproj）
						   该命令将清理hello项目中的所有c++文件

  -no             - 即no overwrite的首字母缩写, 当传入此参数时，本工具仅执行分析并打印分析结果，所有的c++文件将不会被改动

  -onlycpp        - 仅允许清理源文件(cpp、cc、cxx后缀等), 禁止清理头文件(h、hxx、hh后缀等)

  -print-project  - 打印本次清理的配置, 例如：打印待清理的c++文件列表、打印允许清理的文件夹或c++文件列表等等

  -print-vs       - 打印visual studio项目的配置文件内容, 例如：打印头文件搜索路径、打印项目c++文件列表、打印预定义宏等等

  -src=<string>   - 仅清理指定的c++文件（路径须合法），本参数可结合-clean参数使用，可分2种情况：
                        1. 当-clean传入visual studio项目路径时，将采用vs项目的配置来清理该c++文件，例如: 
						       cxxclean -clean hello.vcproj -src ./hello.cpp
						   该命令将根据hello.vcproj项目的配置（如头文件搜索路径、预定义宏等配置）来清理hello.cpp文件

						2. 当-clean传入文件夹路径时，表示仅允许清理该文件夹路径下的文件，例如: 
						       cxxclean -clean ./a/b -src ./hello.cpp
						   该命令将仅允许对./a/b文件夹路径下的c++文件做修改

```

注意：在使用cxx-clean-include清理c++文件前，最好先传入-print-project参数，确保打印出的允许被清理c++文件列表符合要求。