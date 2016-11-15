#ifndef SCRIPTING_COMMON_H
#define SCRIPTING_COMMON_H

#include "application.h"

#include "common/create-params.h"
#include "common/moscatell-lua.h"

namespace msctl { namespace agent { namespace scripts {

    void set_application( lua_State *L, application *app );
    application *get_application( lua_State *L );

    void lcall_init_globls( lua_State *L );

    void get_common_opts( const lua::object_wrapper &obj,
                          common::create_parameters &out );

    void add_callback( const lua::object_wrapper &obj,
                       const std::string &name,
                       common::create_parameters &out );

    const std::string hide_table_name( );

}}}

#endif // SCRIPTING_COMMON_H


