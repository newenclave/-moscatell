#include <iostream>

#include "boost/asio.hpp"
#include "common/async-transport-point.hpp"
#include "application.h"

//#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <net/if.h>
#include <linux/if_tun.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/time.h>
#include <errno.h>
#include <stdarg.h>

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

        void on_read( const char * /*data*/, size_t length ) override
        {
            std::cout << "read " << length << " bytes\n";
            write( "\000000000", 10 );
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

    int tun_alloc( const char *dev, int flags )
    {

        struct ifreq ifr;
        int fd, err;
        const char *clonedev = "/dev/net/tun";

        if( (fd = open(clonedev , O_RDWR)) < 0 ) {
            perror("Opening /dev/net/tun");
            return fd;
        }

        memset(&ifr, 0, sizeof(ifr));

        ifr.ifr_flags = flags;

        if (*dev) {
            strncpy(ifr.ifr_name, dev, IFNAMSIZ);
        }

        if( (err = ioctl(fd, TUNSETIFF, (void *)&ifr)) < 0 ) {
            perror("ioctl(TUNSETIFF)");
            close(fd);
            return err;
        }

        return fd;
    }
}

int main( )
{
    try {
        ba::io_service ios;
        ba::io_service::work wrk(ios);
        auto tuntap = tuntap_transport::create( ios );

        auto hdl = tun_alloc( "tun10", IFF_TUN | IFF_NO_PI );
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

