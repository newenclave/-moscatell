
if(WIN32)
    set(
        CMAKE_USER_MAKE_RULES_OVERRIDE
        "${CMAKE_CURRENT_SOURCE_DIR}/msvc_override.cmake"
    )
endif(WIN32)

cmake_minimum_required(VERSION 2.8)

project(moscatell)

include(cxx11_check.cmake)
check_cxx11( CXX11_ENABLED )

message( "c++11 " ${CXX11_ENABLED} )

if( NOT CXX11_ENABLED )
    message( FATAL_ERROR "Your compiler is too old..." )
endif( NOT CXX11_ENABLED)

get_compiler_thread_local( THREAD_LOCAL_VALUE )
add_definitions( -DTHREAD_LOCAL=${THREAD_LOCAL_VALUE} )

set(Boost_USE_STATIC_LIBS     ON)
set(Boost_USE_MULTITHREADED   ON)
set(Boost_USE_STATIC_RUNTIME  ON)

find_package( Boost 1.53 COMPONENTS
                system
                program_options
                filesystem
              REQUIRED)

############################ VTRC library ############################
find_file( VTRC_IMPORT_CMAKE
         NAMES
             "vtrc-import.cmake"
         PATHS
             "${CMAKE_CURRENT_SOURCE_DIR}/vtrc/" )

if( VTRC_IMPORT )
  set( VTRC_IMPORT_CMAKE ${VTRC_IMPORT} )
endif( )

if( NOT VTRC_IMPORT_CMAKE )
  message( "INITING submodule VTRC" )
  execute_process( COMMAND "git" "submodule" "init"
                   WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
  execute_process( COMMAND "git" "submodule" "update" "vtrc"
                   WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
  execute_process( COMMAND "git" "pull" "origin" "master"
                   WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/vtrc")
endif( NOT VTRC_IMPORT_CMAKE )

add_subdirectory( vtrc )
set( VTRC_IMPORT_CMAKE "${CMAKE_CURRENT_SOURCE_DIR}/vtrc/vtrc-import.cmake")

include( ${VTRC_IMPORT_CMAKE} )
include_directories( ${VTRC_INCLUDE_DIRS} )

######################################################################

############################ LUA library ############################

if( LUA_SRC )

    add_subdirectory( lua-build )
    set( LUA_FOUND 1 )

else( )

    message( "INITING submodule LUA" )
    execute_process( COMMAND "git" "submodule" "init"
                     WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    execute_process( COMMAND "git" "submodule" "update" "lua"
                     WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

    find_file( LUA_SM_PATH
               NAMES "lua.h"
               PATHS "${CMAKE_CURRENT_SOURCE_DIR}/lua/src/" )

    if( LUA_SM_PATH )
        set( LUA_FOUND 1 )
        set( LUA_SRC "${CMAKE_CURRENT_SOURCE_DIR}/lua/src" )
        add_subdirectory( lua-build )
        message( "Lua SRC: "  ${LUA_SRC} )
    endif( LUA_SM_PATH )

    include_directories( ${LUA_SRC} )

endif( )
######################################################################

include_directories( ${CMAKE_CURRENT_SOURCE_DIR} )

if(NOT WIN32)
    set(CMAKE_CXX_FLAGS
        "${CMAKE_CXX_FLAGS} -Wextra -Wall -Wno-long-long -pedantic -std=c++11")
endif( )

add_subdirectory(protocol)
add_subdirectory(common)
add_subdirectory(agent)
add_subdirectory(client)

