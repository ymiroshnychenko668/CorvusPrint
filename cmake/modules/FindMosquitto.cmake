#///////////////////////////////////////////////////////////////////////////
#//-------------------------------------------------------------------------
#//
#// Description:
#//      cmake module for finding Mosquitto MQTT client library installation
#//      Mosquitto installation location is defined by environment variable $MOSQUITTO
#//
#//      following variables are defined:
#//      Mosquitto_DIR              - Mosquitto installation directory
#//      Mosquitto_INCLUDE_DIR      - Mosquitto header directory
#//      Mosquitto_LIBRARY_DIR      - Mosquitto library directory
#//      Mosquitto_LIBS             - Mosquitto library files
#//
#//      Example usage:
#//          find_package(Mosquitto REQUIRED)
#//
#//
#//-------------------------------------------------------------------------


set(Mosquitto_FOUND        FALSE)
set(Mosquitto_ERROR_REASON "")
set(Mosquitto_DEFINITIONS  "")
unset(Mosquitto_LIBS CACHE)

set(Mosquitto_DIR $ENV{MOSQUITTO})

# Determine architecture for deps path
set(_Mosquitto_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
if(CMAKE_OSX_ARCHITECTURES)
    list(GET CMAKE_OSX_ARCHITECTURES 0 _Mosquitto_ARCH)
endif()

# Check BambuStudio_deps build directory first (preferred)
if(NOT Mosquitto_DIR AND EXISTS "${CMAKE_SOURCE_DIR}/deps/build/${_Mosquitto_ARCH}/BambuStudio_deps/usr/local")
    set(Mosquitto_DIR "${CMAKE_SOURCE_DIR}/deps/build/${_Mosquitto_ARCH}/BambuStudio_deps/usr/local")
    message(STATUS "Using Mosquitto from BambuStudio_deps build: ${Mosquitto_DIR}")
# Fallback to destdir if BambuStudio_deps doesn't exist
elseif(NOT Mosquitto_DIR AND EXISTS "${CMAKE_SOURCE_DIR}/deps/build/${_Mosquitto_ARCH}/destdir/usr/local")
    set(Mosquitto_DIR "${CMAKE_SOURCE_DIR}/deps/build/${_Mosquitto_ARCH}/destdir/usr/local")
    message(STATUS "Using Mosquitto from deps build: ${Mosquitto_DIR}")
endif()
unset(_Mosquitto_ARCH)

if(NOT Mosquitto_DIR)

    set(Mosquitto_FOUND TRUE)

    # Look for static library first, then shared
    set(_Mosquitto_LIB_NAMES "mosquitto" "libmosquitto")
    find_library(Mosquitto_LIBS
        NAMES ${_Mosquitto_LIB_NAMES})
    if(NOT Mosquitto_LIBS)
        set(Mosquitto_FOUND FALSE)
        set(Mosquitto_ERROR_REASON "${Mosquitto_ERROR_REASON} Cannot find Mosquitto library '${_Mosquitto_LIB_NAMES}'.")
    else()
        get_filename_component(Mosquitto_DIR ${Mosquitto_LIBS} PATH)
    endif()
    unset(_Mosquitto_LIB_NAMES)

    set(_Mosquitto_HEADER_FILE_NAME "mosquitto.h")
    find_file(_Mosquitto_HEADER_FILE
        NAMES ${_Mosquitto_HEADER_FILE_NAME})
    if(NOT _Mosquitto_HEADER_FILE)
        set(Mosquitto_FOUND FALSE)
        set(Mosquitto_ERROR_REASON "${Mosquitto_ERROR_REASON} Cannot find Mosquitto header file '${_Mosquitto_HEADER_FILE_NAME}'.")
    endif()
    unset(_Mosquitto_HEADER_FILE_NAME)

    if(NOT Mosquitto_FOUND)
        set(Mosquitto_ERROR_REASON "${Mosquitto_ERROR_REASON} Mosquitto not found in system directories (and environment variable MOSQUITTO is not set).")
    else()
        get_filename_component(Mosquitto_INCLUDE_DIR ${_Mosquitto_HEADER_FILE} DIRECTORY)
    endif()

    unset(_Mosquitto_HEADER_FILE CACHE)

else()

    set(Mosquitto_FOUND TRUE)

    set(Mosquitto_INCLUDE_DIR "${Mosquitto_DIR}/include")
    if(NOT EXISTS "${Mosquitto_INCLUDE_DIR}")
        set(Mosquitto_FOUND FALSE)
        set(Mosquitto_ERROR_REASON "${Mosquitto_ERROR_REASON} Directory '${Mosquitto_INCLUDE_DIR}' does not exist.")
    endif()

    set(Mosquitto_LIBRARY_DIR "${Mosquitto_DIR}/lib")
    if(NOT EXISTS "${Mosquitto_LIBRARY_DIR}")
        set(Mosquitto_FOUND FALSE)
        set(Mosquitto_ERROR_REASON "${Mosquitto_ERROR_REASON} Directory '${Mosquitto_LIBRARY_DIR}' does not exist.")
    endif()

    set(_Mosquitto_LIB_NAMES "mosquitto" "libmosquitto" "mosquitto_static")
    find_library(Mosquitto_LIBS
        NAMES ${_Mosquitto_LIB_NAMES}
        PATHS ${Mosquitto_LIBRARY_DIR}
        NO_DEFAULT_PATH)
    if(NOT Mosquitto_LIBS)
        set(Mosquitto_FOUND FALSE)
        set(Mosquitto_ERROR_REASON "${Mosquitto_ERROR_REASON} Cannot find Mosquitto library '${_Mosquitto_LIB_NAMES}' in '${Mosquitto_LIBRARY_DIR}'.")
    endif()
    unset(_Mosquitto_LIB_NAMES)

    set(_Mosquitto_HEADER_FILE_NAME "mosquitto.h")
    find_file(_Mosquitto_HEADER_FILE
        NAMES ${_Mosquitto_HEADER_FILE_NAME}
        PATHS ${Mosquitto_INCLUDE_DIR}
        NO_DEFAULT_PATH)
    if(NOT _Mosquitto_HEADER_FILE)
        set(Mosquitto_FOUND FALSE)
        set(Mosquitto_ERROR_REASON "${Mosquitto_ERROR_REASON} Cannot find Mosquitto header file '${_Mosquitto_HEADER_FILE_NAME}' in '${Mosquitto_INCLUDE_DIR}'.")
    endif()
    unset(_Mosquitto_HEADER_FILE_NAME)
    unset(_Mosquitto_HEADER_FILE CACHE)

endif()


# make variables changeable
mark_as_advanced(
    Mosquitto_INCLUDE_DIR
    Mosquitto_LIBRARY_DIR
    Mosquitto_LIBS
    Mosquitto_DEFINITIONS
)


# report result
if(Mosquitto_FOUND)
    message(STATUS "Found Mosquitto in '${Mosquitto_DIR}'.")
    message(STATUS "Using Mosquitto include directory '${Mosquitto_INCLUDE_DIR}'.")
    message(STATUS "Using Mosquitto library '${Mosquitto_LIBS}'.")

    # Create imported target
    if(NOT TARGET Mosquitto::mosquitto)
        add_library(Mosquitto::mosquitto INTERFACE IMPORTED)
        set_target_properties(Mosquitto::mosquitto PROPERTIES
            INTERFACE_LINK_LIBRARIES "${Mosquitto_LIBS}"
            INTERFACE_INCLUDE_DIRECTORIES "${Mosquitto_INCLUDE_DIR}"
            INTERFACE_COMPILE_DEFINITIONS "${Mosquitto_DEFINITIONS}"
        )
    endif()
else()
    if(Mosquitto_FIND_REQUIRED)
        message(FATAL_ERROR "Unable to find requested Mosquitto installation:${Mosquitto_ERROR_REASON}")
    else()
        if(NOT Mosquitto_FIND_QUIETLY)
            message(STATUS "Mosquitto was not found:${Mosquitto_ERROR_REASON}")
        endif()
    endif()
endif()
