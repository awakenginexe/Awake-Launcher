# SPDX-License-Identifier: GPL-3.0-only
find_program(AWAKE_NPM_EXECUTABLE NAMES npm.cmd npm REQUIRED)
set(AWAKE_WEB_DIR "${CMAKE_SOURCE_DIR}/launcher/awake-web")
set(AWAKE_WEB_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/awake-web")
find_file(AWAKE_WEBCHANNEL_JS qwebchannel.js
    PATHS "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_DATA}/qt6/webchannel"
          "${QT6_INSTALL_PREFIX}/share/qt6/webchannel"
          "${QT6_INSTALL_PREFIX}/share/qt/webchannel"
    REQUIRED NO_DEFAULT_PATH)
file(GLOB_RECURSE AWAKE_WEB_INPUTS CONFIGURE_DEPENDS
    "${AWAKE_WEB_DIR}/src/*" "${AWAKE_WEB_DIR}/public/*")
set(AWAKE_WEB_QRC "${AWAKE_WEB_OUTPUT}/awake-web.qrc")
set(AWAKE_WEB_CPP "${AWAKE_WEB_OUTPUT}/qrc_awake_web.cpp")
add_custom_command(
    OUTPUT "${AWAKE_WEB_DIR}/node_modules/.awake-installed"
    COMMAND "${AWAKE_NPM_EXECUTABLE}" ci --no-audit --no-fund
    COMMAND ${CMAKE_COMMAND} -E touch "${AWAKE_WEB_DIR}/node_modules/.awake-installed"
    WORKING_DIRECTORY "${AWAKE_WEB_DIR}"
    DEPENDS "${AWAKE_WEB_DIR}/package.json" "${AWAKE_WEB_DIR}/package-lock.json"
    COMMENT "Installing locked Awake frontend build dependencies"
    VERBATIM)
add_custom_command(
    OUTPUT "${AWAKE_WEB_CPP}"
    COMMAND ${CMAKE_COMMAND} -E env "AWAKE_WEB_OUT_DIR=${AWAKE_WEB_OUTPUT}/dist"
        "${AWAKE_NPM_EXECUTABLE}" run build
    COMMAND ${CMAKE_COMMAND}
        "-DWEB_DIST=${AWAKE_WEB_OUTPUT}/dist" "-DWEB_QRC=${AWAKE_WEB_QRC}"
        "-DWEBCHANNEL_JS=${AWAKE_WEBCHANNEL_JS}"
        -P "${CMAKE_SOURCE_DIR}/cmake/AwakeWebResources.cmake"
    COMMAND Qt6::rcc -name awake_web -o "${AWAKE_WEB_CPP}" "${AWAKE_WEB_QRC}"
    WORKING_DIRECTORY "${AWAKE_WEB_DIR}"
    DEPENDS ${AWAKE_WEB_INPUTS} "${AWAKE_WEB_DIR}/index.html"
        "${AWAKE_WEB_DIR}/vite.config.ts" "${AWAKE_WEB_DIR}/tsconfig.json"
        "${AWAKE_WEB_DIR}/package.json" "${AWAKE_WEB_DIR}/package-lock.json"
        "${AWAKE_WEB_DIR}/node_modules/.awake-installed"
        "${CMAKE_SOURCE_DIR}/cmake/AwakeWebResources.cmake" "${AWAKE_WEBCHANNEL_JS}"
    COMMENT "Building and embedding local Awake Vue assets"
    VERBATIM)
file(GLOB AWAKE_WEB_NATIVE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/awake/web/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/awake/web/*.h")
set(AWAKE_WEB_SOURCES ${AWAKE_WEB_NATIVE_SOURCES} "${AWAKE_WEB_CPP}")
