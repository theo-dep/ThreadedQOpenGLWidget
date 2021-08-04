cmake_minimum_required(VERSION 3.15)
cmake_policy(SET CMP0091 NEW)

project(ThreadedQOpenGLWidget)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(CMAKE_VERSION VERSION_LESS "3.7.0")
    set(CMAKE_INCLUDE_CURRENT_DIR ON)
endif()

find_package(Qt5 COMPONENTS Widgets REQUIRED)

set(TARGET ${CMAKE_PROJECT_NAME})

set(SOURCES
    main.cpp
    MainWindow.cpp
    GLWidget.cpp
)

add_executable(${TARGET} ${SOURCES})

target_link_libraries(${TARGET} Qt5::Core Qt5::Gui Qt5::Widgets)

set(QT_STATIC ON)
if(QT_STATIC)

    set(QT_LIB_DIR "${Qt5_DIR}/../..")
    set(QT_PLUGIN_DIR "${Qt5_DIR}/../../../plugins")

    if(MSVC)
        set_property(TARGET ${TARGET} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()

    set(DEBUG_SUFFIX $<$<CONFIG:Debug>:d>)

    target_link_libraries(${TARGET} 
        ${QT_LIB_DIR}/qtmain${DEBUG_SUFFIX}.lib
        ${QT_LIB_DIR}/qtfreetype${DEBUG_SUFFIX}.lib
        ${QT_LIB_DIR}/qtpcre2${DEBUG_SUFFIX}.lib
        ${QT_LIB_DIR}/qtlibpng${DEBUG_SUFFIX}.lib
        ${QT_LIB_DIR}/qtharfbuzz${DEBUG_SUFFIX}.lib

        Qt5::QWindowsIntegrationPlugin 
        ${QT_LIB_DIR}/Qt5EventDispatcherSupport${DEBUG_SUFFIX}.lib
        ${QT_LIB_DIR}/Qt5WindowsUiAutomationSupport${DEBUG_SUFFIX}.lib
        ${QT_LIB_DIR}/Qt5ThemeSupport${DEBUG_SUFFIX}.lib
        ${QT_LIB_DIR}/Qt5FontDatabaseSupport${DEBUG_SUFFIX}.lib
    )

endif()

if(MSVC)
    list(APPEND SYSTEM_DEFINES $<$<CONFIG:Debug>:-D_ITERATOR_DEBUG_LEVEL=0>)

    target_link_libraries(${TARGET} Ws2_32 Netapi32 Version UserEnv Winmm Dwmapi Imm32 Wtsapi32 OpenGL32)
endif()
