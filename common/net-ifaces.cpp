#include "net-ifaces.h"
#include <iostream>

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#include <ws2tcpip.h>
#include <winnt.h>
#include <tchar.h>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

#else
#include <ifaddrs.h>
#include <sys/types.h>
#include <arpa/inet.h>
#endif

#include <map>

#include "os/win-utils.h"

namespace utilities {

namespace {

    namespace bai = boost::asio::ip;

    bai::address from_sock_addr4( const sockaddr *sa )
    {
        auto si = reinterpret_cast<const sockaddr_in *>(sa);
        return bai::address_v4( ntohl( si->sin_addr.s_addr ) );
    }

    bai::address from_sock_addr6( const sockaddr *sa )
    {
        auto si = reinterpret_cast<const sockaddr_in6 *>(sa);
        bai::address_v6::bytes_type bytes;
        std::copy( &si->sin6_addr.s6_addr[0],
                   &si->sin6_addr.s6_addr[bytes.max_size( )],
                   bytes.begin( ) );
        return bai::address_v6( bytes );
    }

    bai::address from_sock_addr( const sockaddr *sa )
    {
        switch( sa->sa_family ) {
        case AF_INET:
            return from_sock_addr4( sa );
        case AF_INET6:
            return from_sock_addr6( sa );
        }
        return bai::address( );
    }

#ifdef _WIN32
    typedef std::vector<MIB_IPADDRROW>         mib_table_type;
    typedef std::vector<PIP_ADAPTER_ADDRESSES> pip_table_type;

    bool fill_native_version( OSVERSIONINFO *info ) {

        LONG( __stdcall *NtRtlGetVersion )(PRTL_OSVERSIONINFOW);

        RTL_OSVERSIONINFOW oi = { 0 };

        oi.dwOSVersionInfoSize = sizeof( oi );

        auto ntdll   = GetModuleHandle( _T("ntdll.dll") );
        auto farproc = GetProcAddress( ntdll, "RtlGetVersion" );

        if( !farproc ) {
            return false;
        }

        NtRtlGetVersion = reinterpret_cast<decltype(NtRtlGetVersion)>(farproc);

        if( NtRtlGetVersion( &oi ) ) {
            return false;
        }

        if( info ) {
            info->dwBuildNumber  = oi.dwBuildNumber;
            info->dwMajorVersion = oi.dwMajorVersion;
            info->dwMinorVersion = oi.dwMinorVersion;
            info->dwPlatformId   = oi.dwPlatformId;
        }
        return true;
    };

    bool vista_or_higher( )
    {
        OSVERSIONINFO ov;
        return fill_native_version( &ov ) && (ov.dwMajorVersion >= 6);
    }

    static std::string make_mb_string( LPCWSTR src, UINT CodePage = CP_UTF8 )
    {
        int cch = WideCharToMultiByte( CodePage, 0, src, -1, 0, 0, 0, 0 );

        if( 0 == cch ) {
            return std::string( );
        } else {

            std::vector<char> data;
            data.resize( cch + 1 );
            cch = WideCharToMultiByte( CodePage, 0,
                                       src, -1, &data[0],
                                       (DWORD)data.size( ), 0, 0 );

            return cch ? &data.front( ) : "";
        }
    }

    bool get_addresses( iface_info_list &out, int family )
    {
        static const sockaddr_in6 mask0 = { 0 };
        const DWORD flags = 0
                | GAA_FLAG_SKIP_ANYCAST
                | GAA_FLAG_SKIP_MULTICAST
                | GAA_FLAG_SKIP_DNS_SERVER
                ;

        DWORD res = ERROR_BUFFER_OVERFLOW;
        ULONG size = 0;
        std::vector<char> tmp_data;

        iface_info_list tmp;

        while( res == ERROR_BUFFER_OVERFLOW ) {

            tdata.resize( size + 1 );

            auto p = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(&tdata[0]);
            res = GetAdaptersAddresses( family, flags, NULL, p, &size );

            if( res == ERROR_SUCCESS ) {

                if( vista_or_higher( ) ) { /// PIP_ADAPTER_ADDRESSES_LH has mask

                    using info_type = PIP_ADAPTER_ADDRESSES_LH;

                    auto p = reinterpret_cast<info_type>(&tdata[0]);
                    while( p ) {

                        auto addr = from_sock_addr( p->FirstUnicastAddress
                                                     ->Address.lpSockaddr );

                        auto family = p->FirstUnicastAddress
                                       ->Address.lpSockaddr->sa_family;

                        auto mask_bits = p->FirstUnicastAddress
                                          ->OnLinkPrefixLength;

                        auto mask = create_mask( family, mask_bits );

                        tmp.emplace_back( addr, mask,
                                  make_mb_string( p->FriendlyName, CP_UTF8 ),
                                  p->IfIndex );
                        p = p->Next;
                    }
                } else {
                    using info_type = PIP_ADAPTER_ADDRESSES;
                    auto p = reinterpret_cast<info_type>(&tdata[0]);
                    while( p ) {
                        tmp.emplace_back(
                                    p->FirstUnicastAddress->Address.lpSockaddr,
                                    reinterpret_cast<const sockaddr *>(&mask0),
                                    make_mb_string( p->FriendlyName, CP_UTF8 ),
                                    p->IfIndex );
                        p = p->Next;
                    }
                }

                out.swap( tmp );
            }
        }
        return res == NO_ERROR;
    }

    std::map<DWORD, std::string> adapter_names( int family )
    {
        iface_info_list lst;
        std::map<DWORD, std::string> res;
        if( get_addresses( lst, family ) ) {
            for( auto &ad: lst ) {
                res[ad.id( )] = ad.name( );
            }
        }
        return res;
    }

    bool get_addr_table( iface_info_list &out )
    {
        PMIB_IPADDRTABLE ptable = nullptr;
        iface_info_list tmp;

        DWORD size = 0;
        std::vector<char> tmp_data;
        DWORD res = ERROR_INSUFFICIENT_BUFFER;
        while( res == ERROR_INSUFFICIENT_BUFFER ) {
            tmp_data.resize( size + 1 );
            auto buf = reinterpret_cast<PMIB_IPADDRTABLE>(&tmp_data[0]);
            res = GetIpAddrTable( buf, &size, 0 );
            if( res == NO_ERROR ) {
                ptable = reinterpret_cast<PMIB_IPADDRTABLE>(&tmp_data[0]);
                auto anames = adapter_names( AF_INET );
                for( size_t b = 0; b < ptable->dwNumEntries; b++ ) {
                    sockaddr_in addr = { 0 };
                    sockaddr_in mask = { 0 };
                    addr.sin_family = AF_INET;
                    addr.sin_addr.s_addr = ptable->table[b].dwAddr;
                    mask.sin_addr.s_addr = ptable->table[b].dwMask;
                    tmp.emplace_back( reinterpret_cast<const sockaddr *>(&addr),
                                      reinterpret_cast<const sockaddr *>(&mask),
                                      anames[ptable->table[b].dwIndex],
                            ptable->table[b].dwIndex );
                }
                out.swap( tmp );
            }
        }
        return res == NO_ERROR;
    }

    iface_info_list get_all( )
    {
        iface_info_list v4;
        iface_info_list v6;
        get_addr_table( v4 );
        if( get_addresses( v6, AF_INET6 ) ) {
            v4.insert( v4.end( ), v6.begin( ), v6.end( ) );
        }
        return std::move(v4);
    }

#else

    bool enum_ifaces_( iface_info_list &out )
    {
        ifaddrs *addrs = nullptr;
        int res = ::getifaddrs( &addrs );
        if( 0 != res ) {
            //std::perror( "::getifaddrs" );
            return false;
        }

        iface_info_list tmp;
        ifaddrs *p = addrs;
        size_t id = 0;

        while( p  ) {
            if( p->ifa_addr ) {
            switch (p->ifa_addr->sa_family) {
                case AF_INET:
                case AF_INET6:
                    tmp.emplace_back( p->ifa_addr, p->ifa_netmask,
                                      p->ifa_name, id++ );
                    break;
                }
            }
            p = p->ifa_next;
        }
        ::freeifaddrs( addrs );
        out.swap( tmp );
        return true;
    }

    iface_info_list get_all( )
    {
        iface_info_list res;
        enum_ifaces_(res);
        return std::move(res);
    }

#endif
}

    iface_info::iface_info( const sockaddr *sa, const sockaddr *mask,
                            const std::string &name, size_t	id )
        :sockaddr_(from_sock_addr(sa))
        ,mask_(from_sock_addr( mask ))
        ,name_(name)
        ,id_(id)
    { }

    iface_info::iface_info( const address_type &sa,
                            const address_type &mask,
                            const std::string  &name, size_t id )
        :sockaddr_(sa)
        ,mask_(mask)
        ,name_( name )
        ,id_( id )
    { }

    bool iface_info::check( const address_type & test ) const
    {
        bool res = false;
        if( sockaddr_.is_v4( ) && test.is_v4( ) ) {
            auto sa = sockaddr_.to_v4( ).to_ulong( );
            auto ma = mask_.to_v4( ).to_ulong( );
            auto ta = test.to_v4( ).to_ulong( );
            res = ((sa & ma) == (ta & ma));
        } else if( sockaddr_.is_v6( ) && test.is_v6( ) ) {

#ifdef _WIN32
            bool has_mask = vista_or_higher( );
#else 
            bool has_mask = true;
#endif
            if( has_mask ) { /// vista has ipv6 masks

                auto sa = sockaddr_.to_v6( ).to_bytes( );
                auto ma = mask_.to_v6( ).to_bytes( );
                auto ta = test.to_v6( ).to_bytes( );

                /// something wrong!
                static_assert(sa.max_size( ) == 16,
                              "bytes_type::max_size( ) != 16");
                res = (sa[0x0] & ma[0x0]) == (ta[0x0] & ma[0x0])
                        && (sa[0x1] & ma[0x1]) == (ta[0x1] & ma[0x1])
                        && (sa[0x2] & ma[0x2]) == (ta[0x2] & ma[0x2])
                        && (sa[0x3] & ma[0x3]) == (ta[0x3] & ma[0x3])
                        && (sa[0x4] & ma[0x4]) == (ta[0x4] & ma[0x4])
                        && (sa[0x5] & ma[0x5]) == (ta[0x5] & ma[0x5])
                        && (sa[0x6] & ma[0x6]) == (ta[0x6] & ma[0x6])
                        && (sa[0x7] & ma[0x7]) == (ta[0x7] & ma[0x7])
                        && (sa[0x8] & ma[0x8]) == (ta[0x8] & ma[0x8])
                        && (sa[0x9] & ma[0x9]) == (ta[0x9] & ma[0x9])
                        && (sa[0xA] & ma[0xA]) == (ta[0xA] & ma[0xA])
                        && (sa[0xB] & ma[0xB]) == (ta[0xB] & ma[0xB])
                        && (sa[0xC] & ma[0xC]) == (ta[0xC] & ma[0xC])
                        && (sa[0xD] & ma[0xD]) == (ta[0xD] & ma[0xD])
                        && (sa[0xE] & ma[0xE]) == (ta[0xE] & ma[0xE])
                        && (sa[0xF] & ma[0xF]) == (ta[0xF] & ma[0xF])
                        ;
            } else { // XP, Seven
                return true; // always valid.
            }
        }
        return res;
    }

    iface_info_list get_system_ifaces( )
    {
        return std::move(get_all( ));
    }

    std::ostream &operator <<(std::ostream &o, const iface_info &info)
    {
        o << info.name( )  << " " << info.addr( ) << "/" << info.mask( );
        return o;
    }

    namespace {

        static const std::uint8_t mask_value[ ] = {
            0x00,
            0x80, 0xC0, 0xE0, 0xF0,
            0xF8, 0xFC, 0xFE, 0xFF,
        };

    }

    bai::address_v6 create_mask_v6( std::uint32_t bits )
    {
        if( bits > 128 ) {
            return bai::address_v6( );
        }

        bai::address_v6::bytes_type bytes { { 0, 0, 0, 0, 0, 0, 0, 0 } };
        for( std::uint32_t i=0; (i < bytes.max_size( )) && (0 != bits); i++ ) {
            if( bits >= 8 ) {
                bytes[i] = 0xFF;
                bits    -= 8;
            } else {
                bytes[i] = mask_value[bits];
                bits     = 0;
            }
        }
        bai::address_v6 res( bytes );
        return std::move( res );
    }

    bai::address_v4 create_mask_v4( std::uint32_t bits )
    {
        if( bits > 32 ) {
            return bai::address_v4( );
        }
        std::uint32_t res = 0;
        for( std::uint32_t i = 4; (i > 0) && (0 !=bits); i-- ) {
            if( bits >= 8 ) {
                res = res | (0xFF << ((i - 1) * 8));
                bits -= 8;
            } else {
                res = res | (mask_value[bits] << ((i - 1) * 8));
                bits  = 0;
            }
        }
        return bai::address_v4(res);
    }

    bai::address create_mask( int family, std::uint32_t bits )
    {
        bai::address res;
        switch( family ) {
        case AF_INET:
            res = create_mask_v4( bits );
            break;
        case AF_INET6:
            res = create_mask_v6( bits );
            break;
        }
        return std::move( res );
    }

}

