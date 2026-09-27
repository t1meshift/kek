# Cross-compiles for DOS with the DJGPP toolchain (i586-pc-msdosdjgpp-gcc,
# GCC 14.2 from delorie.com's official djcross-gcc RPMs, unpacked rather than
# installed — see BACKLOG.md's Tier 7 DOS platform item for why). Usage:
#
#   cmake -B build-dos -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-djgpp.cmake \
#         -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-dos
#
# KEK_DJGPP_ROOT points at the unpacked RPM tree's usr/ (the one built by
# unpacking djcross-gcc/djcross-binutils/djcrx with bsdtar, not installed
# system-wide): its bin/ holds the i586-pc-msdosdjgpp-* tools, and its own
# lib64/gcc and libexec/gcc trees under that prefix mean the compiler resolves
# its own sysroot without extra -I flags, unlike a bare extracted tarball.
set(KEK_DJGPP_ROOT "$ENV{HOME}/.local/share/kek-dos/root/usr"
    CACHE PATH "DJGPP cross toolchain prefix (its bin/ has i586-pc-msdosdjgpp-gcc)")

# A name CMake has never heard of marks this a cross build (CMAKE_CROSSCOMPILING
# becomes true, try_run results are not executed) without claiming DOS is some
# flavour of an OS CMake already has assumptions about. demo/CMakeLists.txt
# tests this same variable to pick the DOS platform layer over SDL3.
set(CMAKE_SYSTEM_NAME Generic-DJGPP)
set(DJGPP TRUE)

set(CMAKE_C_COMPILER "${KEK_DJGPP_ROOT}/bin/i586-pc-msdosdjgpp-gcc")

set(CMAKE_FIND_ROOT_PATH "${KEK_DJGPP_ROOT}/i586-pc-msdosdjgpp")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# The gcc driver already appends .exe to its link output regardless of -o;
# without this CMake looks for a same-named file with no extension and thinks
# the link step produced nothing.
set(CMAKE_EXECUTABLE_SUFFIX ".exe")
