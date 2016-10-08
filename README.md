2016-10-08 注意：当前正在试验新的清理方式，仍有问题，暂时不要使用（或者通过添加-mode 1+2参数改为采用更保守的清理方式）
=====

cxx-clean-include说明
=====

cxx-clean-include是一个用于清理c++文件中多余#include的工具，可以对visual studio项目（2005及以上版本）进行清理，也可以对单个文件夹（含子文件夹）的c++源文件进行清理。

## 使用方法

* 1. 下载本项目
* 2. 双击运行hello文件夹下的run.bat
* 3. 如果成功的话，hello项目将被清理，且将在hello文件夹下生成清理日志（可使用浏览器查看）
* 4. 将run.bat里面的./hello.vcxproj改成你自己的vs工程文件，就可以清理你的工程了

注意：使用前请备份你的代码

（如果无法正常使用，请试着安装visual studio 2013的32位运行时组件，可到官网：https://www.microsoft.com/zh-cn/download/details.aspx?id=40784 中下载vcredist_x86.exe并安装)

## cxx-clean-include的一些测试结果

目前，已在网上的一些开源vs项目上测试使用了本工具（只是自行测试）

1. https://github.com/MSOpenTech/redis

该项目是由[MS Open Tech组织]将redis移植到windows下的版本，使用本工具清理msvs\RedisServer.vcxproj后发现共有28个文件（含.c文件和.h头文件）包含多余的#include，清理后仍可编译成功。
（清理前须将头文件包含路径中的$(SolutionDir)删掉）

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

// 比如：cxxclean -clean d:/vs2005/hello.vcproj
// vs项目名称最好是绝对路径，如: d:/vs2005/hello.vcproj、d:/vs2008/hello.vcxproj
```

该命令将清理整个vs项目内的c++文件，同时将在当前文件夹自动生成清理日志

* 2. 对于单个文件夹，可以使用以下命令

```cpp
cxxclean -clean 文件夹路径

// 比如：cxxclean -clean d:/a/b/hello/
// 文件夹路径最好是绝对路径，如: d:/a/b/hello/、/home/proj/hello/
```

该命令将清理该文件夹内的c++文件，同时将在当前文件夹自动生成清理日志

但很多情况下需要指定更详细的编译条件，如指定头文件路径、预定义宏等，clang库已内置提供了相应的命令行参数供使用，可使用如下方式（注意添加--号）：

```cpp
cxxclean -clean 文件夹路径 -- -I"你的头文件搜索路径" -D 需要预定义的宏 -include 需要强制包含的文件
（其中：-I、-D、-include均可使用多次）

// 例如：cxxclean -clean d:/a/b/hello/ -- -I"../../" -I"../" -I"./" -I"../include" -D DEBUG -D WIN32 -include platform.h
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

  -mode=<string>  - 用于指定哪些清理类型应开启，默认为[-mode 3]，不同清理类型用+号连接，例如：[-mode 1+2]
                        清理类型1. 清除多余的#include
						清理类型2. 尝试替换#include
						清理类型3. 每个头文件仅包含自己所需要的头文件，并尽量用前置声明替代#include[本模式只能单独使用]

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

## 源码对应的llvm版本：282490，clang版本：282492

## 感谢

有部分试用了本工具的人进行了后续反馈，明确指出了存在的问题并给予了建议，在此表示十分感谢。

卢文茂：指出定义子类class B : public A时未能成功引用父类A，给出建议屏蔽预编译头文件的修改，且不重复打印相同文件的日志

张宇飞：指出本工具无法处理由编译器合成的构造函数调用，例如，若有类class A{}，由于A未拥有显式构造函数，所以new A时会导致无法引用到类A。指出由于未能正确地引用特化后的模板将导致链接失败，以及无法处理sizeof(A)的问题

大fedor：指出当有更深层次的包含关系时替换会有问题，给出建议增加功能参数以单独控制不同的清理类型，更加灵活