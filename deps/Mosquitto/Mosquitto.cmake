# Mosquitto MQTT client library
# https://mosquitto.org/
# https://github.com/eclipse-mosquitto/mosquitto

set(_mosquitto_platform_flags
    -DWITH_BROKER:BOOL=OFF           # Only build client library
    -DWITH_APPS:BOOL=OFF             # Don't build mosquitto_pub/sub
    -DWITH_CLIENTS:BOOL=OFF          # Don't build client apps
    -DWITH_PLUGINS:BOOL=OFF          # Don't build plugins
    -DDOCUMENTATION:BOOL=OFF         # Don't build docs
    -DWITH_STATIC_LIBRARIES:BOOL=ON  # Build static library
    -DWITH_PIC:BOOL=ON               # Position independent code
    -DWITH_THREADING:BOOL=ON         # Thread support
    -DWITH_TLS:BOOL=ON               # TLS/SSL support via OpenSSL
    -DWITH_CJSON:BOOL=OFF            # Don't need cJSON
    -DWITH_UNIX_SOCKETS:BOOL=OFF     # Don't need Unix sockets
)

if (WIN32)
    set(_mosquitto_platform_flags
        ${_mosquitto_platform_flags}
        -DWITH_SRV:BOOL=OFF          # No SRV lookup on Windows
    )
elseif (APPLE)
    set(_mosquitto_platform_flags
        ${_mosquitto_platform_flags}
        -DWITH_SRV:BOOL=OFF          # No SRV lookup on macOS
        -DCMAKE_OSX_DEPLOYMENT_TARGET:STRING=${DEP_OSX_TARGET}
    )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_mosquitto_platform_flags
        ${_mosquitto_platform_flags}
        -DWITH_SRV:BOOL=OFF          # Disable SRV to avoid c-ares dependency
    )
endif ()

bambustudio_add_cmake_project(Mosquitto
    URL                 https://github.com/eclipse-mosquitto/mosquitto/archive/refs/tags/v2.0.20.zip
    URL_HASH            SHA256=a1e41100c8762f7e680a896479c2551630ed214a623886f030081b266f2f69e9
    DEPENDS             ${OPENSSL_PKG}
    CMAKE_ARGS
        -DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=ON
        ${_mosquitto_platform_flags}
)

if (MSVC)
    add_debug_dep(dep_Mosquitto)
endif ()
