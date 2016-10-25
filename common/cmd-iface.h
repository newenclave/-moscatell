#ifndef CMD_IFACE_H
#define CMD_IFACE_H

#include <string>
#include <map>
#include <memory>

namespace boost { namespace program_options {
    class variables_map;
    class options_description;
}}

namespace msctl { namespace common {

    struct cmd_iface {

        using options_description = boost::program_options::options_description;
        using variables_map = boost::program_options::variables_map;

        cmd_iface( ) { }
        cmd_iface( const cmd_iface& )               = delete;
        cmd_iface & operator = ( const cmd_iface& ) = delete;
        cmd_iface( cmd_iface && )                   = delete;
        cmd_iface & operator = ( cmd_iface && )     = delete;
        virtual ~cmd_iface( ) { }

        virtual void opts( options_description &desc ) = 0;
        virtual int  run( const  variables_map &vm ) = 0;
        virtual std::string desc(  ) const = 0;
    };

    using cmd_ptr  = std::unique_ptr<cmd_iface>;
    using cmd_map  = std::map<std::string, common::cmd_ptr>;

}}

#endif // CMDIFACE_H

