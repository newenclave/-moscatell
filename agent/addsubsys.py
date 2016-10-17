#!/usr/bin/env python
# -*- coding: utf-8 -*-

from sys import argv
import os

def header_file( ):
    """
#ifndef SUBSYS_%ss-name%_H
#define SUBSYS_%ss-name%_H

#include "application.h"

namespace msctl { namespace agent {

    class %ss-name%: public common::subsys_iface {

        struct          impl;
        friend struct   impl;
        impl           *impl_;

    public:

        %ss-name%( application *app );
        std::shared_ptr<%ss-name%> create( application *app );

    private:

        std::string name( ) const override
        {
            return "%ss-name%";
        }
        void init( )  override;
        void start( ) override;
        void stop( )  override;
    };

}}

#endif // SUBSYS_%ss-name%_H

    """
    return header_file.__doc__

def source_file( ):
    """
#include "subsys-%ss-name%.h"


#define LOG(lev) log_(lev, "%ss-name%") 
#define LOGINF   LOG(logger_impl::level::info)
#define LOGDBG   LOG(logger_impl::level::debug)
#define LOGERR   LOG(logger_impl::level::error)
#define LOGWRN   LOG(logger_impl::level::warning)

namespace msctl { namespace agent {

    struct %ss-name%::impl {
        application *app_;
        %ss-name%      *parent_;
    };

    %ss-name%::%ss-name%( application *app )
        :impl_(new impl)
    {
        impl_->app_ = app;
        impl_->parent_ = this;
    }

    void %ss-name%::init( )
    { }

    void %ss-name%::start( )
    { 
        impl_->LOGINF << "Started.";
    }

    void %ss-name%::stop( )
    { 
        impl_->LOGINF << "Stopped.";
    }
    
    std::shared_ptr<%ss-name%> %ss-name%::create( application *app )
    {
        return std::make_shared<%%ss-name>( app );
    }
}}

		"""
    return source_file.__doc__

def usage(  ):
    """
    usage: addsubsys.py <subsystem-name>
    """
    print( usage.__doc__ )

def fix_iface_inc( ss_name ):
    src_path = os.path.join( 'subsys.inc' )
    s = open( src_path, 'r' );
    content = s.readlines(  )
    s.close()
    content.append( '#include "subsys-'  + ss_name + '.h"\n')
    s = open( src_path, 'w' );
    s.writelines( content )

if __name__ == '__main__':
    if len( argv ) < 2:
        usage( )
        exit( 1 )

    ss_file = argv[1]
    ss_name = ss_file # ss_file.replace( '-', '_' )

    src_name = 'subsys-' + ss_file + '.cpp';
    hdr_name = 'subsys-' + ss_file + '.h';

    if os.path.exists( src_name ) or os.path.exists( hdr_name ):
        print ( "File already exists" )
        exit(1)

    src_content = source_file(  ).replace( '%ss-name%', ss_name )
    hdr_content = header_file(  ).replace( '%ss-name%', ss_name )

    s = open( src_name, 'w' );
    s.write( src_content )

    h = open( hdr_name, 'w' );
    h.write( hdr_content )

    fix_iface_inc( ss_name )

