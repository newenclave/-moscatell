#ifndef TUNTAP_H
#define TUNTAP_H

#include <string>

namespace msctl { namespace common {

    int open_tun( const std::string &name );
    int open_tap( const std::string &name );

}}

#endif // TUNTAP_H
