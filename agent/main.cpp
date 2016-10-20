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

#include "boost/program_options.hpp"

using namespace msctl;

namespace {

    namespace ba         = boost::asio;
    namespace po         = boost::program_options;
    using posix_stream   = ba::posix::stream_descriptor;
    using transport      = async_transport::point_iface<posix_stream>;
    using transport_sptr = std::shared_ptr<transport>;

    namespace vcomm = vtrc::common;

    void add_all( agent::application *app )
    {
        using namespace msctl::agent;
        app->subsys_add<logging>( );
        app->subsys_add<listener>( );
        app->subsys_add<clients>( );
        app->subsys_add<tuntap>( );
    }

    class tuntap_transport: public transport {

    protected:

        tuntap_transport( ba::io_service &ios )
            :transport(ios, 2048, transport::OPT_DISPATCH_READ )
        { }

        void on_read( const char *data, size_t length ) override
        {
            const iphdr *hdr = reinterpret_cast<const iphdr *>(data);
            //const ipv6hdr *v6hdr = reinterpret_cast<const iphdr *>(data);

            ba::ip::address_v4 sa(ntohl(hdr->saddr));
            ba::ip::address_v4 da(ntohl(hdr->daddr));
            std::cout << "read " << length
                      << " bytes "
                      << " from " << sa.to_string( )
                      << " to " << da.to_string( )
                      << "\n";
        }

        void on_write_error( const boost::system::error_code &code ) override
        {
            std::cout << "Write error: " << code.value( )
                      << " " << code.message( ) << "\n";
            //throw std::runtime_error( code.message( ) );
        }

    public:

        static transport_sptr create( ba::io_service &ios )
        {
            auto new_inst = new tuntap_transport(ios);
            return transport_sptr(new_inst);
        }

    };

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

    void fill_all_options( po::options_description &desc )
    {
        using string_list = std::vector<std::string>;
        desc.add_options( )
            ("help,?",   "help message")

            ("daemon,D", "run process as daemon")

            ("name,n", po::value<std::string>( )->default_value(""),
                    "agent name; whatever you want")

            ("log,l", po::value<string_list>( ),
                    "files for log output; use '-' for stdout")

            ("io-pool-size,i",  po::value<unsigned>( )->default_value( 1 ),
                    "threads for io operations; default = 1")

            ("rpc-pool-size,r", po::value<unsigned>( )->default_value( 1 ),
                    "threads for rpc calls; default = 1")

            ("config,c",    po::value<std::string>( ),
                            "lua script for configure server")

            ("key,k", po::value< std::vector< std::string> >( ),
                     "format is: key=id:key; "
                     "key will use for client with this id; "
                     "or key=key for key for any connections")
            ;
    }

    po::variables_map create_cmd_params( int argc, const char **argv,
                                         po::options_description const &desc )
    {
        po::variables_map vm;
        po::parsed_options parsed (
            po::command_line_parser(argc, argv)
                .options(desc)
                //.allow_unregistered( )
                .run( ));
        po::store( parsed, vm );
        po::notify( vm);
        return vm;
    }
}

int main( int argc, const char **argv )
{
    try {

        po::options_description options;
        fill_all_options( options );

        agent::thread_prefix::set( "M" );
        vcomm::pool_pair pp(0, 0);
        agent::application app(pp);

        app.cmd_opts( ) = create_cmd_params( argc, argv, options );
        add_all( &app );
        auto &opts = app.cmd_opts( );

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
            app.subsys<agent::clients>( ).add_client( "10.30.0.40:11447", "tun10" );
        }

//        auto tuntap = tuntap_transport::create( pp.get_io_service( ) );
//        auto hdl = common::open_tun( "tun10" );
//        if( hdl < 0 ) {
//            std::perror( "tun_alloc" );
//            return 1;
//        }

//        tuntap->get_stream( ).assign( hdl );
//        tuntap->start_read( );

        pp.get_io_pool( ).attach( decorator( "M" ) );

        agent::thread_prefix::set( "M" );

        pp.join_all( );

    } catch( const std::exception &ex ) {
        std::cerr << "Error: " << ex.what( );
    }


    return 0;
}

