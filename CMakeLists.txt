cmake_minimum_required(VERSION 3.24)

project(PrivateClaw
    VERSION 0.1.1.2
    DESCRIPTION "Desktop app for project-based LLM automation"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

option(PRIVATECLAW_BUILD_TESTS "Build test targets" OFF)
option(PRIVATECLAW_DEPLOY_QT_RUNTIME "Copy Qt runtime files into the build output directory on Windows" ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network Sql Concurrent)

qt_standard_project_setup()

set(PRIVATECLAW_SOURCES
    src/app/main.cpp
    src/app/ApplicationBootstrap.cpp
    src/app/ApplicationBootstrap.h
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    src/ui/ProjectPanel.cpp
    src/ui/ProjectPanel.h
    src/ui/WorkflowPanel.cpp
    src/ui/WorkflowPanel.h
    src/ui/MemoryPanel.cpp
    src/ui/MemoryPanel.h
    src/ui/RunPanel.cpp
    src/ui/RunPanel.h
    src/ui/SchedulePanel.cpp
    src/ui/SchedulePanel.h
    src/ui/RunLogPanel.cpp
    src/ui/RunLogPanel.h
    src/core/WorkflowEngine.cpp
    src/core/WorkflowEngine.h
    src/core/RunContext.h
    src/domain/Project.h
    src/domain/Workflow.h
    src/domain/Run.h
    src/domain/MemoryEntry.h
    src/domain/Schedule.h
    src/providers/ILlmProvider.h
    src/providers/OllamaProvider.cpp
    src/providers/OllamaProvider.h
    src/providers/LmStudioProvider.cpp
    src/providers/LmStudioProvider.h
    src/providers/ProviderManager.cpp
    src/providers/ProviderManager.h
    src/scheduler/SchedulerService.cpp
    src/scheduler/SchedulerService.h
    src/storage/DatabaseManager.cpp
    src/storage/DatabaseManager.h
    src/services/SettingsService.cpp
    src/services/SettingsService.h
    src/services/SecretsService.cpp
    src/services/SecretsService.h
    src/services/ComfyUiMetadataService.cpp
    src/services/ComfyUiMetadataService.h
    src/services/UpdateChecker.cpp
    src/services/UpdateChecker.h
    src/services/ProjectService.cpp
    src/services/ProjectService.h
    src/services/WorkflowService.cpp
    src/services/WorkflowService.h
    src/services/MemoryService.cpp
    src/services/MemoryService.h
    src/services/RunService.cpp
    src/services/RunService.h
    src/services/ScheduleService.cpp
    src/services/ScheduleService.h
    src/tools/ToolExecutor.cpp
    src/tools/ToolExecutor.h
    src/tools/ToolRisk.cpp
    src/tools/ToolRisk.h
    src/ui/ExecutionApproval.cpp
    src/ui/ExecutionApproval.h
    src/utils/Logger.cpp
    src/utils/Logger.h
    resources/resources.qrc
)

qt_add_executable(PrivateClaw
    MANUAL_FINALIZATION
    ${PRIVATECLAW_SOURCES}
)

target_include_directories(PrivateClaw PRIVATE src)

target_compile_definitions(PrivateClaw
    PRIVATE
        PRIVATECLAW_APP_VERSION="${PROJECT_VERSION}"
        PRIVATECLAW_GITHUB_RELEASE_API_URL="https://api.github.com/repos/UbivisMedia/PrivateClaw/releases/latest"
        PRIVATECLAW_GITHUB_RELEASES_URL="https://github.com/UbivisMedia/PrivateClaw/releases/latest"
)

target_link_libraries(PrivateClaw
    PRIVATE
        Qt6::Widgets
        Qt6::Network
        Qt6::Sql
        Qt6::Concurrent
)

if(WIN32)
    set_target_properties(PrivateClaw PROPERTIES WIN32_EXECUTABLE TRUE)
    target_link_libraries(PrivateClaw PRIVATE Crypt32)

    if(PRIVATECLAW_DEPLOY_QT_RUNTIME)
        get_target_property(PRIVATECLAW_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
        get_filename_component(PRIVATECLAW_QT_BIN_DIR "${PRIVATECLAW_QMAKE_EXECUTABLE}" DIRECTORY)
        find_program(
            PRIVATECLAW_WINDEPLOYQT_EXECUTABLE
            NAMES windeployqt
            HINTS "${PRIVATECLAW_QT_BIN_DIR}"
            REQUIRED
        )

        add_custom_command(
            TARGET PrivateClaw
            POST_BUILD
            COMMAND
                "${PRIVATECLAW_WINDEPLOYQT_EXECUTABLE}"
                --dir "$<TARGET_FILE_DIR:PrivateClaw>"
                --no-translations
                "$<IF:$<CONFIG:Debug>,--debug,--release>"
                "$<TARGET_FILE:PrivateClaw>"
            COMMENT "Deploying Qt runtime dependencies to the build output directory"
            VERBATIM
        )
    endif()
endif()

if(PRIVATECLAW_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

qt_finalize_executable(PrivateClaw)
