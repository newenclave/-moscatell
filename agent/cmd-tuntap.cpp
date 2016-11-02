#include <string>
#include <iostream>

#include "common/cmd-iface.h"
#include "boost/program_options.hpp"
#include "common/tuntap.h"

#include "boost/algorithm/string.hpp"

namespace msctl { namespace agent { namespace cmd {

    namespace {

        namespace po = boost::program_options;

        struct impl: public common::cmd_iface {

            std::string device_;
            std::string mask_;
            bool mk_;
            bool rm_;
            bool up_ = false;

            std::string split_addr( std::string &addr )
            {
                auto pos = addr.find( '/' );
                if( pos != std::string::npos ) {
                    auto tmp = addr;
                    addr.resize( pos );
                    return std::string( tmp.begin( ) + pos + 1, tmp.end( ) );
                }
                return std::string( );
            }

            int run( const boost::program_options::variables_map &vm ) override
            {

                common::device_info res;

                mk_ = (vm.count( "mktun" ) != 0);
                rm_ = (vm.count( "rmtun" ) != 0);


                if( mk_ && rm_ ) {
                    std::cerr << "mktun conflicts with rmtun. "
                                 "Select something one\n";
                    return 1;
                }

                up_ = vm.count( "up" );

#ifndef _WIN32
                if( rm_ ) {
                    std::cout << "Removing device " << device_ << "...";
                    int del_res = common::del_tun( device_ );
                    std::cout << (del_res  < 0 ? "FAILED" : "OK") << std::endl;
                    if( del_res  < 0 ) {
                        std::perror( "rmtun" );
                    }
                    return (del_res  < 0);
                } else if( mk_ ) {
                    std::cout << "Adding device " << device_ << "...";
                    res = common::open_tun( device_ );
                    std::cout << "Ok";
                }

                if( up_ ) {
                    std::cout << "Setting device up " << device_ << "...";
                    common::device_up( device_ );
                    up_ = false;
                    std::cout << "Ok";
                }
#endif
                if( vm.count( "setup" ) ) {
                    std::cout << "Assigning addr to device "
                              << device_ << "...";

                    auto val = vm["setup"].as<std::string>( );

                    std::string ip1;
                    std::string ip2;
                    std::string mask;

                    std::vector<std::string> all;

                    boost::split( all, val, boost::is_any_of("/,") );

                    if( all.size( ) == 2 ) {
                        ip1 = ip2 = all[0];
                        mask = all[1];
                    } else if( all.size( ) == 3 ) {
                        ip1 =  all[0];
                        ip2 =  all[1];
                        mask = all[2];
                    } else {
                        std::cerr << "Invalid string format: " << val;
                        return 1;
                    }

                    using common::setup_device;
                    setup_device( common::TUN_HANDLE_INVALID_VALUE,
                                  device_,  ip1, ip2, mask );

                    std::cout << "OK" << std::endl;
                }

                return 1;
            }

            void opts( options_description &desc ) override
            {
                desc.add_options( )
#ifndef _WIN32
                ("mktun", "make tun device; use --dev option for name")
                ("rmtun", "remove tun device; use --dev option for name")
                ("up,U",  "set interface up")
#endif
                ("setup,i", po::value<std::string>( ),
                                "setup device ip1,ip2/mask or ip/mask; "
                                "192.168.0.1/255.255.255.0")
                ("dev,d", po::value<std::string>( &device_ ),
                        "device name")
                ;
            }

            std::string desc(  ) const
            {
                return "Commands for create or delete tun/tap devices.";
            }

        };
    }

    namespace tuntap {
        void create( common::cmd_map &all_map )
        {
            common::cmd_ptr ptr( new impl );
            all_map.emplace( std::make_pair( "tuntap", std::move(ptr) ) );
        }
    }

}}}
