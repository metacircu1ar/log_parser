cmake_minimum_required(VERSION 3.7.2 FATAL_ERROR)

project(LogParser)

set(CMAKE_CXX_STANDARD 17)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

if (CMAKE_CXX_COMPILER_ID MATCHES GNU)
    if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libgcc -static-libstdc++")# -static")
    endif()
endif ()

SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")

add_subdirectory(imgui-cmake)

add_executable(${PROJECT_NAME} src/main.cpp)

file(GLOB_RECURSE SOURCES "src/*")
target_sources(${PROJECT_NAME} PRIVATE ${SOURCES})

target_link_libraries(${PROJECT_NAME} PUBLIC
    IMGUI
)
