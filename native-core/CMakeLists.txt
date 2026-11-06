# SPDX-License-Identifier: GPL-3.0-only
cmake_minimum_required(VERSION 3.25)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(AwakeNativeCore VERSION 0.1.0 LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 23)
    set(CMAKE_AUTOMOC ON)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Core Network)
    include(CTest)
endif()
if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Awake native core currently supports Windows x64 only")
endif()
get_filename_component(AWAKE_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(INTEGRATION "${AWAKE_SOURCE_ROOT}/launcher/awake/integration")
add_library(AwakeCore STATIC
    ${INTEGRATION}/Protocol.cpp ${INTEGRATION}/InstanceDiscovery.cpp
    ${INTEGRATION}/IntegrationServer.cpp ${INTEGRATION}/ProfileAccess.cpp
    ${INTEGRATION}/OperationStore.cpp ${INTEGRATION}/DurableIO.cpp
    ${INTEGRATION}/InstanceAuthority.cpp ${INTEGRATION}/PhysicalInstanceLease.cpp
    ${INTEGRATION}/GameProcessGroup.cpp ${INTEGRATION}/TransactionEngine.cpp)
target_compile_features(AwakeCore PUBLIC cxx_std_23)
target_include_directories(AwakeCore PUBLIC "${AWAKE_SOURCE_ROOT}/launcher")
target_link_libraries(AwakeCore PUBLIC Qt6::Core Qt6::Network advapi32 shell32 ole32 uuid)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR AND BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    foreach(name AwakeIntegration AwakeOperations AwakeTransactions)
        add_executable(${name} "${AWAKE_SOURCE_ROOT}/tests/${name}_test.cpp")
        target_link_libraries(${name} PRIVATE AwakeCore Qt6::Test)
        add_test(NAME ${name} COMMAND ${name})
    endforeach()
endif()
