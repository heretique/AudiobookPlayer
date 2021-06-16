cmake_minimum_required(VERSION 3.5)

if(COMMAND cmake_policy)
  cmake_policy(SET CMP0003 NEW)
endif(COMMAND cmake_policy)

set(VCPKG_TARGET_TRIPLET x86-windows CACHE STRING "VCPKG Target Triplet to use")
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    file(TO_CMAKE_PATH "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CMAKE_TOOLCHAIN_FILE)
endif()


project(AudiobookPlayer LANGUAGES CXX C)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/hq)


set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(AudiobookPlayer
    imFileBroser.cpp
    main.cpp
    AudiobookPlayer.cpp)

find_package(imgui CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)

find_library(VLC_LIBRARY libvlc PATHS ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/vlc/lib)
find_path(VLC_INCLUDE vlc/libvlc.h PATHS ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/vlc/include)

message(${VLC_INCLUDE})
message(${VLC_LIBRARY})


target_link_libraries(AudiobookPlayer PRIVATE
    hq
    imgui::imgui
    glad::glad
    glfw
    unofficial::sqlite3::sqlite3
    ${VLC_LIBRARY}
    )

target_include_directories(AudiobookPlayer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty
    ${VLC_INCLUDE})

add_custom_command(TARGET AudiobookPlayer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/vlc/bin/libvlc.dll
    $<TARGET_FILE_DIR:AudiobookPlayer>

    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/vlc/bin/libvlccore.dll
    $<TARGET_FILE_DIR:AudiobookPlayer>

    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_CURRENT_SOURCE_DIR}/generic_cover.png
    $<TARGET_FILE_DIR:AudiobookPlayer>

    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/vlc/plugins
    $<TARGET_FILE_DIR:AudiobookPlayer>/plugins

    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/fonts
    $<TARGET_FILE_DIR:AudiobookPlayer>/fonts
    )
