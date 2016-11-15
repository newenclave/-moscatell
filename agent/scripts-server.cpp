
#include "scripts-server.h"
#include "scripts-common.h"

#include "subsys-listener.h"

#define LOG(lev) log_(lev, "script")
#define LOGINF   LOG(logger_impl::level::info)
#define LOGDBG   LOG(logger_impl::level::debug)
#define LOGERR   LOG(logger_impl::level::error)
#define LOGWRN   LOG(logger_impl::level::warning)
namespace msctl { namespace agent { namespace scripts { namespace server {

    namespace {

        application *get_app( lua_State *L )
        {
            return lcall_get_application( L );
        }
    }

    void add_calls( lua::objects::table &tab )
    {

    }


}}}}
