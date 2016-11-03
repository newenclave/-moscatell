#ifndef POSIXUTILS_H
#define POSIXUTILS_H

#if !defined(_WIN32)

#include <system_error>
#include <string>

namespace utilities {

    struct fd_keeper {
        int fd_ = -1;

        fd_keeper( )
        { }

        explicit fd_keeper( int fd )
            :fd_(fd)
        { }

        ~fd_keeper( )
        {
            if( fd_ != -1 ) {
                close( fd_ );
            }
        }

        operator int ( )
        {
            return fd_;
        }

        int release( )
        {
            int tmp = fd_;
            fd_ = -1;
            return tmp;
        }
    };


    inline
    void throw_errno( const std::string &name )
    {
        std::error_code ec(errno, std::system_category( ));
        throw std::runtime_error( name + " " + ec.message( ) );
    }

}

#endif

#endif // POSIXUTILS_H
