#include <string>
#include <iostream>

#include "common/cmd-iface.h"
#include "boost/program_options.hpp"
#include "common/tuntap.h"

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

                int res = 0;

                mk_ = (vm.count( "mktun" ) != 0);
                rm_ = (vm.count( "rmtun" ) != 0);


                if( mk_ && rm_ ) {
                    std::cerr << "mktun conflicts with rmtun. "
                                 "Select something one\n";
                    return 1;
                }

                up_ = vm.count( "up" );

                if( rm_ ) {
                    std::cout << "Removing device " << device_ << "...";
                    res = common::del_tun( device_ );
                    std::cout << (res < 0 ? "FAILED" : "OK") << std::endl;
                    if( res < 0 ) {
                        std::perror( "rmtun" );
                    }
                    return (res < 0);
                } else if( mk_ ) {
                    std::cout << "Adding device " << device_ << "...";
                    res = common::open_tun( device_, true );
                    std::cout << (res < 0 ? "FAILED" : "OK") << std::endl;
                    if( res < 0 ) {
                        std::perror( "mktun" );
                        return 1;
                    } else {
                        close(res);
                    }
                }

                if( up_ ) {
                    std::cout << "Setting device up " << device_ << "...";
                    res = common::device_up( device_ );
                    up_ = false;
                    std::cout << (res < 0 ? "FAILED" : "OK") << std::endl;
                    if( res < 0 ) {
                        std::perror( "setup_tun" );
                        return 1;
                    }
                }

                if( vm.count( "set-ip4" ) ) {
                    std::cout << "Assigning addr to device "
                              << device_ << "...";

                    auto val = vm["set-ip4"].as<std::string>( );
                    auto mask = split_addr( val );

                    if( mask_.empty( ) ) {
                        mask_ = mask;
                    }

                    res = common::set_dev_ip4( device_, val );
                    std::cout << (res < 0 ? "FAILED" : "OK") << std::endl;
                    if( res < 0 ) {
                        std::perror( "set-ip4" );
                        return 1;
                    }
                }

                if( !mask_.empty( ) ) {
                    std::cout << "Assigning netmask to device "
                              << device_ << "...";

                    if( mask_.find( '.' ) != std::string::npos ) {
                        res = common::set_dev_ip4_mask( device_, mask_ );
                    } else {
                        auto mask_value =
                                boost::lexical_cast<std::uint32_t>( mask_ );
                        res = common::set_dev_ip4_mask( device_, mask_value );
                        if( mask_value > 32 ) {
                            std::cout << (res < 0 ? "FAILED" : "OK")
                                      << std::endl;
                            std::cerr << "netmask: Invalid argument";
                        }
                    }

                    std::cout << (res < 0 ? "FAILED" : "OK") << std::endl;
                    if( res < 0 ) {
                        std::perror( "netmask" );
                        return 1;
                    }
                }

                return res;
            }

            void opts( options_description &desc ) override
            {
                desc.add_options( )
                ("mktun", "make tun device; use --dev option for name")
                ("rmtun", "remove tun device; use --dev option for name")
                ("dev,d", po::value<std::string>( &device_ ),
                        "device name")
                ("set-ip4,i", po::value<std::string>( ),
                        "set ip (v4) address")
                ("up,U", "set interface up")
                ("netmask,n", po::value<std::string>( &mask_ ),
                        "set ip mask for device")
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
