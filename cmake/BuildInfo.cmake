include("${BLAZER_SOURCE_DIR}/cmake/Version.cmake")
set(BLAZER_REVISION "source-archive")
if(EXISTS "${BLAZER_SOURCE_DIR}/SOURCE_REVISION")
    file(READ "${BLAZER_SOURCE_DIR}/SOURCE_REVISION" BLAZER_REVISION)
    string(STRIP "${BLAZER_REVISION}" BLAZER_REVISION)
    string(LENGTH "${BLAZER_REVISION}" revision_length)
    if(NOT BLAZER_REVISION MATCHES "^[0-9a-f]+$" OR
       NOT (revision_length EQUAL 40 OR revision_length EQUAL 64))
        message(FATAL_ERROR "SOURCE_REVISION must contain one full Git revision")
    endif()
elseif(EXISTS "${BLAZER_SOURCE_DIR}/.git")
    find_package(Git REQUIRED)
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
        WORKING_DIRECTORY "${BLAZER_SOURCE_DIR}" OUTPUT_VARIABLE BLAZER_REVISION
        OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=normal
        WORKING_DIRECTORY "${BLAZER_SOURCE_DIR}" OUTPUT_VARIABLE dirty
        OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    if(dirty)
        string(APPEND BLAZER_REVISION "-dirty")
    endif()
endif()
# Update the timestamp only when content changes, avoiding a full core rebuild.
file(CONFIGURE OUTPUT "${BLAZER_BINARY_DIR}/generated/blazer_version.h" CONTENT
"#pragma once
#define BLAZER_VERSION \"@BLAZER_RELEASE_LABEL@\"
#define BLAZER_REVISION \"@BLAZER_REVISION@\"
" @ONLY)
