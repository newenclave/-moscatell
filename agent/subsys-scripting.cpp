
#include "subsys-scripting.h"
#include "subsys-clients.h"
#include "subsys-listener.h"
#include "subsys-logging.h"

#include "common/moscatell-lua.h"

#include "common/utilities.h"
#include "common/tuntap.h"
#include "common/net-ifaces.h"
#include "common/parameter.h"

#include "boost/algorithm/string.hpp"


#define LOG(lev) log_(lev, "scripting") 
#define LOGINF   LOG(logger_impl::level::info)
#define LOGDBG   LOG(logger_impl::level::debug)
#define LOGERR   LOG(logger_impl::level::error)
#define LOGWRN   LOG(logger_impl::level::warning)

namespace msctl { namespace agent {

    namespace {

        application *gs_application = nullptr;

        namespace ba        = boost::asio;
        namespace bs        = boost::system;
        namespace mlua      = msctl::lua;
        namespace objects   = mlua::objects;

        using utilities::decorators::quote;
        using param_map = std::map<std::string, utilities::parameter_sptr>;

        int lcall_log_print( lua_State *L )
        {
            static auto &log_(gs_application->log( ));

            mlua::state ls(L);

            const int n = ls.get_top( );
            std::ostringstream oss;

            oss << "[S] ";

            for( int i=1; i <= n; ++i ) {
                objects::base_sptr o(ls.get_object( i, 1 ));
                oss << o->str( ) << ( i != n ? "\t": "" );
            }

            LOGDBG << oss.str( );

            return 0;
        }

        int lcall_system( lua_State *L )
        {
            mlua::state ls(L);
            objects::table res;
            auto cmd = ls.get_opt<std::string>( );
            ls.push( ::system( cmd.c_str( ) ) );
            return 1;
        }

        void register_globals( mlua::state ls )
        {
            ls.register_call( "print", &lcall_log_print );
            ls.register_call( "shell", &lcall_system );
        }

        struct table_wrap {

            lua_State          *state;
            objects::base_sptr  ptr;

            table_wrap( lua_State *s, const objects::base *p )
                :state(s)
            {
                if( p ) {
                    ptr.reset(p->clone( ));
                }
            }

            table_wrap( lua_State *s, objects::base_sptr p )
                :state(s)
                ,ptr(p)
            { }

            table_wrap operator [] ( const std::string &path ) const
            {
                auto obj = mlua::object_by_path( state, ptr.get( ),
                                                 path.c_str( ) );
                return table_wrap( state, obj );
            }

            objects::base_sptr as_object( )
            {
                return ptr;
            }

            bool is_string( ) const
            {
                return ptr->type_id( ) == objects::base::TYPE_STRING;
            }

            bool is_bool( ) const
            {
                return is_number( )
                    || ptr->type_id( ) == objects::base::TYPE_BOOL;
            }

            bool is_number( ) const
            {
                switch (ptr->type_id( )) {
                case objects::base::TYPE_INTEGER:
                case objects::base::TYPE_UINTEGER:
                    return true;
                default:
                    break;
                }
                return false;
            }

            std::string as_string( const std::string &def = std::string( ) )
            {
                if( ptr && is_string( ) ) {
                    return ptr->str( );
                }
                return def;
            }

            std::uint32_t as_uint32( std::uint32_t def = 0 )
            {
                if( ptr && is_number( ) ) {
                    return ptr->inum( );
                }
                return def;
            }

            bool as_bool( bool def = false )
            {
                if( ptr && is_bool( ) ) {
                    return !!ptr->inum( );
                }
                return def;
            }

        };

        struct event_callback: public utilities::parameter {
            lua_State           *state;
            objects::base_sptr   call;
        };

        void add_param( param_map& store, lua_State *state,
                        const std::string &name, objects::base_sptr call )
        {
            if( call && call->type_id( ) == objects::base::TYPE_FUNCTION ) {
                auto par = std::make_shared<event_callback>( );
                par->state = state;
                par->call  = call;
                store[name] = par;
            }
        }

        int lcall_add_device( lua_State *L )
        {
            static auto &log_(gs_application->log( ));
            mlua::state ls(L);
            auto svc = ls.get_object(  );
            int res = 0;
            if( svc && svc->is_container( ) ) {
                listener::server_create_info inf;

                table_wrap tw(L, svc);

                auto name = tw["name"].as_string( );
                auto ip   = tw["ip"].as_string( );
                auto mask = tw["mask"].as_string( );;

                if( ip.empty( ) || mask.empty( ) ) {
                    LOGERR << "Bad device format: " << quote(svc->str( ))
                           << " ip = " << quote(ip)
                           << " mask = " << quote(mask)
                              ;
                    ls.push( );
                    ls.push( "Bad device format." );
                    return 2;
                }

                LOGDBG << "Adding device: " << quote(svc->str( ));
                common::device_info tun;
                try {
                    tun = std::move(common::open_tun( name ));
                    common::setup_device( tun.get( ), name, ip, ip, mask );
                } catch( const std::exception &ex ) {
                    ls.push( );
                    ls.push( std::string( "Failed to add device: " )
                             + name + "; " + ex.what( ) );
                    return 2;
                }

                ls.push( true );
                res = 1;
            } else {
                ls.push(  );
                ls.push( "Bad value" );
                return 2;
            }
            return res;
        }

        int lcall_del_device( lua_State *L )
        {
            mlua::state ls(L);
            objects::table res;
            auto cmd = ls.get_opt<std::string>( );
            if( common::del_tun( cmd ) < 0 ) {
                std::error_code ec( errno, std::system_category( ) );
                ls.push( false );
                ls.push( ec.message( ) );
                return 0;
            }
            ls.push( true );
            return 1;
        }

        int lcall_add_server( lua_State *L )
        {
            static auto &log_(gs_application->log( ));
            mlua::state ls(L);
            auto svc = ls.get_object(  );
            int res = 0;
            if( svc && svc->is_container( ) ) {

                listener::server_create_info inf;

                table_wrap tw(L, svc);

                inf.point       = tw["addr"].as_string( );
                inf.device      = tw["dev"].as_string( );
                auto addr_poll  = tw["addr_poll"].as_string( );
                inf.tcp_nowait  = tw["tcp_nowait"].as_bool( true );

                if( inf.point.empty( ) || addr_poll.empty( ) ) {
                    LOGERR << "Bad server format: " << quote(svc->str( ))
                           << " addr = " << quote(inf.point)
                           << " addr_poll = " << quote(addr_poll);
                    ls.push( );
                    ls.push( "Bad server value." );
                    return 0;
                }

                std::vector<std::string> all;
                boost::split( all, addr_poll, boost::is_any_of("/- \t\r\n") );

                if( all.size( ) < 2 ) {
                    ls.push(  );
                    ls.push( std::string("Invalid string format ")
                             + addr_poll );
                    return 2;
                }

                std::vector<ba::ip::address_v4> addrs;
                addrs.reserve( all.size( ) );

                size_t i = 0;
                for( auto &s: all ) {

                    if( s.empty( ) ) {
                        continue;
                    }

                    bs::error_code err;
                    bool failed = false;
                    auto next = ba::ip::address_v4::from_string( s, err );
                    if( err ) {
                        failed = true;
                        if (i == (all.size( ) - 1 ) ) {
                            int bits = atoi( s.c_str( ) );
                            if( bits >= 8 && bits <= 32 ) {
                                failed = false;
                                addrs[i] = utilities::create_mask_v4( bits );
                            }
                        }
                    }
                    if( failed ) {
                        ls.push(  );
                        ls.push( std::string("Invalid format ")
                                 + all[0]
                                 + std::string( " " )
                                 + err.message( ) );
                        return 2;
                    } else {
                        addrs.emplace_back( std::move( next ) );
                        ++i;
                    }
                }

                if( all.size( ) > 2 ) {
                    inf.addr_poll
                         = utilities::address_v4_poll( addrs[0].to_ulong( ),
                                                       addrs[1].to_ulong( ),
                                                       addrs[2].to_ulong( ) );
                } else {
                    inf.addr_poll
                         = utilities::address_v4_poll( addrs[0].to_ulong( ),
                                                       addrs[1].to_ulong( ) );
                }

                ls.push( gs_application->subsys<listener>( )
                                        .add_server( inf, false ) );
                res = 1;
            } else {
                ls.push(  );
                ls.push( "Bad value" );
                return 2;
            }
            return res;
        }

        int lcall_add_logger( lua_State *L )
        {
            static auto &log_(gs_application->log( ));
            mlua::state ls(L);
            auto svc = ls.get_object(  );
            int res = 1;
            if( svc ) {
                if( svc->is_container( ) ) {

                    auto path = table_wrap( L, svc)["path"].as_string( );

                    if( path.empty( ) ) {
                        LOGERR << "Bad logger format " << svc->str( );
                        ls.push( false );
                        ls.push( "Bad logger format. 'path' was not found" );
                        return 2;
                    }

                    gs_application->subsys<logging>( ).add_logger_output(path);
                    ls.push( true );
                } else if( svc->type_id( ) == objects::base::TYPE_STRING ) {
                    gs_application->subsys<logging>( )
                                  .add_logger_output( svc->str( ) );
                    ls.push( true );
                } else {
                    res = 2;
                }
            } else {
                res = 2;
            }
            if( res == 2 ) {
                ls.push(  );
                ls.push( "Bad value" );
            }
            return res;
        }

        int lcall_add_client( lua_State *L )
        {
            static auto &log_(gs_application->log( ));

            mlua::state ls(L);
            auto svc = ls.get_object(  );
            int res = 0;
            if( svc && svc->is_container( ) ) {

                clients::client_create_info inf;

                table_wrap tw(L, svc);

                inf.point      = tw["addr"].as_string( );
                inf.device     = tw["dev"].as_string( );
                inf.tcp_nowait = tw["tcp_nowait"].as_bool( true );

                auto on_register   = tw["on_register"].as_object( );
                auto on_disconnect = tw["on_disconnect"].as_object( );

                add_param( inf.params, L, "on_register", on_register );
                add_param( inf.params, L, "on_disconnect", on_disconnect );

//                if( on_register && on_register. )
//                inf.params[]

//                call->push( L );
//                int res = lua_pcall( L, 0, LUA_MULTRET, 0 );

                if( inf.point.empty( ) ) {
                    LOGERR << "Bad client format: " << quote(svc->str( ))
                           << " addr = " << quote(inf.point)
                           << " dev = "  << quote(inf.device);
                    ls.push( );
                    ls.push( "Bad client format." );
                    return 2;
                }

                LOGDBG << "Adding new client: " << svc->str( );

                ls.push(gs_application->subsys<clients>( )
                                        .add_client( inf, false ) );

                res = 1;
            } else {
                ls.push(  );
                ls.push( "Bad value" );
                return 2;
            }
            return res;
        }

        int lcall_set_polls( lua_State *L )
        {
            static auto &log_(gs_application->log( ));

            mlua::state ls(L);
            auto svc = ls.get_object(  );
            if( svc && svc->is_container( ) ) {

                table_wrap tw(L, svc);

                auto ios = tw["io"].as_uint32( );
                auto rpc = tw["rpc"].as_uint32( );

                if( ios < 1 ) ios = 1;
                if( rpc < 1 ) rpc = 1;

                if( ios > 20 ) ios = 20;
                if( rpc > 20 ) rpc = 20;

                LOGDBG << "Got polls values: " << svc->str( );

                gs_application->set_io_pools( ios );
                gs_application->set_rpc_pools( ios );

                ls.push( true );

            } else {
                ls.push(  );
                ls.push( "Bad value" );
                return 2;
            }
            return 1;
        }

        void state_init( lua_State *L )
        {
            mlua::state ls(L);
            using namespace objects;

            register_globals( ls );

            /// set tables
            objects::table tab;
            tab.add( "server", new_function( &lcall_add_server ) );
            tab.add( "client", new_function( &lcall_add_client ) );
            tab.add( "logger", new_function( &lcall_add_logger ) );
            tab.add( "polls",  new_function( &lcall_set_polls  ) );
            tab.add( "mkdev",  new_function( &lcall_add_device ) );
            tab.add( "rmdev",  new_function( &lcall_del_device ) );

            ls.set_object( "msctl", &tab );

        }
    }

    struct scripting::impl {

        application     *app_;
        scripting       *parent_;
        logger_impl     &log_;
        mlua::state      state_;

        impl( application *app )
            :app_(app)
            ,log_(app_->log( ))
        {
            gs_application = app_;
        }

        void run_config( const std::string &path )
        {
            auto res = state_.load_file( path.c_str( ) );
            if( 0 != res ) {
                auto error =  state_.pop_error( );
                std::ostringstream oss;
                oss << "Failed to load file '"
                    << path << "'; " << error<< std::endl;
                    ;
                std::cerr << oss.str( );
                throw std::runtime_error( oss.str( ) );
            }
        }
    };

    scripting::scripting( application *app )
        :impl_(new impl(app))
    {
        impl_->parent_ = this;
    }

    void scripting::init( )
    {
        state_init( impl_->state_.get_state( ) );
    }

    void scripting::start( )
    { 
        impl_->LOGINF << "Started.";
    }

    void scripting::stop( )
    { 
        impl_->LOGINF << "Stopped.";
    }
    
    std::shared_ptr<scripting> scripting::create( application *app )
    {
        return std::make_shared<scripting>( app );
    }

    void scripting::run_config( )
    {
        auto &opts(impl_->app_->cmd_opts( ));
        if( opts.count( "config" ) ) {
            auto cfg = opts["config"].as<std::string>( );
            impl_->run_config( cfg );
        }
    }

}}

