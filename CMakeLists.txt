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
# Tworzenie exe
# Dodawanie potrzebnych plików do kompilacji
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
# Warningi kompilatora
# =====================================
if (MSVC)
    target_compile_options(wisielec_server PRIVATE /W4 /permissive-)
else()
    target_compile_options(wisielec_server PRIVATE -Wall -Wextra -Wpedantic)
endif()
