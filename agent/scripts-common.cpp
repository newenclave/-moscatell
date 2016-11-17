
#include <string>
#include <sstream>

#include "scripts-common.h"

#include "common/utilities.h"

#define LOG(lev) log_(lev, "script")
#define LOGINF   LOG(logger_impl::level::info)
#define LOGDBG   LOG(logger_impl::level::debug)
#define LOGERR   LOG(logger_impl::level::error)
#define LOGWRN   LOG(logger_impl::level::warning)

#ifdef _WIN32
#include "common/os/win-utils.h"
#endif

namespace msctl { namespace agent { namespace scripts {

    namespace {

        namespace mlua      = msctl::lua;
        namespace objects   = mlua::objects;

        std::string get_hide_table_name( );

        std::string gs_hide_table_name = get_hide_table_name( );
        std::string gs_app_path        = get_hide_table_name( ) + ".app";

        ///////////////////////////////////////

        struct event_callback final: public utilities::parameter {
            lua_State           *state;
            objects::base_sptr   call;
            void apply( ) override
            {
                call->push( state );
            }
        };

        void add_obj( common::create_parameters::param_map& store,
                      lua_State *state, const std::string &name,
                      objects::base_sptr obj, int obj_type )
        {
            if( obj && ( obj->type_id( ) == obj_type ) ) {
                auto par    = std::make_shared<event_callback>( );
                par->state  = state;
                par->call   = obj;
                store[name] = par;
            }
        }

        void add_any( common::create_parameters::param_map& store,
                      lua_State *state, const std::string &name,
                      objects::base_sptr obj )
        {
            if( obj ) {
                auto par    = std::make_shared<event_callback>( );
                par->state  = state;
                par->call   = obj;
                store[name] = par;
            }
        }

        std::string get_hide_table_name( )
        {
            struct local_index_type { };
            std::ostringstream oss;
            oss << "msctl_hide_table_"
                << utilities::type_uid<local_index_type>::uid( );
            return oss.str( );
        }

        int lcall_log_print_all( lua_State *L, logger_impl::level lvl )
        {

            static auto &log_(get_application(L)->log( ));

            mlua::state ls(L);

            const int n = ls.get_top( );
            std::ostringstream oss;

            oss << "[S] ";

            for( int i=1; i <= n; ++i ) {
                objects::base_sptr o(ls.get_object( i, 1 ));
                oss << o->str( ) << ( i != n ? "\t": "" );
            }

            LOG(lvl) << oss.str( );

            return 0;
        }

        int lcall_log_print( lua_State *L )
        {
            return lcall_log_print_all( L, logger_impl::level::debug );
        }

        int lcall_system( lua_State *L )
        {
            mlua::state ls(L);
            auto cmd = ls.get_opt<std::string>( );
#ifdef _WIN32
            using cs = utilities::charset;
            /// convert string from utf8 to win locale
            cmd = cs::make_mb_string( cs::make_ws_string( cmd ) );
#endif
            ls.push( ::system( cmd.c_str( ) ) );
            return 1;
        }

        void register_globals( mlua::state ls )
        {
            ls.openlib( "base" );
            ls.openlib( "string" );
            ls.openlib( "table" );
            ls.openlib( "math" );
            ls.openlib( "utf8" );
            ls.register_call( "print", &lcall_log_print );
            ls.register_call( "shell", &lcall_system );
        }
    }

    const std::string hide_table_name( )
    {
        return gs_hide_table_name;
    }

    void set_application( lua_State *L, agent::application *app )
    {
        mlua::state ls( L );
        lua::objects::table hide_table;

        hide_table.add( "app", objects::new_light_userdata( app ) );

        ls.set_object( gs_hide_table_name.c_str( ), &hide_table );
    }

    application *get_application(lua_State *L)
    {
        mlua::state ls( L );
        void *ptr = ls.get<void *>( gs_app_path.c_str( ) );
        return static_cast<application *>(ptr);
    }

    void lcall_init_globls( lua_State *L )
    {
        mlua::state ls(L);
        register_globals( ls );
    }

    void get_common_opts( const lua::object_wrapper &obj,
                          common::create_parameters &out )
    {
        out.tcp_nowait = obj["tcp_nowait"].as_bool( false );
        out.max_queue  = obj["max_queue"].as_uint32( 10 );

        if( out.max_queue < 5 ) {
            out.max_queue = 5;
        }
    }

    void add_function(const lua::object_wrapper &obj,
                       const std::string &name,
                       common::create_parameters &out)
    {
        auto call = obj[name].as_object( );
        add_obj( out.params, obj.state( ), name, call,
                 objects::base::TYPE_FUNCTION );
    }

    void add_object( const lua::object_wrapper &obj,
                     const std::string &name,
                     common::create_parameters &out)
    {
        auto call = obj[name].as_object( );
        add_any( out.params, obj.state( ), name, call );
    }

}}}
