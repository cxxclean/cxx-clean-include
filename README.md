cxx-clean-include
=====

cxx-clean-include是一个用于清理c++文件中多余#include的工具，既支持对visual studio 2005及以上版本的c++项目进行清理，也支持对单个文件夹下（含子文件夹）的c++源文件进行清理。

同时，由于cxx-clean-include基于llvm+clang库编写而成，依托于clang库对现有编译器的兼容，所以本项目也支持msvc、gcc/g++的语法，并完整支持c++11标准。

本项目在windows和linux系统下均可进行编译，但仅提供windows下的32位编译文件，如果想在linux系统下使用cxx-clean-include，则需要参考clang插件的编译方法，对本项目重新进行编译。

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

~ 3. 最后，hello.cpp虽然包含了b.h，却仅使用到b.h所包含的stdio.h文件，因此，可将#include "b.h"语句替换为#include <stdio.h>

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

## 下载cxx-clean-include

本项目中已上传了cxx-clean-include在windows下的32位编译版本，下载项目中的cxxclean.rar压缩文件并解压，将得到可执行文件cxxclean.exe，在命令行中输入cxxclean -help可获取详细的命令行参数信息

（linux下后续仅会提供CentOs下的编译文件）

## cxx-clean-include的使用方法

cxx-clean-include目前支持清理visual studio项目（vs2005及以上版本），同时支持清理指定文件夹下的c++文件，

* 1. 对于visual studio项目，可以使用以下命令（注意包含--号）：

```cpp
cxxclean -clean vs项目名称 --

// 比如：cxxclean -clean d:/vs2005/hello.vcproj --
// vs项目名称最好是绝对路径，如: d:/vs2005/hello.vcproj、d:/vs2008/hello.vcxproj
```

该命令将清理整个vs项目内的c++文件

* 2. 对于单个文件夹，可以使用以下命令

```cpp
cxxclean -clean 文件夹路径 --

// 比如：cxxclean -clean d:/a/b/hello/ --
// 文件夹路径最好是绝对路径，如: d:/a/b/hello/、/home/proj/hello/
```

该命令将清理该文件夹内的c++文件

但很多情况下需要指定更详细的编译条件，如指定头文件路径、预定义宏等，可使用如下方式：

```cpp
cxxclean -clean 文件夹路径 -- -I 头文件搜索路径 -D 需要预定义的宏 -include 需要强制包含的文件

// 例如：cxxclean -clean d:/a/b/hello/ -- -I ../../ -I ../ -I ./ -D DEBUG -D WIN32 -include platform.h
// （-I、-D、-include均可使用多次）
```

## 如何将屏幕输出存入文件中

如果想把屏幕输出重定向到某个文件，可以使用重定向符号>（windows及linux均可采用此方法）
```
cxxclean -clean d:/vs2005/hello.vcproj -- > cxxclean_hello.log
// 屏幕打印将被存入当前文件夹下的cxxclean_hello.log文件
```
## 示例工程

本项目中的hello文件夹下含有示例工程hello.vcproj，是一个visual studio 2008项目，可以正常编译通过，可用于测试cxx-clean-include是否正常工作

使用方式：下载hello文件夹，在命令行中执行以下命令

```
cxxclean -clean hello.vcxproj -- > cxxclean_hello.log
```

打开cxxclean_hello.log查看执行后的清理效果

## 注意事项

由于cxx-clean-include会对c++源文件进行覆盖，所以请在使用前备份c++代码以防不必要的麻烦。

（cxx-clean-include仅会改动#include语句所在的行，并不会对其余的行作任何改动，但安全起见，最好在使用前进行备份）

## cxx-clean-include的命令行参数

在命令行中输入cxxclean -help可获取详细的命令行参数信息

cxx-clean-include提供以下选项：

```

  -clean=<string> - 该选项可以用来：
                        1. 清理指定的文件夹, 例如: cxxclean -clean ../hello/，将清理../hello/文件夹（包括子文件夹）下的c++文件
                        2. 清理指定的visual studio项目(vs2005版本及以上): 例如: cxxclean -clean ./hello.vcproj 或 cxxclean -clean ./hello.vcxproj，该命令将清理hello项目中的所有c++文件

  -no             - 即no overwrite的首字母缩写, 当传入此参数时，所有的c++文件将不会被改动，本工具仅执行分析并打印分析结果，但不改动c++文件

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