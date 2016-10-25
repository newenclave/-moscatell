#include <iostream>

#include "boost/asio.hpp"
#include "common/async-transport-point.hpp"
#include "application.h"

#include "protocol/control.pb.h"
#include "common/subsys-root.h"
#include "common/tuntap.h"

#include "linux/ip.h"

#include "vtrc-common/vtrc-pool-pair.h"

#include "subsys.inc"

#include "common/moscatell-lua.h"
#include "common/cmd-iface.h"

#include "boost/program_options.hpp"

using namespace msctl;


namespace msctl { namespace agent { namespace cmd {

    namespace tuntap { void create( common::cmd_map &all ); }

}}}

namespace {

    namespace ba   = boost::asio;
    namespace po   = boost::program_options;

    namespace vcomm = vtrc::common;

    void add_all( agent::application *app )
    {
        using namespace msctl::agent;
        app->subsys_add<logging>( );
        app->subsys_add<listener>( );
        app->subsys_add<clients>( );
        app->subsys_add<tuntap>( );
    }

    vcomm::thread_pool::thread_decorator decorator( std::string p )
    {
        using dec_type = vcomm::thread_pool::call_decorator_type;
        return [p]( dec_type dt ) {
            switch ( dt ) {
            case vcomm::thread_pool::CALL_PROLOGUE:
                agent::thread_prefix::set( p );
                break;
            case vcomm::thread_pool::CALL_EPILOGUE:
                agent::thread_prefix::set( "" );
                break;
            }
        };
    }

    common::cmd_map get_all_command( )
    {
        common::cmd_map res;
        agent::cmd::tuntap::create( res );
        return res;
    }

    void fill_cmd_options( po::options_description &desc )
    {
        desc.add_options( )
            ("command,c", po::value<std::string>( ),
                    "run command;")
            ;
    }

    void fill_all_options( po::options_description &desc )
    {
        using string_list = std::vector<std::string>;
        desc.add_options( )
            ("help,?",   "help message")

            ("daemon,D", "run process as daemon")

            ("name,n", po::value<std::string>( ),
                    "agent name; whatever you want")

            ("command,c", po::value<std::string>( ),
                    "run command;")

            ("log,l", po::value<string_list>( ),
                    "files for log output; use '-' for stdout")

            ("io-pool-size,i",  po::value<unsigned>( )->default_value( 1 ),
                    "threads for io operations; default = 1")

            ("rpc-pool-size,r", po::value<unsigned>( )->default_value( 1 ),
                    "threads for rpc calls; default = 1")

            ("config,C",    po::value<std::string>( ),
                            "lua script for configure server")

            ("key,k", po::value< std::vector< std::string> >( ),
                     "format is: key=id:key; "
                     "key will use for client with this id; "
                     "or key=key for key for any connections")
            ;
    }

    po::variables_map create_cmd_params( int argc, const char **argv,
                                         po::options_description const &desc,
                                         bool allow_unreg = true)
    {
        po::variables_map vm;
        if( allow_unreg ) {
            po::parsed_options parsed (
                po::command_line_parser(argc, argv)
                    .options(desc)
                    .allow_unregistered( )
                    .run( ));
            po::store( parsed, vm );
            po::notify( vm);
        } else {
            po::parsed_options parsed (
                po::command_line_parser(argc, argv)
                    .options(desc)
                    .run( ));
            po::store( parsed, vm );
            po::notify( vm);
        }
        return vm;
    }

    int run_command( int argc, const char **argv,
                     const std::string &name, common::cmd_map &all )
    {
        auto r = all.find( name );

        if( r == all.end( ) ) {

            std::cerr << "Invalid command '" << name << "';\n";
            std::cerr << "Available:\n";

            for( auto &d: all ) {
                std::cerr << "\t" << d.first << ": "
                          << d.second->desc( ) << "\n";
            }

            return 1;

        } else {

            po::options_description desc( std::string("Valid options for '")
                                        + name + "'");

            desc.add_options( )
                ("help,?",   "help message")
            ;

            r->second->opts( desc );
            auto opts = create_cmd_params( argc, argv, desc, true );

            if( opts.count( "help" ) ) {
                std::cout << desc;
                return 0;
            }

            return r->second->run( opts );
        }
    }
}

int main( int argc, const char **argv )
{
    try {

        po::options_description options;
        fill_cmd_options( options );

        agent::thread_prefix::set( "M" );
        vcomm::pool_pair pp(0, 0);
        agent::application app(pp);

        auto opts = create_cmd_params( argc, argv, options );

        if( opts.count( "command" ) ) {
            auto cmd = opts["command"].as<std::string>();
            auto all = get_all_command( );
            return run_command( argc, argv, cmd, all );
        }

        po::options_description all_opt;
        fill_all_options( all_opt );
        opts = create_cmd_params( argc, argv, all_opt );
        app.cmd_opts( ) = opts;
        add_all( &app );

        if( opts.count( "help" ) ) {
            std::cout << options;
            return 0;
        }

        app.subsys<agent::logging>( ).add_logger_output( "-" );

        if( opts.count( "name" ) ) {
            app.subsys<agent::listener>( ).add_server( "0.0.0.0:11447",
                                             opts["name"].as<std::string>( ));
            app.subsys<agent::listener>( ).start_all( );
        }

        if( opts.count( "daemon" ) )  {
            int res = ::daemon( 1, 0 );
            if( -1 == res ) {
                std::cerr << "::daemon call failed: errno = " << errno << "\n";
            } else if( res != 0 ) {
                return 0;
            }
        }

        auto io_poll = opts["io-pool-size"].as<std::uint32_t>( );
        auto rpc_poll = opts["rpc-pool-size"].as<std::uint32_t>( );

        io_poll  = io_poll  ? io_poll  : 1;
        rpc_poll = rpc_poll ? rpc_poll : 1;

        pp.get_rpc_pool( ).assign_thread_decorator( decorator( "R" ) );
        pp.get_io_pool( ).assign_thread_decorator( decorator( "I" ) );

        auto &logger = app.log( );
        using lvl = agent::logger_impl::level;

        logger( lvl::info, "main" ) << "Start threads. IO: " << io_poll
                                    << " RPC: " << rpc_poll;

        pp.get_rpc_pool( ).add_threads( io_poll - 1 );
        pp.get_io_pool( ).add_threads(  rpc_poll );

        logger( lvl::info, "main" ) << "Init all...";
        app.init( );
        logger( lvl::info, "main" ) << "Start all...";
        app.start( );
        logger( lvl::info, "main" ) << "Start OK.";

        if( !opts.count( "name" ) ) {
            app.subsys<agent::clients>( ).add_client( "212.24.104.31:11447", "tun10" );
            app.subsys<agent::clients>( ).start_all( );
        }

        pp.get_io_pool( ).attach( decorator( "M" ) );

        agent::thread_prefix::set( "M" );

        pp.join_all( );

    } catch( const std::exception &ex ) {
        std::cerr << "Error: " << ex.what( );
    }


    return 0;
}

