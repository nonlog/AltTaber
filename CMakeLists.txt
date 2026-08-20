cmake_minimum_required(VERSION 3.24)
project(Win_Switcher VERSION 0.6.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Keep the original local Qt layout as a fallback, but don't override CI/toolchain paths.
if(NOT CMAKE_PREFIX_PATH AND EXISTS "D:/Qt/6.8.1/msvc2022_64/lib/cmake")
    list(APPEND CMAKE_PREFIX_PATH "D:/Qt/6.8.1/msvc2022_64/lib/cmake")
endif()
list(APPEND CMAKE_AUTOUIC_SEARCH_PATHS ui)

set(VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(VERSION_PATCH ${PROJECT_VERSION_PATCH})

set(COMPANY_NAME "nonlog")
set(FILE_DESCRIPTION "AltTaber - Window Switcher with DWM thumbnails")
set(PRODUCT_NAME "AltTaber")
set(ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/img/icon.ico")

set(VERSIONINFO_RC "${CMAKE_CURRENT_BINARY_DIR}/VersionInfo.rc")
configure_file(
        VersionInfo.rc.in
        ${VERSIONINFO_RC}
        @ONLY
)

include_directories(header)

find_package(Qt6 COMPONENTS
        Core
        Gui
        Widgets
        Xml
        Network
        REQUIRED)

add_executable(Win_Switcher src/main.cpp
        ${VERSIONINFO_RC}
        res.qrc
        src/widget.cpp
        header/widget.h
        ui/widget.ui
        src/utils/winEventHook.cpp
        header/utils/winEventHook.h
        src/utils/Util.cpp
        header/utils/Util.h
        header/utils/setWindowBlur.h
        src/utils/setWindowBlur.cpp
        src/utils/IconOnlyDelegate.cpp
        header/utils/IconOnlyDelegate.h
        header/utils/uiautomation.h
        src/utils/uiautomation.cpp
        src/utils/AppUtil.cpp
        header/utils/AppUtil.h
        src/utils/KeyboardHooker.cpp
        header/utils/KeyboardHooker.h
        src/utils/TaskbarWheelHooker.cpp
        header/utils/TaskbarWheelHooker.h
        src/utils/QtWin.cpp
        header/utils/QtWin.h
        header/utils/ComInitializer.h
        header/utils/SingleApp.h
        header/utils/Startup.h
        header/utils/SystemTray.h
        header/utils/ConfigManager.h
        header/utils/ConfigManagerBase.h
        src/UpdateDialog.cpp
        header/UpdateDialog.h
        ui/UpdateDialog.ui
        src/utils/ScheduledTask.cpp
        header/utils/ScheduledTask.h)

target_link_libraries(Win_Switcher
        Qt::Core
        Qt::Gui
        Qt::Widgets
        Qt::Xml
        Qt::Network
        Dwmapi
        RuntimeObject
)

# UpdateDialog uses Qt's private QZipReader API. The official Qt packages include these headers.
target_include_directories(${PROJECT_NAME} PRIVATE
        ${Qt6Core_PRIVATE_INCLUDE_DIRS}
        ${Qt6Gui_PRIVATE_INCLUDE_DIRS})

set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "AltTaber")
if(CMAKE_BUILD_TYPE MATCHES "Release")
    set_target_properties(${PROJECT_NAME} PROPERTIES WIN32_EXECUTABLE true)
endif()

target_compile_definitions(${PROJECT_NAME} PRIVATE
        QT_DEPRECATED_WARNINGS
        UNICODE
        _UNICODE
        ALTTABER_VERSION="${PROJECT_VERSION}"
)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE "/utf-8")
endif()
