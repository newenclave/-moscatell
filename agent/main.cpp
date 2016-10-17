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

using namespace msctl;

namespace {

    namespace ba         = boost::asio;
    using posix_stream   = ba::posix::stream_descriptor;
    using transport      = async_transport::point_iface<posix_stream>;
    using transport_sptr = std::shared_ptr<transport>;

    namespace vcomm = vtrc::common;

    void add_all( agent::application *app )
    {
        using namespace msctl::agent;
        app->subsys_add<logging>( );
        app->subsys_add<listener>( );
        app->subsys_add<client>( );
    }

    class tuntap_transport: public transport {

    protected:

        tuntap_transport( ba::io_service &ios )
            :transport(ios, 4096, transport::OPT_NONE )
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

}

int main( )
{
    try {

        vcomm::pool_pair pp(1, 1);
        agent::application app(pp);

        add_all( &app );

        app.init( );
        app.start( );

        auto tuntap = tuntap_transport::create( pp.get_io_service( ) );
        auto hdl = common::open_tun( "tun10" );
        if( hdl < 0 ) {
            std::perror( "tun_alloc" );
            return 1;
        }
        tuntap->get_stream( ).assign( hdl );
        tuntap->start_read( );
        pp.get_io_pool( ).attach( );

    } catch( const std::exception &ex ) {
        std::cerr << "Error: " << ex.what( );
    }


    return 0;
}
