#ifndef UTILITIES_H
#define UTILITIES_H

#include <string>
#include <cstdint>
#include <ostream>

#include "result.hpp"

namespace utilities {

    template <typename T>
    struct type_uid {
        typedef T type;
        static std::uintptr_t uid( )
        {
            static const char i = '!';
            return reinterpret_cast<std::uintptr_t>(&i);
        }
    };

    class address_v4_poll {

        std::uint32_t first_   = 0;
        std::uint32_t last_    = 0;
        std::uint32_t current_ = 0;
        std::uint32_t mask_    = 0;

    public:

        address_v4_poll             (                          ) = default;
        address_v4_poll             ( const address_v4_poll &  ) = default;
        address_v4_poll             (       address_v4_poll && ) = default;
        address_v4_poll &operator = ( const address_v4_poll &  ) = default;
        address_v4_poll &operator = (       address_v4_poll && ) = default;

        address_v4_poll( std::uint32_t first, std::uint32_t last,
                         std::uint32_t mask )
            :first_(first)
            ,last_(last)
            ,current_(first)
            ,mask_(mask)
        { }

        address_v4_poll( std::uint32_t net, std::uint32_t mask )
            :first_(net + 1)
            ,last_((~mask | net) - 1)
            ,current_(net + 1)
            ,mask_(mask)
        { }

        std::uint32_t current( ) const
        {
            return current_;
        }

        std::uint32_t last( ) const
        {
            return last_;
        }

        std::uint32_t mask( ) const
        {
            return mask_;
        }

        std::uint32_t next( )
        {
            return current_ != (last_ + 1) ? current_++ : 0;
        }

        void drop( )
        {
            if( current_ != first_ ) {
                --current_;
            }
        }

        bool end( ) const
        {
            return current_ == (last_ + 1);
        }

        bool begin( ) const
        {
            return current_ == first_;
        }

    };

    namespace decorators {

        template <typename T, typename Pre, typename Post>
        class output {
            T       value_;
            Pre     pre_;
            Post    pos_;
        public:
            output( T &&value, Pre &&pre, Post &&pos )
                :value_(value)
                ,pre_(pre)
                ,pos_(pos)

            { }

            std::ostream &print( std::ostream &o ) const
            {
                o << pre_ << value_ << pos_;
                return o;
            }
        };

        template <typename T, typename Pre, typename Post>
        inline
        std::ostream & operator << ( std::ostream &o,
                                     const output<T, Pre, Post> &d )
        {
            return d.print( o );
        }

        template <typename T, typename Pre, typename Post>
        inline
        output<T, Pre, Post> create( T &&value, Pre &&pre, Post &&pos )
        {
            return std::move( output<T, Pre, Post>(value, pre, pos) );
        }

        inline
        output<const char *, char, char> quote( const char *value,
                                                char sym = '\'' )
        {
            using res_type = output<const char *, char, char>;
            return std::move( res_type( std::move(value),
                                        std::move(sym),
                                        std::move(sym) ) );
        }

        inline
        output<const char *, char, char> quote( const std::string &value,
                                                char sym = '\'' )
        {
            return quote( value.c_str( ), sym );
        }
    }

    using h2b_result = result<std::string, const char *>;

    h2b_result bin2hex( void const *bytes, size_t length );
    h2b_result bin2hex( std::string const &input );
    h2b_result hex2bin( std::string const &input );

    namespace console {
        std::ostream &light ( std::ostream &s );
        std::ostream &red   ( std::ostream &s );
        std::ostream &green ( std::ostream &s );
        std::ostream &blue  ( std::ostream &s );
        std::ostream &cyan  ( std::ostream &s );
        std::ostream &yellow( std::ostream &s );
        std::ostream &none  ( std::ostream &s );
    }

    struct endpoint_info {
        enum ep_type {
             ENDPOINT_NONE   = 0
            ,ENDPOINT_LOCAL  = 1
            ,ENDPOINT_IP     = 2
            ,ENDPOINT_UDP    = 2 // hm
            ,ENDPOINT_TCP    = 2 //
        };

        enum ep_flags {
             FLAG_SSL   = 0x01
            ,FLAG_DUMMY = 0x01 << 1
        };

        std::string    addpess;
        std::uint16_t  service = 0;
        unsigned       flags   = 0;
        ep_type        type    = ENDPOINT_NONE;

        bool is_local( ) const noexcept
        {
            return type == ENDPOINT_LOCAL;
        }

        bool is_ip( ) const noexcept
        {
            return type == ENDPOINT_IP;
        }

        bool is_none( ) const noexcept
        {
            return type == ENDPOINT_NONE;
        }

        bool is_ssl( ) const noexcept
        {
            return !!(flags & FLAG_SSL);
        }

        bool is_dummy( ) const noexcept
        {
            return !!(flags & FLAG_DUMMY);
        }

        operator bool( ) const noexcept
        {
            return !is_none( );
        }

    };

    std::ostream & operator << ( std::ostream &os, const endpoint_info &ei );

    ///  0.0.0.0:12345             - tcp  endpoint (address:port)
    ///  0.0.0.0:12345             - tcp6 endpoint (address:port)
    /// @0.0.0.0:12345             - tcp  + ssl endpoint( !address:port )
    /// @:::12345                  - tcp6 + ssl endpoint( !address:port )
    ///  /home/data/fr.sock        - local endpoint
    ///                              (/local/socket or \\.\pipe\name )
    endpoint_info get_endpoint_info( const std::string &ep );

}

#endif // UTILS_H
