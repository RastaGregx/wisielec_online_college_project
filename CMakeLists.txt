# =====================================
# Minimalna wersja CMake
# =====================================
cmake_minimum_required(VERSION 3.10)

# =====================================
# Nazwa Projektu i język
# =====================================
project(WisielecOnline LANGUAGES CXX)

# =====================================
# Wersja c++
# =====================================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


# =====================================
# SFML install
# =====================================
include(FetchContent)

FetchContent_Declare(
    sfml
    GIT_REPOSITORY https://github.com/SFML/SFML.git
    GIT_TAG 2.6.1
    GIT_SHALLOW TRUE
)

set(FETCHCONTENT_QUIET OFF)
FetchContent_MakeAvailable(sfml)

# =====================================
# Server
# =====================================
add_executable(
    wisielec_server
    src/main.cpp
    src/game.cpp
)

# =====================================
# Headers
# =====================================
target_include_directories(
    wisielec_server
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
)

# =====================================
# Client
# =====================================

add_executable(
    wisielec_client
    src/client.cpp
)

target_include_directories(
    wisielec_client
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
)

# Podlinkuj SFML
target_link_libraries(
    wisielec_client PRIVATE 
    sfml-graphics 
    sfml-window 
    sfml-system)

# =====================================
# Warningi kompilatora
# =====================================
if (MSVC)
    target_compile_options(wisielec_server PRIVATE /W4 /permissive-)
else()
    target_compile_options(wisielec_server PRIVATE -Wall -Wextra -Wpedantic)
endif()
