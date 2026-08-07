cmake_minimum_required(VERSION 3.24)

project(RocketVolley VERSION 0.8 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

find_package(raylib 6.0 QUIET CONFIG)
if(NOT TARGET raylib)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        raylib
        URL https://github.com/raysan5/raylib/archive/refs/tags/6.0.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP FALSE
    )
    FetchContent_MakeAvailable(raylib)
endif()

find_package(Jolt QUIET CONFIG)
if(NOT TARGET Jolt AND NOT TARGET Jolt::Jolt)
    set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
    set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
    set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
    set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
    set(TARGET_VIEWER OFF CACHE BOOL "" FORCE)
    set(GENERATE_DEBUG_SYMBOLS OFF CACHE BOOL "" FORCE)
    set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
    set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)
    set(USE_SSE4_1 OFF CACHE BOOL "" FORCE)
    set(USE_SSE4_2 OFF CACHE BOOL "" FORCE)
    set(USE_AVX OFF CACHE BOOL "" FORCE)
    set(USE_AVX2 OFF CACHE BOOL "" FORCE)
    set(USE_AVX512 OFF CACHE BOOL "" FORCE)
    set(USE_LZCNT OFF CACHE BOOL "" FORCE)
    set(USE_TZCNT OFF CACHE BOOL "" FORCE)
    set(USE_F16C OFF CACHE BOOL "" FORCE)
    set(USE_FMADD OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        Jolt
        URL https://github.com/jrouwe/JoltPhysics/archive/refs/tags/v5.5.0.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP FALSE
        SOURCE_SUBDIR Build
    )
    FetchContent_MakeAvailable(Jolt)
endif()

add_executable(RocketVolley
    src/main.cpp
    src/Game.cpp
    src/MultiplayerProtocol.cpp
    src/PhysicsWorld.cpp
)

target_include_directories(RocketVolley PRIVATE include)
target_compile_definitions(RocketVolley PRIVATE ROCKET_VOLLEY_VERSION="${PROJECT_VERSION}")

if(TARGET Jolt::Jolt)
    set(ROCKET_VOLLEY_JOLT_TARGET Jolt::Jolt)
else()
    set(ROCKET_VOLLEY_JOLT_TARGET Jolt)
endif()

target_link_libraries(RocketVolley PRIVATE raylib ${ROCKET_VOLLEY_JOLT_TARGET})

include(CTest)
if(BUILD_TESTING)
    add_test(NAME rocket_volley_smoke COMMAND RocketVolley --smoke-test)
    set_tests_properties(rocket_volley_smoke PROPERTIES TIMEOUT 60)
    add_test(NAME rocket_volley_protocol COMMAND RocketVolley --protocol-test)
    set_tests_properties(rocket_volley_protocol PROPERTIES TIMEOUT 10)
endif()

if(MSVC)
    target_compile_options(RocketVolley PRIVATE /W4 /permissive- /EHsc)
    target_compile_definitions(RocketVolley PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(RocketVolley PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    set_target_properties(RocketVolley PROPERTIES WIN32_EXECUTABLE $<CONFIG:Release>)
endif()

install(TARGETS RocketVolley RUNTIME DESTINATION . COMPONENT Game)
install(FILES README.md DESTINATION . COMPONENT Game)
install(DIRECTORY docs/images DESTINATION docs COMPONENT Game)

if(MSVC)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_COMPONENT Game)
    include(InstallRequiredSystemLibraries)
endif()

set(CPACK_GENERATOR "ZIP")
set(CPACK_PACKAGE_NAME "RocketVolley")
set(CPACK_PACKAGE_VENDOR "Rocket Volley")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "RocketVolley-${PROJECT_VERSION}-windows-x64")
set(CPACK_PACKAGE_CHECKSUM "SHA256")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
set(CPACK_COMPONENTS_ALL Game)
set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
set(CPACK_COMPONENTS_GROUPING ALL_COMPONENTS_IN_ONE)
include(CPack)

add_custom_target(package_windows
    COMMAND "${CMAKE_CPACK_COMMAND}" -G ZIP --config "${CMAKE_BINARY_DIR}/CPackConfig.cmake"
    DEPENDS RocketVolley
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMMENT "Creating portable Rocket Volley Windows ZIP"
)
