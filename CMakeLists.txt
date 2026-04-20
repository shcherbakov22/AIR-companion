cmake_minimum_required(VERSION 3.25)

project(AIRCompanion VERSION 0.1.18 LANGUAGES CXX)

set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(MINGW)
    add_link_options(-static -static-libgcc -static-libstdc++)
endif()

if(WIN32)
    include(FetchContent)

    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(WITH_TESTS OFF CACHE BOOL "" FORCE)
    set(WITH_ZLIB OFF CACHE BOOL "" FORCE)
    set(WITH_OPENSSL OFF CACHE BOOL "" FORCE)
    set(WITH_GNUTLS OFF CACHE BOOL "" FORCE)
    set(WITH_GCRYPT OFF CACHE BOOL "" FORCE)
    set(WITH_SASL OFF CACHE BOOL "" FORCE)
    set(WITH_FFMPEG OFF CACHE BOOL "" FORCE)
    set(LIBVNCSERVER_WITH_WEBSOCKETS OFF CACHE BOOL "" FORCE)
    set(LIBVNCSERVER_INSTALL OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        libvncserver
        URL https://github.com/LibVNC/libvncserver/archive/refs/tags/LibVNCServer-0.9.14.tar.gz
    )
    FetchContent_MakeAvailable(libvncserver)
endif()

enable_testing()

set(COMPANION_CORE_SOURCES
    src/networking/HttpClient.cpp
    src/networking/CompanionApiClient.cpp
    src/networking/CompanionApiParsers.cpp
    src/core/Agent.cpp
    src/core/CaptureScheduler.cpp
    src/core/CommandPoller.cpp
    src/core/EnforcementCoordinator.cpp
    src/core/PolicySync.cpp
    src/core/PushUpStationCoordinator.cpp
    src/core/UplinkSync.cpp
    src/service/Bootstrap.cpp
    src/service/BootAutoStartRegistrar.cpp
    src/service/CaptureSettingsStore.cpp
    src/service/CompanionConfigStore.cpp
    src/service/EnrollmentRequestStore.cpp
    src/service/TrustedRootInstaller.cpp
    src/service/UpdateCoordinator.cpp
    src/tray/RemoteControlHelper.cpp
)

if(WIN32)
    list(APPEND COMPANION_CORE_SOURCES
        src/adapters/windows/WindowsAppTrackerAdapter.cpp
        src/adapters/windows/WindowsBrowserDomainAdapter.cpp
        src/adapters/windows/WindowsCameraCaptureAdapter.cpp
        src/adapters/windows/WindowsEnforcementAdapter.cpp
        src/adapters/windows/WindowsNetworkConfigurationAdapter.cpp
        src/adapters/windows/WindowsPushUpCounterAdapter.cpp
        src/adapters/windows/WindowsRemoteAccessAdapter.cpp
        src/adapters/windows/WindowsScreenCaptureAdapter.cpp
        src/adapters/windows/WindowsServiceLifecycleAdapter.cpp
        src/service/ServiceHost.cpp
    )
endif()

add_library(air_companion_core STATIC ${COMPANION_CORE_SOURCES})

target_include_directories(
    air_companion_core
    PUBLIC
        include
)

if(WIN32)
    target_include_directories(
        air_companion_core
        PUBLIC
            ${libvncserver_SOURCE_DIR}
            ${libvncserver_BINARY_DIR}
    )
endif()

target_compile_definitions(air_companion_core
    PUBLIC
        AIR_COMPANION_VERSION="${PROJECT_VERSION}"
)

if(WIN32)
    add_executable(air_companion_service src/service/main.cpp)
    target_link_libraries(air_companion_service PRIVATE air_companion_core)

    add_executable(air_companion_tray src/tray/main.cpp)
    target_link_libraries(air_companion_tray PRIVATE air_companion_core)

    add_executable(air_companion_helper src/helper/main.cpp)
    target_link_libraries(air_companion_helper PRIVATE air_companion_core)

    add_executable(air_companion_updater src/updater/main.cpp)
    target_link_libraries(air_companion_updater PRIVATE air_companion_core)
endif()

if(UNIX AND NOT APPLE)
    add_executable(
        air_companion_linux_screenshot
        src/linux_screenshot_service/main.cpp
        src/adapters/linux/LinuxScreenCaptureAdapter.cpp
    )
    target_link_libraries(air_companion_linux_screenshot PRIVATE air_companion_core)
endif()

add_executable(air_companion_tests tests/CompanionTests.cpp tests/FakePushUpCounterAdapter.cpp tests/FakeCompanionApiClient.cpp)
target_link_libraries(air_companion_tests PRIVATE air_companion_core)

if(COMPANION_COVERAGE)
    target_compile_options(air_companion_tests PRIVATE --coverage -O0 -g)
    target_link_options(air_companion_tests PRIVATE --coverage)
    target_compile_options(air_companion_core PRIVATE --coverage -O0 -g)
    target_link_options(air_companion_core PRIVATE --coverage)
    message(STATUS "Coverage instrumentation enabled")
endif()

if(WIN32)
    target_compile_definitions(air_companion_core PUBLIC AIR_COMPANION_WINDOWS=1)
    target_link_libraries(air_companion_core PUBLIC vncserver iphlpapi ws2_32 crypt32 gdiplus mf mfplat mfreadwrite mfuuid ole32 user32 advapi32 wtsapi32 userenv d3d11 dxgi dxguid dwmapi winhttp)
endif()

add_test(NAME air_companion_tests COMMAND air_companion_tests)
