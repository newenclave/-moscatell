#include "../tuntap.h"
#if defined(_WIN32)

#include <iostream>

#include <WinSock2.h>
#include <winioctl.h>
#include <windows.h>
#include <Ws2tcpip.h>
#include <tchar.h>

#include "win-ip-header.h"
#include "win-utils.h"

#include "../utilities.h"

#include "boost/asio/ip/address.hpp"

#define TAP_CONTROL_CODE(request, method) \
    CTL_CODE(FILE_DEVICE_UNKNOWN, request, method, FILE_ANY_ACCESS)
#define TAP_IOCTL_CONFIG_TUN       TAP_CONTROL_CODE(10, METHOD_BUFFERED)
#define TAP_IOCTL_SET_MEDIA_STATUS TAP_CONTROL_CODE( 6, METHOD_BUFFERED)

#define REGISTRY_CONTROL_PATH _T("SYSTEM\\CurrentControlSet\\Control")

#define TAP_ADAPTER_KEY REGISTRY_CONTROL_PATH \
        _T("\\Class\\{4D36E972-E325-11CE-BFC1-08002BE10318}")

#define NETWORK_KEY REGISTRY_CONTROL_PATH \
    _T("\\Network\\{4D36E972-E325-11CE-BFC1-08002BE10318}")

#define TAP_DEVICE_SPACE     _T("\\\\.\\Global\\")
#define TAP_VERSION_ID_0801  _T("tap0801")
#define TAP_VERSION_ID_0901  _T("tap0901")
#define KEY_COMPONENT_ID     _T("ComponentId")
#define NET_CFG_INST_ID      _T("NetCfgInstanceId")

namespace msctl { namespace common {


namespace {

	using tstring = std::basic_string<TCHAR,
		                        std::char_traits<TCHAR>, 
		                        std::allocator<TCHAR> >;

	using charset = utilities::charset;

    std::string get_error_string( DWORD	code,
                                  HMODULE mod = NULL,
                                  DWORD lang = 0 )
    {
        std::string tmp;

        char *buf = NULL;
        DWORD len = 0;
        DWORD flags = FORMAT_MESSAGE_FROM_SYSTEM |
                FORMAT_MESSAGE_ALLOCATE_BUFFER |
                FORMAT_MESSAGE_IGNORE_INSERTS |
                FORMAT_MESSAGE_MAX_WIDTH_MASK;

        if( mod ) {
            flags |= FORMAT_MESSAGE_FROM_HMODULE;
        }

        len = FormatMessageA( flags, mod, code, lang,
                              reinterpret_cast<char *>(&buf),
                              0, NULL );

        if( (0 != len) && (NULL != buf) ) {
            tmp.assign( buf, buf + len );
            LocalFree( buf );
        }
        return tmp;
    }

    void throw_runtime( const std::string &add )
    {
        throw std::runtime_error( std::string( add )
                                  + ". "
                                  + get_error_string( GetLastError( ) ) );
    }

    struct win_reg_key {

        HKEY key = NULL;

        win_reg_key( const win_reg_key & ) = delete;
        win_reg_key &operator = ( const win_reg_key & ) = delete;

        win_reg_key( HKEY k )
        {
            assign( k );
        }

        win_reg_key( )
        { }

        void assign( HKEY k )
        {
            RegCloseKey( key );
            key = k;
        }

        win_reg_key &operator = ( HKEY k )
        {
            assign( k );
        }

        operator HKEY ( ) const
        {
            return key;
        }

        ~win_reg_key( )
        {
            if( key != NULL ) {
                RegCloseKey( key );
            }
        }

        HKEY *pget( )
        {
            return &key;
        }
    };

    struct bool_checker {
        std::string call_name;

        bool_checker( ) { }

        bool_checker( const std::string &name )
            :call_name( name )
        { }

        BOOL operator = ( BOOL s ) const
        {
            if( s == FALSE ) {
                throw_runtime( call_name );
            }
            return s;
        }

        bool_checker operator ( )( const std::string &name ) const
        {
            return bool_checker( call_name + "." + name );
        }
    };

    struct status_checker {
        std::string call_name;

        status_checker( ) { }

        status_checker( const std::string &name )
            :call_name( name )
        { }

        LSTATUS operator = ( LSTATUS s ) const
        {
            if( s != ERROR_SUCCESS ) {
                throw_runtime( call_name );
            }
            return s;
        }

        status_checker operator ( )( const std::string &name ) const
        {
            return status_checker( call_name + "." + name );
        }
    };

    tstring get_name( const tstring &dev_name )
    {
        static const tstring name = _T( "Name" );
        status_checker chker( "get_name" );
        DWORD dtype;

        tstring path = tstring( NETWORK_KEY ) + _T( "\\" )
                + dev_name + _T( "\\Connection" )
                ;
        win_reg_key key;
        chker( "RegOpenKeyEx" )
                = RegOpenKeyEx( HKEY_LOCAL_MACHINE, path.c_str( ),
                                0, KEY_READ, key.pget( ) );

        tstring res;
        res.resize( 256 );
        DWORD len = res.size( );
        chker( "RegQueryValueEx" )
                = RegQueryValueEx( key, name.c_str( ),
                                   NULL, &dtype,
                                   reinterpret_cast<LPBYTE>(&res[0]), &len );
        res.resize( len );
        return std::move( res );
    }

    tstring get_device( const tstring &hint, tstring &outname )
    {
        status_checker chker( "get_device" );
        win_reg_key key;
        chker( "RegOpenKeyEx" ) = RegOpenKeyEx( HKEY_LOCAL_MACHINE,
                                                TAP_ADAPTER_KEY, 0,
                                                KEY_READ, key.pget( ) );
        for( DWORD id = 0; ; id++ ) {

            TCHAR name[256];
            DWORD len = 256;
            DWORD dtype;
            tstring comp;

            auto status = RegEnumKeyEx( key, id, name,
                                        &len, NULL, NULL, NULL, NULL );
            if( status == ERROR_NO_MORE_ITEMS ) {
                break;
            }

            chker( "RegEnumKeyEx" ) = status;

            tstring unit = tstring( TAP_ADAPTER_KEY ) + _T( "\\" ) + name;
            win_reg_key unitkey;
            status = RegOpenKeyEx( HKEY_LOCAL_MACHINE, unit.c_str( ),
                                   0, KEY_READ, unitkey.pget( ) );
            if( status != ERROR_SUCCESS ) {
                continue;
            }

            len = 256;
            comp.resize( len );
            status = RegQueryValueEx( unitkey, KEY_COMPONENT_ID, NULL, &dtype,
                                      reinterpret_cast<LPBYTE>(&comp[0]),
                    &len );
            if( status != ERROR_SUCCESS || dtype != REG_SZ ) {
                continue;
            }
            comp.resize( len / sizeof( tstring::value_type ) - 1 );
            if( comp == TAP_VERSION_ID_0801 || comp == TAP_VERSION_ID_0901 ) {
                tstring device;
                len = 1024;
                device.resize( len );
                status = RegQueryValueEx( unitkey, NET_CFG_INST_ID,
                                          NULL, &dtype,
                                          reinterpret_cast<LPBYTE>(&device[0]),
                        (DWORD *)&len );
                if( status != ERROR_SUCCESS || dtype != REG_SZ ) {
                    continue;
                }
                device.resize( len / sizeof( tstring::value_type ) - 1 );
                auto hname = get_name( device );

                if( !hint.empty( ) ) {
                    if( hname != hint ) {
                        continue;
                    }
                }
                outname.swap( hname );
                return std::move( device );
            }
        }
        return tstring( );
    }

    HANDLE open_tun( const tstring &tun_device, tstring &name )
    {
        auto device = get_device( tun_device, name );

        if( !device.empty( ) ) {
            tstring tuntap = tstring( TAP_DEVICE_SPACE )
                    + device + _T( ".tap" );
            return CreateFile( tuntap.c_str( ),
                               GENERIC_WRITE | GENERIC_READ, 0, 0,
                               OPEN_EXISTING,
                               FILE_ATTRIBUTE_SYSTEM | FILE_FLAG_OVERLAPPED,
                               NULL );
        }
        return INVALID_HANDLE_VALUE;
    }

} // namespace =======================================================

    src_dest_v4 extract_ip_v4( const char *data, size_t len )
    {
        auto hdr = reinterpret_cast<const IPV4_HDR *>(data);
        if( extract_family( data, len ) != 4 ) {
            return src_dest_v4( );
        }
        return std::make_pair( hdr->saddr,
                               hdr->daddr );
    }

    int extract_family( const char *data, size_t len )
    {
        auto hdr = reinterpret_cast<const IPV4_HDR *>(data);
        if( len < sizeof( *hdr ) ) {
            return -1;
        }
        return hdr->version;
    }

    device_info open_tun( const std::string &hint_name )
    {
        device_info res;
        tstring name;
		auto hdl = open_tun( hint_name, name );
        if( hdl == INVALID_HANDLE_VALUE ) {
            throw_runtime( "open_tun" );
        }
		res.assign( hdl );
		res.assign_name( charset::make_utf8_string( name ) );

        return std::move( res );
    }

    void close_handle( native_handle hdl )
    {
        CloseHandle( hdl );
    }

    void setup_device( native_handle dev,
                       const std::string &name,
                       const std::string &ip,
                       const std::string &otherip,
                       const std::string &mask )
    {
        using av4  = boost::asio::ip::address_v4;
        av4 sip    = av4::from_string( ip );
        av4 sother = av4::from_string( otherip );
        av4 smask  = av4::from_string( mask );

        DWORD status = 1;
        DWORD len;
        DWORD ipdata[3] = {
            htonl( sip.to_ulong( ) ),
            htonl( sip.to_ulong( ) & smask.to_ulong( ) ),
            htonl( smask.to_ulong( ) )
        };

        bool_checker chkr( "setup_device" );

        chkr( "DeviceIoControl(TAP_IOCTL_SET_MEDIA_STATUS)" )
                = DeviceIoControl( dev,
                                   TAP_IOCTL_SET_MEDIA_STATUS, &status,
                                   sizeof( status ), &status,
                                   sizeof( status ), &len, NULL );

        chkr( "DeviceIoControl(TAP_IOCTL_CONFIG_TUN)" )
                = DeviceIoControl( dev,
                                   TAP_IOCTL_CONFIG_TUN, &ipdata,
                                   sizeof( ipdata ), &ipdata,
                                   sizeof( ipdata ), &len, NULL );

		std::ostringstream cmd;
		auto ws = charset::make_ws_string( name, CP_UTF8 );
		auto mb = charset::make_mb_string( ws );

		using utilities::decorators::quote;

		/// netsh interface ip set address "iface name" static ip mask > NUL
		cmd << "netsh interface ip set address " << quote( mb, '"' )
			<< " static "
			<< sip.to_string( ).c_str( ) << " "
			<< smask.to_string( ).c_str( )
			<< " > NUL"
			;
        system( cmd.str( ).c_str( ) );
    }

    int del_tun( const std::string &name ) /// not supported
    {
        return 0;
    }

    int device_up( const std::string &name ) /// not supported
    {
        return 0;
    }
}}


#endif
