cmake_minimum_required(VERSION 3.20)
project(safetensors VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# Dependencies
find_package(cum REQUIRED)

# Options
option(BUILD_SHARED_LIBS "Build libraries as shared" OFF)
option(SAFETENSORS_BUILD_EXAMPLES "Build example programs" ON)
option(SAFETENSORS_BUILD_TESTS "Build tests" ON)

# Library target
add_library(safetensors src/safetensors.cpp)
add_library(safetensors::safetensors ALIAS safetensors)
add_library(safetensors_cpp ALIAS safetensors)
add_library(safetensors::safetensors_cpp ALIAS safetensors)

target_include_directories(safetensors
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(safetensors
    PUBLIC
        cum::cum
)

set_target_properties(safetensors PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
    EXPORT_NAME safetensors
)

# Examples
if(SAFETENSORS_BUILD_EXAMPLES)
    add_executable(basic_usage examples/basic_usage.cpp)
    target_link_libraries(basic_usage PRIVATE safetensors::safetensors)
endif()

# Tests
if(SAFETENSORS_BUILD_TESTS)
    enable_testing()
    if(TARGET basic_usage)
        add_test(NAME basic_usage COMMAND basic_usage)
    endif()
endif()

# Installation rules
install(TARGETS safetensors
    EXPORT safetensorsTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

set(SAFETENSORS_CONFIG_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/safetensors")

install(EXPORT safetensorsTargets
    FILE safetensorsTargets.cmake
    NAMESPACE safetensors::
    DESTINATION "${SAFETENSORS_CONFIG_INSTALL_DIR}"
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/safetensorsConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/safetensorsConfig.cmake"
    INSTALL_DESTINATION "${SAFETENSORS_CONFIG_INSTALL_DIR}"
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/safetensorsConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/safetensorsConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/safetensorsConfigVersion.cmake"
    DESTINATION "${SAFETENSORS_CONFIG_INSTALL_DIR}"
)