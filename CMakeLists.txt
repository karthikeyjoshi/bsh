cmake_minimum_required(VERSION 3.15)
project(bsh VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SQLITECPP_INTERNAL_SQLITE ON CACHE BOOL "" FORCE)
add_compile_definitions(SQLITE_ENABLE_FTS5)

include(FetchContent)
FetchContent_Declare(SQLiteCpp GIT_REPOSITORY https://github.com/SRombauts/SQLiteCpp.git GIT_TAG 3.3.1)
FetchContent_MakeAvailable(SQLiteCpp)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBGIT2 REQUIRED IMPORTED_TARGET libgit2) 

add_executable(bsh-daemon
    src/daemon.cpp
    src/db.cpp
    src/git_utils.cpp
)
target_link_libraries(bsh-daemon PRIVATE SQLiteCpp PkgConfig::LIBGIT2)