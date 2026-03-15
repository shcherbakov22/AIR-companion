cmake_minimum_required(VERSION 3.25)

project(AIRCompanion VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(COMPANION_CORE_SOURCES
    src/networking/HttpClient.cpp
    src/networking/CompanionApiClient.cpp
    src/adapters/windows/WindowsAppTrackerAdapter.cpp
    src/adapters/windows/WindowsBrowserDomainAdapter.cpp
    src/adapters/windows/WindowsCameraCaptureAdapter.cpp
    src/adapters/windows/WindowsEnforcementAdapter.cpp
    src/adapters/windows/WindowsNetworkConfigurationAdapter.cpp
    src/adapters/windows/WindowsScreenCaptureAdapter.cpp
    src/adapters/windows/WindowsServiceLifecycleAdapter.cpp
    src/core/Agent.cpp
    src/core/CaptureScheduler.cpp
    src/core/CommandPoller.cpp
    src/core/EnforcementCoordinator.cpp
    src/core/PolicySync.cpp
    src/core/UplinkSync.cpp
    src/service/Bootstrap.cpp
    src/service/CompanionConfigStore.cpp
    src/service/ServiceHost.cpp
    src/tray/TrayApplication.cpp
)

add_library(air_companion_core STATIC ${COMPANION_CORE_SOURCES})

target_include_directories(air_companion_core PUBLIC include)

target_compile_definitions(air_companion_core
    PUBLIC
        AIR_COMPANION_VERSION="${PROJECT_VERSION}"
)

add_executable(air_companion_service src/service/main.cpp)
target_link_libraries(air_companion_service PRIVATE air_companion_core)

add_executable(air_companion_tray src/tray/main.cpp)
target_link_libraries(air_companion_tray PRIVATE air_companion_core)

if(WIN32)
    target_compile_definitions(air_companion_core PUBLIC AIR_COMPANION_WINDOWS=1)
    target_link_libraries(air_companion_core PUBLIC iphlpapi ws2_32)
endif()
