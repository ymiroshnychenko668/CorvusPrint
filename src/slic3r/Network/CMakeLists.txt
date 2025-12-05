# Network communication library for slicer
# Provides MQTT and HTTP interfaces for remote control

# Ensure we can find our custom modules
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/modules")

# Find Mosquitto first to determine what sources to compile
find_package(Mosquitto)

# Base sources (always compiled)
set(SLIC3R_NETWORK_SOURCES
    HttpSlicerApi.cpp
)

set(SLIC3R_NETWORK_HEADERS
    HttpSlicerApi.hpp
)

# Add MQTT sources only if Mosquitto is found
if(Mosquitto_FOUND)
    list(APPEND SLIC3R_NETWORK_SOURCES MqttEventSink.cpp)
    list(APPEND SLIC3R_NETWORK_HEADERS MqttEventSink.hpp)
    list(APPEND SLIC3R_NETWORK_SOURCES MqttConfigPublisher.cpp)
    list(APPEND SLIC3R_NETWORK_HEADERS MqttConfigPublisher.hpp)
endif()

add_library(slic3r_network STATIC
    ${SLIC3R_NETWORK_SOURCES}
    ${SLIC3R_NETWORK_HEADERS}
)

target_include_directories(slic3r_network PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/slic3r
)

# Link dependencies
target_link_libraries(slic3r_network PUBLIC
    libslic3r
)

# Mosquitto MQTT library
if(Mosquitto_FOUND)
    target_link_libraries(slic3r_network PUBLIC Mosquitto::mosquitto)
    target_compile_definitions(slic3r_network PUBLIC SLIC3R_HAS_MOSQUITTO=1)
    message(STATUS "Network: MQTT support enabled via Mosquitto")
else()
    message(WARNING "Mosquitto not found - MQTT support disabled")
    target_compile_definitions(slic3r_network PUBLIC SLIC3R_HAS_MOSQUITTO=0)
endif()

# cpp-httplib (header-only)
# Either fetch or find system installation
if(EXISTS "${CMAKE_SOURCE_DIR}/deps/cpp-httplib/httplib.h")
    target_include_directories(slic3r_network PRIVATE
        ${CMAKE_SOURCE_DIR}/deps/cpp-httplib
    )
    target_compile_definitions(slic3r_network PUBLIC SLIC3R_HAS_HTTPLIB=1)
else()
    # Try to find via FetchContent
    include(FetchContent)
    FetchContent_Declare(
        cpp-httplib
        GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
        GIT_TAG v0.15.3
    )
    FetchContent_MakeAvailable(cpp-httplib)
    target_link_libraries(slic3r_network PRIVATE httplib::httplib)
    target_compile_definitions(slic3r_network PUBLIC SLIC3R_HAS_HTTPLIB=1)
endif()

# OpenSSL for HTTPS support (optional)
find_package(OpenSSL)
if(OpenSSL_FOUND)
    target_link_libraries(slic3r_network PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    target_compile_definitions(slic3r_network PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)
endif()
