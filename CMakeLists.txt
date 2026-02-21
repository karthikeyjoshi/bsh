cmake_minimum_required(VERSION 3.15)
project(bsh VERSION 0.2.8 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_definitions(SQLITE_ENABLE_FTS5)

option(USE_SYSTEM_SQLITECPP "Use system SQLiteCpp package" OFF)

if(USE_SYSTEM_SQLITECPP)
    find_package(SQLiteCpp REQUIRED)
else()
    set(SQLITECPP_INTERNAL_SQLITE ON CACHE BOOL "" FORCE)
    include(FetchContent)
    FetchContent_Declare(SQLiteCpp GIT_REPOSITORY https://github.com/SRombauts/SQLiteCpp.git GIT_TAG 3.3.1)
    FetchContent_MakeAvailable(SQLiteCpp)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBGIT2 REQUIRED IMPORTED_TARGET libgit2) 

add_executable(bsh-daemon
    src/daemon.cpp
    src/db.cpp
    src/git_utils.cpp
)
target_link_libraries(bsh-daemon PRIVATE SQLiteCpp PkgConfig::LIBGIT2)

include(GNUInstallDirs)
install(TARGETS bsh-daemon 
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(FILES scripts/bsh_init.zsh 
        DESTINATION ${CMAKE_INSTALL_DATADIR}/bsh)

set(CPACK_PACKAGE_NAME "bsh")
set(CPACK_PACKAGE_VENDOR "Karthikey Joshi")
set(CPACK_PACKAGE_CONTACT "karthikey.cse@gmail.com")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "High-performance, Git-aware shell history middleware")

set(CPACK_DEBIAN_PACKAGE_DEPENDS "libgit2-dev, libsqlite3-dev, python3")

set(CPACK_GENERATOR "DEB")
include(CPack)