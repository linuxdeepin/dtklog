# DtkLog

Simple, convinient and thread safe logger for Qt-based C++ apps

## Short example

```cpp
#include <QCoreApplication>
#include <QDebug>

#include <dloghelper.h>
#include <ConsoleAppender.h>

DLOG_CORE_USE_NAMESPACE
int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);
    
    auto consoleAppender = new ConsoleAppender;
    consoleAppender->setFormat("[%{type:-7}] <%{Function}> %{message}\n");
    dlogger->registerAppender(consoleAppender);

    dInfo("Starting the application");

    dWarning() << "Something went wrong." << "Result code is" << -1;

    return 0;
}
```

## Adding DtkLog to your project

Add this repo as a git submodule to your project. 


Include it to your CMakeLists.txt file:
```cmake
find_package(DtkLog REQUIRED)
...

...
TARGET_LINK_LIBRARIES(${your_target} ... Dtk::Log)
```

Include `dloghelper.h` and one or several appenders of your choice:
```cpp
#include <dloghelper.h>
#include <ConsoleAppender.h>
```

## Testing

```shell
cmake -S . -B build -DDTK5=OFF -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Use `-DDTK5=ON` to test Qt5. The lifecycle tests cover concurrent singleton
initialization and Qt message callbacks during application shutdown, including
a later-installed handler that forwards to DtkLog. They use the build-tree
library even when build RPATH is disabled.
