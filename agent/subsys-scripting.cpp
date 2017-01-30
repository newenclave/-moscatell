
#include "subsys-scripting.h"
#include "subsys-clients.h"

#include "subsys-listener.h"
#include "subsys-listener2.h"

#include "subsys-logging.h"

#include "common/utilities.h"
#include "common/tuntap.h"
#include "common/net-ifaces.h"
#include "common/create-params.h"

#include "boost/algorithm/string.hpp"

#include "scripts-common.h"
#include "scripts-server.h"

#define LOG(lev) log_(lev, "script")
#define LOGINF   LOG(logger_impl::level::info)
#define LOGDBG   LOG(logger_impl::level::debug)
#define LOGERR   LOG(logger_impl::level::error)
#define LOGWRN   LOG(logger_impl::level::warning)

#ifdef _WIN32
#include "common/os/win-utils.h"
#endif

namespace msctl { namespace agent {

    namespace {

        application *gs_application = nullptr;

        namespace ba        = boost::asio;
        namespace bs        = boost::system;
        namespace mlua      = msctl::lua;
        namespace objects   = mlua::objects;

        using objects::new_string;
        using objects::new_integer;
        using objects::new_table;

        using utilities::decorators::quote;
        using param_map = std::map<std::string, utilities::parameter_sptr>;

        using table_wrap = mlua::object_wrapper;

        struct event_callback final: public utilities::parameter {
            lua_State           *state;
            objects::base_sptr   call;
            void apply( ) override
            {
                call->push( state );
            }
        };

        void add_param( param_map& store, lua_State *state,
                        const std::string &name, objects::base_sptr param )
        {
            if( param ) {
                auto par = std::make_shared<event_callback>( );
                par->state = state;
                par->call  = param;
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

                //listener::server_create_info inf;
                listener2::server_create_info inf;

                table_wrap tw(L, svc);

                inf.point                 = tw["addr"].as_string( );
                inf.device                = tw["dev"].as_string( );
                //inf.ll_opts.hello_message = tw["txt.hello"].as_string( );

                scripts::get_common_opts( tw["options"], inf.common );

                auto addr_poll  = tw["addr_poll"].as_string( );

                scripts::add_function( tw, "on_register",   inf.common );
                scripts::add_function( tw, "on_disconnect", inf.common );

                objects::table p;
                auto param_ref = std::make_shared<objects::reference>( L, &p );
                add_param( inf.common.params, L, "parameter", param_ref );


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

//                ls.push( gs_application->subsys<listener>( )
//                                        .add_server( inf, false ) );
                ls.push( gs_application->subsys<listener2>( )
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

                    gs_application->subsys<logging>( )
                                   .add_logger_output( path, false );
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

                inf.point  = tw["addr"].as_string( );
                inf.device = tw["dev"].as_string( );
                inf.id     = tw["id"].as_string( );

                scripts::get_common_opts( tw["options"],    inf.common );
                scripts::add_function( tw, "on_register",   inf.common );
                scripts::add_function( tw, "on_disconnect", inf.common );

                objects::table p;
                auto param_ref = std::make_shared<objects::reference>( L, &p );
                add_param( inf.common.params, L, "parameter", param_ref );

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

                if( ios < 1 )  { ios =  1; }
                if( rpc < 1 )  { rpc =  1; }

                if( ios > 20 ) { ios = 20; }
                if( rpc > 20 ) { rpc = 20; }

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

        int lcall_net_ifaces( lua_State *L )
        {
//            static auto &log_(gs_application->log( ));

            using objects::new_string;
            using objects::new_boolean;
            using objects::new_integer;
            using objects::new_table;
            objects::table res;

            auto ifaces = utilities::get_system_ifaces( );
            for( auto &i: ifaces ) {
                res.add( new_table( )
                       ->add( "name",  new_string( i.name( ) ) )
                       ->add( "addr",  new_string( i.addr( ).to_string( ) ) )
                       ->add( "mask",  new_string( i.mask( ).to_string( ) ) )
                       ->add( "id",    new_integer( i.id( ) ) )
                       ->add( "is_v4", new_boolean( i.is_v4( ) ) )
                       ->add( "is_v6", new_boolean( i.is_v6( ) ) )
                       );
            }

            res.push( L );
            return 1;

        }

        int lcall_os_info( lua_State *L )
        {
            static auto &log_(gs_application->log( ));
            using objects::new_string;
            using objects::new_boolean;
            using objects::new_integer;
            using objects::new_table;
            objects::table res;
#if   defined(_WIN32)
            res.add( "name", new_string( "windows" ) );
            OSVERSIONINFO ovi;
            if( utilities::fill_native_version( &ovi ) ) {
                res.add( "verinfo", new_table( )
                         ->add( "major", new_integer( ovi.dwMajorVersion ) )
                         ->add( "minor", new_integer( ovi.dwMinorVersion ) )
                         ->add( "minor", new_integer( ovi.dwMinorVersion ) )
                         ->add( "platform_id", new_integer( ovi.dwPlatformId ) )
                         ->add( "build", new_integer( ovi.dwBuildNumber ) )
                         );
            } else {
                LOGERR << "Failed to get os version: " << GetLastError( );
            }
#elif defined( __linux__ )
            res.add( "name", new_string( "linux" ) );
#elif defined (__APPLE__)
            res.add( "name", new_string( "apple" ) );
#elif defined (__FreeBSD__) || defined( __OpenBSD__)
            res.add( "name", new_string( "bsd" ) );
#endif
            res.push( L );
            return 1;
        }

        void state_init( lua_State *L, application *app )
        {
            mlua::state ls(L);
            using namespace objects;

            scripts::set_application( L, app );
            scripts::lcall_init_globls( L );

            /// set tables
            objects::table tab;

            scripts::server::add_calls( tab );

            tab.add( "server", new_function( &lcall_add_server ) );
            tab.add( "client", new_function( &lcall_add_client ) );
            tab.add( "logger", new_function( &lcall_add_logger ) );
            tab.add( "polls",  new_function( &lcall_set_polls  ) );
            tab.add( "mkdev",  new_function( &lcall_add_device ) );
            tab.add( "rmdev",  new_function( &lcall_del_device ) );

            tab.add( "os", new_table( )
                     ->add( "info", new_function( &lcall_os_info ) )
                     ->add( "ifaces", new_function( &lcall_net_ifaces ) )
                     );

            ls.set_object( "msctl", &tab );

        }
    }

    struct scripting::impl {

        application     *app_;
        scripting       *parent_;
        logger_impl     &log_;
        mlua::state      state_;
        std::mutex       state_lock_;

        impl( application *app )
            :app_(app)
            ,log_(app_->log( ))
        {
            gs_application = app_;
        }

        void call_event( const std::string &name, const param_map &map_params,
                         objects::base &param )
        {
            auto f = map_params.find( name );
            if( f != map_params.end( ) ) {

                auto par = map_params.find( "parameter" );

                size_t params = (par != map_params.end( ));

                std::lock_guard<std::mutex> lck(state_lock_);

                f->second->apply( );
                param.push( state_.get_state( ) );

                if( params != 0 ) {
                    par->second->apply( );
                }

                int res = lua_pcall( state_.get_state( ),
                                     params + 1, LUA_MULTRET, 0 );

                if( res != LUA_OK ) {
                    std::string err = state_.pop_error( );
                    LOGERR << "Failed to call " << quote( name )
                           << " for client; "   << err
                           ;
                }
            }
        }

        void add_client_to_table( objects::table &res,
                                  vtrc::client::base_sptr c )
        {
            auto ptr  = reinterpret_cast<std::uint64_t>(c.get( ));
            auto name = c->connection( )->name( );
            auto id   = c->connection( )->id( );

            res.add( "client", new_table( )
                     ->add( "name", new_string( name ) )
                     ->add( "prt",  new_integer( ptr ) )
                     ->add( "id",   new_string( id ) )
                    );
        }

        void add_connection_to_table( objects::table &res,
                                      vtrc::common::connection_iface *c )
        {
            auto ptr  = reinterpret_cast<std::uint64_t>( c );
            auto name = c->name( );
            auto id   = c->id( );

            res.add( "client", new_table( )
                     ->add( "name", new_string( name ) )
                     ->add( "prt",  new_integer( ptr ) )
                     ->add( "id",   new_string( id ) )
                    );
        }

        void on_con_register( vtrc::common::connection_iface *c,
                              const listener::server_create_info &inf,
                              const listener::register_info &reg )
        {
            objects::table res;

            add_connection_to_table( res, c );

            res.add( "addr",      new_string( reg.ip ) );
            res.add( "mask",      new_string( reg.mask ) );
            res.add( "dst_addr",  new_string( reg.my_ip ) );
            res.add( "device",    new_string( inf.device ) );
            res.add( "name",      new_string( reg.name ) );

            call_event( "on_register", inf.common.params, res );
        }

        void on_con_disconnect( vtrc::common::connection_iface *c,
                                const listener::server_create_info &inf )
        {
            objects::table res;

            add_connection_to_table( res, c );

            call_event( "on_disconnect", inf.common.params, res );
        }

        void on_client_disconnect( vtrc::client::base_sptr c,
                                   const clients::client_create_info &inf )
        {
            objects::table res;

            add_client_to_table( res, c );

            res.add( "device", new_string( inf.device ) );

            call_event( "on_disconnect", inf.common.params, res );
        }

        void on_client_register( vtrc::client::base_sptr c,
                                 const clients::client_create_info &inf,
                                 const clients::register_info &reg )
        {
            objects::table res;

            add_client_to_table( res, c );

            res.add( "addr",      new_string( reg.ip ) );
            res.add( "mask",      new_string( reg.mask ) );
            res.add( "dst_addr",  new_string( reg.server_ip ) );
            res.add( "device",    new_string( inf.device ) );
            res.add( "name",      new_string( inf.name ) );

            call_event( "on_register", inf.common.params, res );
        }

        void init( )
        {
            auto &cc( app_->subsys<clients>( ) );

            ////////////// Clients
            cc.on_client_disconnect_connect(
                [this]( vtrc::client::base_sptr clnt,
                        const clients::client_create_info &inf )
                { this->on_client_disconnect( clnt, inf ); } );

            cc.on_client_register_connect(
                [this]( vtrc::client::base_sptr c,
                        const clients::client_create_info &inf,
                        const clients::register_info &reg )
                { this->on_client_register( c, inf, reg ); });

            ////////////// Listeners
            auto &ll( app_->subsys<listener>( ) );

            ll.on_reg_connection_connect(
                [this]( vtrc::common::connection_iface *c,
                        const listener::server_create_info &inf,
                        const listener::register_info &reg )
                { this->on_con_register( c, inf, reg ); });

            ll.on_stop_connection_connect(
                [this]( vtrc::common::connection_iface *c,
                        const listener::server_create_info &inf )
                { this->on_con_disconnect( c, inf ); });
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
        state_init( impl_->state_.get_state( ), impl_->app_ );
        impl_->init( );
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

