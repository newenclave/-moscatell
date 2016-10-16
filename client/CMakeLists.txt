
aux_source_directory(. src_dir)

set( exe_name  ${PROJECT_NAME}_client )

add_executable(${exe_name} ${src_dir})

list(APPEND dependencies
    ${PROJECT_NAME}_common_s
    VTRC_CLIENT
    VTRC_COMMON
    Boost
    )

include_directories(${PROJECT_SOURCE_DIR})
include_directories(${vtrc_common_s_PROJECT_SOURCE_DIR})

message( "-- Seting up dependencies for " ${exe_name} )
foreach( dep ${dependencies} )

    message( "\tAdd dependence: " ${dep} )

    add_dependencies(       ${exe_name} ${dep}      )
    add_definitions(        ${${dep}_DEFINITIONS}   )
    include_directories(    ${${dep}_INCLUDE_DIRS}  )
    target_link_libraries(  ${exe_name} ${${dep}_LIBRARIES} )

endforeach(dep)

if(WIN32)
    target_link_libraries(${exe_name} ws2_32.lib)
else(WIN32)
    target_link_libraries(  ${exe_name} pthread )
endif(WIN32)


set_target_properties(${exe_name} PROPERTIES COMPILE_DEFINITIONS "STATIC")

