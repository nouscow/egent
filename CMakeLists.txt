cmake_minimum_required(VERSION 3.19)
project(egent LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Network Sql LinguistTools)

qt_standard_project_setup()

qt_add_executable(egent
    WIN32 MACOSX_BUNDLE
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui

    # 核心：数据模型 / SSE 解析 / provider 接口与实现
    core/models.h
    core/models.cpp
    core/sseparser.h
    core/sseparser.cpp
    core/sessionstore.h
    core/sessionstore.cpp
    core/providers/imodelprovider.h
    core/providers/bailianprovider.h
    core/providers/bailianprovider.cpp

    # 界面组件：头像 + 气泡
    ui/avatarwidget.h
    ui/avatarwidget.cpp
    ui/avatarstore.h
    ui/avatarstore.cpp
    ui/messagebubble.h
    ui/messagebubble.cpp
    ui/settingsdialog.h
    ui/settingsdialog.cpp
    ui/chatview.h
    ui/chatview.cpp

    # 主题模块（不依赖 QWidget，只吃 QtCore/QtGui）
    theme/themedata.h
    theme/themedata.cpp
    theme/themerepository.h
    theme/themerepository.cpp
    theme/qsstemplate.h
    theme/qsstemplate.cpp
    theme/thememanager.h
    theme/thememanager.cpp
)

# 资源：显式声明（AUTORCC 对单独 .qrc 不够稳，用 qt_add_resources 保证一定编进二进制）
# BASE=resources 让资源路径变成 :/themes/*.json 与 :/qss/app.qss
qt_add_resources(egent "theme_assets"
    PREFIX "/"
    BASE "resources"
    FILES
        resources/themes/dark.json
        resources/themes/light.json
        resources/qss/app.qss
)

qt_add_translations(
    TARGETS egent
    TS_FILES egent_zh_CN.ts
)

target_include_directories(egent PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(egent
    PRIVATE
        Qt::Core
        Qt::Widgets
        Qt::Network
        Qt::Sql
)

install(TARGETS egent
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
    TARGET egent
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})

# ---------------- 主题模块自测（console，不依赖 GUI） ----------------
qt_add_executable(theme_selftest
    tests/theme_selftest.cpp
    theme/themedata.h
    theme/themedata.cpp
    theme/qsstemplate.h
    theme/qsstemplate.cpp
)
target_include_directories(theme_selftest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(theme_selftest PRIVATE
    PROJECT_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(theme_selftest PRIVATE Qt::Core Qt::Gui)

# ---------------- M1 验收：真实调用百炼接口（需要 DASHSCOPE_API_KEY） ----------------
qt_add_executable(provider_live_test
    tests/provider_live_test.cpp
    core/models.h
    core/models.cpp
    core/sseparser.h
    core/sseparser.cpp
    core/providers/imodelprovider.h
    core/providers/bailianprovider.h
    core/providers/bailianprovider.cpp
)
target_include_directories(provider_live_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(provider_live_test PRIVATE Qt::Core Qt::Network)

# ---------------- 会话存储自测（真实 SQLite，临时目录） ----------------
qt_add_executable(session_test
    tests/session_test.cpp
    core/models.h
    core/models.cpp
    core/sessionstore.h
    core/sessionstore.cpp
)
target_include_directories(session_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(session_test PRIVATE Qt::Core Qt::Sql)
