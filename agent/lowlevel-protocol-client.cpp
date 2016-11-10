#include "lowlevel-protocol-client.h"

#include "vtrc-common/vtrc-lowlevel-protocol-default.h"
#include "protocol/lowlevel.pb.h"

#include "vtrc-system.h"

#define LOG(lev) log_(lev, "protoC")
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

        struct impl: public vcomm::lowlevel::default_protocol {

            application *app_;
            logger_impl &log_;
            stages_list  calls_;
            call_ptr     stage_call_;

            impl( application *app )
                :app_(app)
                ,log_(app->log( ))
            {
                calls_.emplace_back(
                    [this]( std::string data ) {
                        recv_hello( std::move(data) );
                    } );
                stage_call_ = calls_.begin( );
            }

            void recv_hello( std::string data )
            {
                rpc::ll::hello mess;
                mess.ParseFromString( data );
                LOGINF << "Hello recv: " << mess.DebugString( );
                accessor( )->ready( true );
                switch_to_ready( );
            }

            void init( protocol_accessor *pa, system_closure_type cb )
            {
                set_accessor( pa );
                switch_to_handshake( );
                cb( error_code( ) );
            }

            void do_handshake( )
            {
                std::string raw_message;

                if( pop_raw_message( raw_message ) ) {
                    (*stage_call_)( std::move(raw_message) );
                } else {
                    LOGERR << "";
                }
                // (*stage_call_)( );
            }

        };
    }

    vtrc::common::lowlevel::protocol_layer_iface *client_proto( application *a )
    {
        return new impl( a );
    }

}}}

