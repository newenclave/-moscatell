#include <iostream>

#include "boost/asio.hpp"
#include "common/async-transport-point.hpp"
#include "application.h"

#include "protocol/control.pb.h"
#include "common/subsys-root.h"
#include "common/tuntap.h"

#include "linux/ip.h"

using namespace msctl;

namespace {

    namespace ba         = boost::asio;
    using posix_stream   = ba::posix::stream_descriptor;
    using transport      = async_transport::point_iface<posix_stream>;
    using transport_sptr = std::shared_ptr<transport>;

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

class test_ss: public msctl::common::subsys_iface {
    ba::io_service &ios_;
public:
    test_ss( int , ba::io_service &ios )
        :ios_(ios)
    { }
    std::string name( ) const override { return "test"; }
    void init( )    override { }
    void start( )   override { }
    void stop( )    override { }

    static
    msctl::common::subsys_sptr create( msctl::agent::application *,
                                       int i, ba::io_service &ios )
    {
        return std::make_shared<test_ss>(i, std::ref(ios));
    }
};

int main( )
{
    try {

        ba::io_service ios;
        ba::io_service::work wrk(ios);
        auto tuntap = tuntap_transport::create( ios );

        msctl::agent::application root;
        root.subsys_add<test_ss>( 100, std::ref(ios) );

        root.subsys<test_ss>( ).start( );

        auto hdl = common::open_tun( "tun10" );
        if( hdl < 0 ) {
            std::perror( "tun_alloc" );
            return 1;
        }
        tuntap->get_stream( ).assign( hdl );
        tuntap->start_read( );

        while( ios.run_one( ) );
    } catch( const std::exception &ex ) {
        std::cerr << "Error: " << ex.what( );
    }


    return 0;
}

