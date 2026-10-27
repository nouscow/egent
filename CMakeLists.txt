cmake_minimum_required(VERSION 3.19)
project(egent LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Network Sql LinguistTools Svg)

qt_standard_project_setup()

# ============================================================
# 核心静态库
# ------------------------------------------------------------
# 所有与界面无关的逻辑都放这里。新增 core 文件只改这一处，
# 应用与三个测试程序统一链接它，避免「加了文件忘记同步某个 target」。
# ============================================================
qt_add_library(egent_core STATIC
    core/models.h
    core/models.cpp
    core/sseparser.h
    core/sseparser.cpp
    core/sessionstore.h
    core/sessionstore.cpp
    core/providers/imodelprovider.h
    core/providers/openaicompatprovider.h
    core/providers/openaicompatprovider.cpp
    core/providers/providerregistry.h
    core/providers/providerregistry.cpp
    core/agentloop.h
    core/agentloop.cpp

    # Agent 工具（M5 只读）
    core/tools/itool.h
    core/tools/pathguard.h
    core/tools/pathguard.cpp
    core/tools/readonlytools.h
    core/tools/readonlytools.cpp
    core/tools/toolregistry.h
    core/tools/toolregistry.cpp
    core/tools/runcommandtool.h
    core/tools/runcommandtool.cpp
    core/tools/writefiletool.h
    core/tools/writefiletool.cpp
)
target_include_directories(egent_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(egent_core PUBLIC Qt::Core Qt::Network Qt::Sql)

# ============================================================
# 应用
# ============================================================
qt_add_executable(egent
    WIN32 MACOSX_BUNDLE
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui

    # 界面组件：头像 + 气泡 + 会话列表 + 设置
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
        resources/icons/check.svg
        resources/icons/send.svg
        resources/icons/stop.svg
        resources/icons/settings.svg
)

qt_add_translations(
    TARGETS egent
    TS_FILES egent_zh_CN.ts
)

target_link_libraries(egent
    PRIVATE
        egent_core
        Qt::Widgets
        Qt::Svg
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

# ============================================================
# 测试程序
# ============================================================

# 主题模块（console，不依赖 GUI，也不依赖 core）
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

# M1 验收：真实调用百炼接口（需要 DASHSCOPE_API_KEY）
qt_add_executable(provider_live_test tests/provider_live_test.cpp)
target_link_libraries(provider_live_test PRIVATE egent_core)

# 会话存储（真实 SQLite，临时目录）
qt_add_executable(session_test tests/session_test.cpp)
target_link_libraries(session_test PRIVATE egent_core)

# Agent 只读工具（含路径越界拦截）
qt_add_executable(tool_test tests/tool_test.cpp)
target_link_libraries(tool_test PRIVATE egent_core)

# 命令执行工具（黑名单 / 工作区约束 / 退出码）
qt_add_executable(cmd_test tests/cmd_test.cpp)
target_link_libraries(cmd_test PRIVATE egent_core)

# Agent 端到端：真实模型 + 工具循环（需要 DASHSCOPE_API_KEY）
qt_add_executable(agent_test tests/agent_test.cpp)
target_link_libraries(agent_test PRIVATE egent_core)
