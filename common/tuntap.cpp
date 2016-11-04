#include <iostream>

#include "tuntap.h"
#include "net-ifaces.h"

namespace msctl { namespace common {

    src_dest_v4 iface_v4_addr( const std::string &device )
    {
        auto all = utilities::get_system_ifaces( );
        for( auto &i: all ) {
            if( i.name( ) == device && i.is_v4( ) ) {
                return std::make_pair( htonl(i.v4( ).to_ulong( )),
                                       htonl(i.mask( ).to_v4( ).to_ulong( )));
            }
        }
        return src_dest_v4( );
    }

}}

