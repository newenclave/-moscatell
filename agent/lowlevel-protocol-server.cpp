#include "lowlevel-protocol-server.h"

#include "vtrc-common/vtrc-lowlevel-protocol-default.h"

#include "boost/system/error_code.hpp"

#include "protocol/lowlevel.pb.h"

#define LOG(lev) log_(lev, "protoS")
#define LOGINF   LOG(logger_impl::level::info)
#define LOGDBG   LOG(logger_impl::level::debug)
#define LOGERR   LOG(logger_impl::level::error)
#define LOGWRN   LOG(logger_impl::level::warning)
namespace msctl { namespace agent { namespace lowlevel {

    namespace {

        namespace vcomm = vtrc::common;

        using protocol_accessor   = vcomm::protocol_accessor;
        using system_closure_type = vcomm::system_closure_type;

        using void_call   = std::function<void(std::string)>;
        using stages_list = std::vector<void_call>;
        using call_ptr    = stages_list::iterator;
        using error_code  = VTRC_SYSTEM::error_code;

        std::string first_message( )
        {
            rpc::ll::hello mess;

            mess.set_hello_message( "Hello!" );

            auto res = mess.SerializeAsString( );
            return std::move(res);
        }

        struct impl: public vcomm::lowlevel::default_protocol {
            application  *app_;
            logger_impl  &log_;

            impl( application *app )
                :app_(app)
                ,log_(app->log( ))
            { }

            /// on_success
            ///     true:  call after  write
            ///     false: call before write
            void send_message( const std::string &mess, system_closure_type cb,
                               bool on_success = true )
            {
                accessor( )->write( mess, cb, on_success );
            }

            void send_message( const std::string &mess )
            {
                static auto default_cb = [this]( const error_code & ) { };
                send_message( std::move(mess), default_cb, true );
            }

            void init( protocol_accessor *pa, system_closure_type ready )
            {
                set_accessor( pa );
                const auto hello = first_message( );
                send_message( hello );
                accessor( )->ready( true );
                switch_to_ready(  );
                ready( error_code( ) );
            }

//            void do_handshake( )
//            {
//                std::string raw_message;

//                if(pop_raw_message( raw_message )) {
//                }
//                // (*stage_call_)( );
//            }

        };
    }

    vtrc::common::lowlevel::protocol_layer_iface *server_proto( application *a )
    {
        return new impl( a );
    }

}}}
