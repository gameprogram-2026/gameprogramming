cmake_minimum_required(VERSION 3.20)
project(DeadZone VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ─────────────────────────────────────────────
# Build options
# ─────────────────────────────────────────────
option(BUILD_CLIENT "Build the game client" ON)
option(BUILD_SERVER "Build the dedicated server" ON)
option(BUILD_TESTS  "Build unit tests"         OFF)

# ─────────────────────────────────────────────
# CMake module path for custom Find* scripts
# ─────────────────────────────────────────────
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# ─────────────────────────────────────────────
# Output directories
# ─────────────────────────────────────────────
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# ─────────────────────────────────────────────
# SDL2 (client only — server is headless)
# ─────────────────────────────────────────────
if(BUILD_CLIENT)
    find_package(SDL2 REQUIRED)
    find_package(SDL2_image REQUIRED)
    find_package(SDL2_mixer REQUIRED)
    find_package(SDL2_ttf REQUIRED)
endif()

# ─────────────────────────────────────────────
# ENet (UDP networking — both client and server)
# ─────────────────────────────────────────────
find_package(ENet QUIET)
if(ENet_FOUND)
    message(STATUS "Found system ENet: ${ENET_LIBRARY}")
else()
    # Manual detection for Homebrew install (no cmake config file)
    find_library(ENET_LIBRARY NAMES enet
        HINTS /usr/local/lib /opt/homebrew/lib)
    find_path(ENET_INCLUDE_DIR enet/enet.h
        HINTS /usr/local/include /opt/homebrew/include)
    if(ENET_LIBRARY AND ENET_INCLUDE_DIR)
        add_library(ENet::ENet UNKNOWN IMPORTED)
        set_target_properties(ENet::ENet PROPERTIES
            IMPORTED_LOCATION             "${ENET_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${ENET_INCLUDE_DIR}")
        message(STATUS "Found ENet (manual): ${ENET_LIBRARY}")
    else()
        message(STATUS "ENet not found — using vendored source")
        add_subdirectory(third_party/enet EXCLUDE_FROM_ALL)
        add_library(ENet::ENet ALIAS enet)
    endif()
endif()

# ─────────────────────────────────────────────
# cJSON
# ─────────────────────────────────────────────
find_package(cJSON QUIET)
if(cJSON_FOUND)
    # Homebrew installs the target as 'cjson' (lowercase), not 'cJSON::cJSON'.
    # Create a uniform alias so downstream CMakeLists always use cJSON::cJSON.
    if(NOT TARGET cJSON::cJSON)
        add_library(cJSON::cJSON ALIAS cjson)
    endif()
else()
    message(STATUS "cJSON not found via find_package — using vendored source")
    add_subdirectory(third_party/cJSON EXCLUDE_FROM_ALL)
    add_library(cJSON::cJSON ALIAS cjson)
endif()

# ─────────────────────────────────────────────
# MySQL (Server only)
# ─────────────────────────────────────────────
if(BUILD_SERVER)
    find_library(MYSQL_LIBRARY NAMES mysqlclient
        HINTS /usr/local/lib /opt/homebrew/lib /usr/local/opt/mysql/lib /opt/homebrew/opt/mysql/lib /usr/local/Cellar/mysql/9.6.0_2/lib)
    find_path(MYSQL_INCLUDE_DIR mysql.h
        HINTS /usr/local/include/mysql /opt/homebrew/include/mysql /usr/local/opt/mysql/include/mysql /opt/homebrew/opt/mysql/include/mysql /usr/local/Cellar/mysql/9.6.0_2/include/mysql)
    
    if(MYSQL_LIBRARY AND MYSQL_INCLUDE_DIR)
        add_library(MySQL::MySQL UNKNOWN IMPORTED)
        set_target_properties(MySQL::MySQL PROPERTIES
            IMPORTED_LOCATION             "${MYSQL_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${MYSQL_INCLUDE_DIR}")
        message(STATUS "Found MySQL: ${MYSQL_LIBRARY}")
    else()
        message(FATAL_ERROR "MySQL client library not found! Required for server database.")
    endif()
endif()


# ─────────────────────────────────────────────
# Shared library (ECS, protocol, utilities)
# ─────────────────────────────────────────────
add_subdirectory(src/shared)

# ─────────────────────────────────────────────
# Client & server executables
# ─────────────────────────────────────────────
if(BUILD_CLIENT)
    add_subdirectory(src/client)
endif()

if(BUILD_SERVER)
    add_subdirectory(src/server)
endif()

# ─────────────────────────────────────────────
# Copy assets next to the client executable
# ─────────────────────────────────────────────
if(BUILD_CLIENT)
    add_custom_target(copy_assets ALL
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/assets"
                "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/assets"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/data"
                "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/data"
        COMMENT "Copying assets and data to output directory"
    )
    add_dependencies(DeadZoneClient copy_assets)
endif()

# ─────────────────────────────────────────────
# Install rules (optional packaging)
# ─────────────────────────────────────────────
if(BUILD_CLIENT)
    install(TARGETS DeadZoneClient RUNTIME DESTINATION bin)
endif()
if(BUILD_SERVER)
    install(TARGETS DeadZoneServer RUNTIME DESTINATION bin)
endif()
install(DIRECTORY assets/ DESTINATION bin/assets)
install(DIRECTORY data/   DESTINATION bin/data)
