#pragma once 

#if defined(_WIN32)

#include <windows.h>

namespace utilities {

struct charset {
    static std::basic_string<char> make_mb_string( LPCWSTR src,
                                                   UINT CodePage = CP_ACP )
    {
        int cch = WideCharToMultiByte( CodePage, 0, src, -1, 0, 0, 0, 0 );
        if( 0 != cch ) {
            std::vector<char> data;
            data.resize( cch + 1 );

            WideCharToMultiByte( CodePage, 0, src, -1, &data[0],
                                 (DWORD)data.size( ), 0, 0 );
            return &data.front( );
        }
        return "";
    }

    static std::basic_string<wchar_t> make_ws_string( LPCSTR src,
                                                      UINT CodePage = CP_ACP )
    {

        int cch = MultiByteToWideChar( CodePage, 0, src, -1, 0, 0 );
        if( 0 != cch ) {
            std::vector<wchar_t> data;

            data.resize( cch + 1 );
            MultiByteToWideChar( CodePage, 0, src, -1,
                                 &data[0], (DWORD)data.size( ));
            return &data.front( );
        }

        return L"";
    }

    static std::basic_string<char> make_utf8_string( const std::wstring &src )
    {
        return make_mb_string( src, CP_UTF8 );
    };

    static std::basic_string<char> make_utf8_string( const std::string &src )
    {
        return make_mb_string( make_ws_string( src ), CP_UTF8 );
    };

    static std::basic_string<char> make_utf8_string( LPCWSTR src )
    {
        return make_mb_string( src, CP_UTF8 );
    };
    static std::basic_string<char> make_utf8_string( LPCSTR src )
    {
        return std::basic_string<char>( src );
    };
    static std::basic_string<char> make_mb_string( LPCSTR src )
    {
        return src;
    }
    static std::basic_string<wchar_t> make_ws_string( LPCWSTR src )
    {
        return src;
    }
    static const std::string &make_mb_string( const std::string &src )
    {
        return src;
    }
    static const std::wstring &make_ws_string( const std::wstring &src )
    {
        return src;
    }
    static std::wstring make_ws_string( const std::string &src,
                                        UINT CodePage = CP_ACP )
    {
        return make_ws_string( src.c_str( ), CodePage );
    }
    static std::string make_mb_string( const std::wstring &src,
                                       UINT CodePage = CP_ACP )
    {
        return make_mb_string( src.c_str( ), CodePage );
    }

};

}

#endif
