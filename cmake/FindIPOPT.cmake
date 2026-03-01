# FindIPOPT.cmake
# ----------------
# Find the IPOPT (Interior Point OPTimizer) library.
#
# IPOPT is an open-source software package for large-scale nonlinear optimization.
# https://github.com/coin-or/Ipopt
#
# This module tries pkg-config first (standard on Linux), then falls back to
# manual search using IPOPT_ROOT_DIR.
#
# The following variables are set upon success:
#   IPOPT_FOUND        - True if IPOPT was found
#   IPOPT_INCLUDE_DIRS - Include directories for IPOPT headers
#   IPOPT_LIBRARIES    - Libraries to link against
#   IPOPT_DEFINITIONS  - Compiler definitions required by IPOPT

# --------------------------------------------------------------------------
# Strategy 1: Try pkg-config (standard on Linux when IPOPT is installed
# via a package manager or built with --prefix)
# --------------------------------------------------------------------------
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(_IPOPT_PKG QUIET ipopt)
endif()

if(_IPOPT_PKG_FOUND)
    set(IPOPT_INCLUDE_DIRS ${_IPOPT_PKG_INCLUDE_DIRS})
    set(IPOPT_LIBRARIES    ${_IPOPT_PKG_LIBRARIES})
    set(IPOPT_DEFINITIONS  ${_IPOPT_PKG_CFLAGS_OTHER})

    # pkg-config may return library names without full paths; add the
    # library directories so the linker can find them.
    if(_IPOPT_PKG_LIBRARY_DIRS)
        link_directories(${_IPOPT_PKG_LIBRARY_DIRS})
    endif()

    message(STATUS "IPOPT found via pkg-config")
    message(STATUS "  IPOPT include dirs: ${IPOPT_INCLUDE_DIRS}")
    message(STATUS "  IPOPT libraries:    ${IPOPT_LIBRARIES}")
else()
    # --------------------------------------------------------------------------
    # Strategy 2: Manual search using IPOPT_ROOT_DIR
    # --------------------------------------------------------------------------

    # Build a list of candidate include directories
    set(_IPOPT_INCLUDE_SEARCH_DIRS "")
    if(IPOPT_ROOT_DIR)
        list(APPEND _IPOPT_INCLUDE_SEARCH_DIRS
            ${IPOPT_ROOT_DIR}/include
            ${IPOPT_ROOT_DIR}/include/coin
            ${IPOPT_ROOT_DIR}/include/coin-or
        )
    endif()

    # Search for the main IPOPT header
    find_path(IPOPT_INCLUDE_DIR
        NAMES
            IpIpoptApplication.hpp
        HINTS
            ${_IPOPT_INCLUDE_SEARCH_DIRS}
        PATH_SUFFIXES
            coin
            coin-or
            coin/Ipopt
            coin-or/Ipopt
    )

    # Build a list of candidate library directories
    set(_IPOPT_LIB_SEARCH_DIRS "")
    if(IPOPT_ROOT_DIR)
        list(APPEND _IPOPT_LIB_SEARCH_DIRS
            ${IPOPT_ROOT_DIR}/lib
            ${IPOPT_ROOT_DIR}/lib64
        )
    endif()

    # Search for the IPOPT library
    find_library(IPOPT_LIBRARY
        NAMES
            ipopt
            libipopt
            ipopt-3
            ipopt-0
        HINTS
            ${_IPOPT_LIB_SEARCH_DIRS}
    )

    # Set output variables
    if(IPOPT_INCLUDE_DIR)
        set(IPOPT_INCLUDE_DIRS ${IPOPT_INCLUDE_DIR})
    endif()
    if(IPOPT_LIBRARY)
        set(IPOPT_LIBRARIES ${IPOPT_LIBRARY})
    endif()
    set(IPOPT_DEFINITIONS "")

    if(IPOPT_INCLUDE_DIR AND IPOPT_LIBRARY)
        message(STATUS "IPOPT found via manual search")
        message(STATUS "  IPOPT include dir: ${IPOPT_INCLUDE_DIRS}")
        message(STATUS "  IPOPT library:     ${IPOPT_LIBRARIES}")
    endif()
endif()

# --------------------------------------------------------------------------
# Standard find_package handling
# --------------------------------------------------------------------------
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(IPOPT
    REQUIRED_VARS IPOPT_LIBRARIES IPOPT_INCLUDE_DIRS
    FAIL_MESSAGE "Could NOT find IPOPT. Set IPOPT_ROOT_DIR to the IPOPT installation prefix."
)

mark_as_advanced(IPOPT_INCLUDE_DIR IPOPT_LIBRARY)
