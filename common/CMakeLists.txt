
set(lib_name ${PROJECT_NAME}_common )

include_directories( ${PROJECT_SOURCE_DIR} )

list(APPEND src_dirs
        .
        ./os
        ${SRPC_DIR_LIST}
    )

foreach( src_dir ${src_dirs} )
    aux_source_directory(${src_dir} lib_src)
    file(GLOB headers ${src_dir}/*.h ${src_dir}/*.hpp)
    list(APPEND lib_src ${headers})
endforeach( )

set( CONFIG_FILES_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/config)
include_directories( ${CONFIG_FILES_DIRECTORY} )

##################################################

add_library(${lib_name} STATIC ${lib_src})

list(APPEND dependencies
    Boost
    )

message( "-- Seting up dependencies for " ${lib_name} )
foreach( dep ${dependencies} )

    message( "\tAdd dependence: " ${dep} )

    add_dependencies(       ${lib_name} ${dep}      )
    add_definitions(        ${${dep}_DEFINITIONS}   )
    include_directories(    ${${dep}_INCLUDE_DIRS}  )

endforeach(dep)

set_target_properties(${lib_name} PROPERTIES COMPILE_DEFINITIONS "STATIC")
get_target_property(${lib_name}_LOCATION ${lib_name} LOCATION)

set(${lib_name}_DEFINITIONS "-DSTATIC" PARENT_SCOPE)

set(${lib_name}_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}
    PARENT_SCOPE
)

set( ${lib_name}_PROJECT_SOURCE_DIR
     ${PROJECT_SOURCE_DIR}
       PARENT_SCOPE
)

set(${lib_name}_LIBRARIES ${${lib_name}_LOCATION} PARENT_SCOPE)
