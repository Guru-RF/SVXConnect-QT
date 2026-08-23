# SPDX-License-Identifier: MIT
# SVXConnect-Debian — Copyright (c) 2026 Diëlectricum BV
#
# Derives the version shown in About, in the log header and in --version.
#
# `git describe` when we are in a git checkout with a tag, the project() version
# otherwise (which is what a Debian source package gets, since dpkg-source
# strips .git). Never fails the build over it.

function(svx_resolve_version OUTVAR)
    set(_v "${PROJECT_VERSION}")
    set(_src "project()")

    find_package(Git QUIET)
    if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" describe --tags --always --dirty
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            OUTPUT_VARIABLE _describe
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE _rc)
        if(_rc EQUAL 0 AND _describe)
            set(_v "${_describe}")
            set(_src "git describe")
        endif()
    endif()

    set(${OUTVAR} "${_v}" PARENT_SCOPE)
    message(STATUS "svxconnect-qt version: ${_v}  (from ${_src})")
endfunction()
