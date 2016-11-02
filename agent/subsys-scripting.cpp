
#include "subsys-scripting.h"
#include "subsys-clients.h"
#include "subsys-listener.h"
#include "subsys-logging.h"

#include "common/moscatell-lua.h"

#include "common/utilities.h"
#include "common/tuntap.h"

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

        std::uint32_t get_table_value(
                mlua::state &ls, const std::string &call_name,
                const mlua::objects::base *table,
                const char *name, std::uint32_t def )
        {
            static auto &log_(gs_application->log( ));

            auto name_obj = mlua::object_by_path( ls.get_state( ),
                                                  table, name);
            if( !name_obj ) {
                LOGERR << "[S] Invalid table; '" << name << "'"
                          " field doesn't exists; call: " << call_name
                          ;
                return def;
            } else {
                LOGDBG << "[S] " << call_name << ": Got value '"
                       << name_obj->str( )
                       << "' for the field '" << name << "'";
                return name_obj->inum( );
            }

        }


        int get_table_value( mlua::state &ls, const std::string &call_name,
                             const mlua::objects::base *table,
                             const char *name, std::string& res )
        {
            static auto &log_(gs_application->log( ));

            auto name_obj = mlua::object_by_path( ls.get_state( ),
                                                  table, name);
            if( !name_obj ) {
                LOGERR << "[S] Invalid table; '" << name << "'"
                          " field doesn't exists; call: " << call_name
                          ;
                ls.push(  );
                ls.push( "Bad value" );
                return 2;
            } else {
                res = name_obj->str( );
                LOGDBG << "[S] " << call_name << ": Got value '" << res
                       << "' for the field '" << name << "'";
            }
            return 0;
        }

        int lcall_add_device( lua_State *L )
        {
            mlua::state ls(L);
            auto svc = ls.get_object(  );
            int res = 0;
            if( svc && svc->is_container( ) ) {
                listener::server_create_info inf;
                std::string name;
                std::string ip;
                std::string mask;

                if( 0 != get_table_value( ls, "device",
                                          svc.get( ), "name", name ) )
                {
                    return 2;
                }

                if( 0 != get_table_value( ls, "device",
                                          svc.get( ), "mask", mask ) )
                {
                    return 2;
                }

                if( 0 != get_table_value( ls, "device",
                                          svc.get( ), "ip", ip ) )
                {
                    return 2;
                }

				common::device_info tun;
				try {
					tun = common::open_tun( name );
					common::setup_device( tun.handle, ip, ip, mask );
					common::clone_handle( tun.handle );
				} catch( const std::exception &ex ) {
					if( common::TUN_HANDLE_INVALID_VALUE != tun.handle ) {
						common::clone_handle( tun.handle );
					}
					ls.push( );
					ls.push( std::string( "Failed to add device: " ) + name );
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
            //static auto &log_(gs_application->log( ));
            mlua::state ls(L);
            auto svc = ls.get_object(  );
            int res = 0;
            if( svc && svc->is_container( ) ) {

                listener::server_create_info inf;

                if( 0 != get_table_value( ls, "server",
                                          svc.get( ), "addr", inf.point ) )
                {
                    return 2;
                }

                if( 0 != get_table_value( ls, "server",
                                          svc.get( ), "dev", inf.device ) )
                {
                    return 2;
                }

                std::string addr_poll;
                if( 0 != get_table_value( ls, "server",
                                          svc.get( ), "addr_poll",
                                          addr_poll ) )
                {
                    return 2;
                }

                std::vector<std::string> all;

                boost::split( all, addr_poll, boost::is_any_of("/-") );

                if( all.size( ) < 2 ) {
                    ls.push(  );
                    ls.push( std::string("Invalid string format ")
                             + addr_poll );
                    return 2;
                }

                bs::error_code err;
                auto ip   = ba::ip::address_v4::from_string( all[0], err );
                if( err ) {
                    ls.push(  );
                    ls.push( std::string("Invalid format ")
                             + all[0]
                             + std::string( " " )
                             + err.message( ) );
                    return 2;
                }

                auto mask = ba::ip::address_v4::from_string( all[1], err );
                if( err ) {
                    ls.push(  );
                    ls.push( std::string("Invalid format ")
                             + all[1]
                             + std::string( " " )
                             + err.message( ) );
                    return 2;
                }

                ba::ip::address_v4 last;
                if( all.size( ) > 2 ) {
                    last = ba::ip::address_v4::from_string( all[2], err );
                    if( err ) {
                        ls.push(  );
                        ls.push( std::string("Invalid format ")
                                 + all[2]
                                 + std::string( " " )
                                 + err.message( ) );
                        return 2;
                    }
                }

                if( all.size( ) > 2 ) {
                    inf.addr_poll = utilities::address_v4_poll( ip.to_ulong( ),
                                                            mask.to_ulong( ),
                                                            last.to_ulong( ) );
                } else {
                    inf.addr_poll = utilities::address_v4_poll( ip.to_ulong( ),
                                                            mask.to_ulong( ) );
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
            //static auto &log_(gs_application->log( ));
            mlua::state ls(L);
            auto svc = ls.get_object(  );
            int res = 1;
            if( svc ) {
                if( svc->is_container( ) ) {
                    std::string path;
                    if( 0 != get_table_value( ls, "logger",
                                              svc.get( ), "path", path ) )
                    {
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
            //static auto &log_(gs_application->log( ));

            mlua::state ls(L);
            auto svc = ls.get_object(  );
            int res = 0;
            if( svc && svc->is_container( ) ) {
                clients::client_create_info inf;
                if( 0 != get_table_value( ls, "client",
                                          svc.get( ), "addr", inf.point ) )
                {
                    return 2;
                }

                if( 0 != get_table_value( ls, "client",
                                          svc.get( ), "dev", inf.device ) )
                {
                    return 2;
                }

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
            mlua::state ls(L);
            auto svc = ls.get_object(  );
            if( svc && svc->is_container( ) ) {

                auto ios = get_table_value( ls, "polls", svc.get( ), "io", 1 );
                auto rpc = get_table_value( ls, "polls", svc.get( ), "rpc", 1 );

                if( ios < 1 ) ios = 1;
                if( rpc < 1 ) rpc = 1;

                if( ios > 20 ) ios = 20;
                if( rpc > 20 ) rpc = 20;

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
            tab.add( "mkdev",  new_function( &lcall_add_device  ) );
            tab.add( "rmdev",  new_function( &lcall_del_device  ) );

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
                LOGERR << "Failed to load file '"
                       << path << "'; " << error
                       ;
                return;
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

