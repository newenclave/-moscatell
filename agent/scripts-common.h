#ifndef SCRIPTING_COMMON_H
#define SCRIPTING_COMMON_H

#include "application.h"

struct lua_State;

namespace msctl { namespace agent { namespace scripts {

    void lcall_set_application( lua_State *L, application *app );
    application *lcall_get_application( lua_State *L );

    void lcall_init_globls( lua_State *L );

    const std::string hide_table_name( );

}}}

#endif // SCRIPTING_COMMON_H


