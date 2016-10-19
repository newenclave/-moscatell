#ifndef SUBSYS_ROOT_H
#define SUBSYS_ROOT_H

#include <map>
#include <vector>
#include <mutex>

#include "subsys-iface.h"
#include "utilities.h"
#include "assert.h"

namespace msctl { namespace common {

    class subsys_root {

        using subsys_map  = std::map<std::uintptr_t, subsys_sptr>;
        using subsys_list = std::vector<subsys_sptr>;

        subsys_map  subsystems_;
        subsys_list order_;

        template <class Tgt, class Src>
        static Tgt poly_downcast ( Src * x )
        {
            assert( dynamic_cast<Tgt>(x) == x );  // logic error ?
            return static_cast<Tgt>(x);
        }

    public:

        virtual ~subsys_root( )
        { }

        template <typename S, typename ...Agrs>
        void subsys_add( Agrs && ...args )
        {
            auto id = utilities::type_uid<S>::uid( );
            auto ss = S::create(std::forward<Agrs>(args)...);

            auto ad = subsystems_.emplace( std::make_pair( id, ss ) );
            if( ad.second ) {
                order_.push_back( ad.first->second );
            }
        }

    public:
        template <typename S>
        S &subsys( )
        {
            auto id = utilities::type_uid<S>::uid( );
            auto f  = subsystems_.find( id );
            if( f == subsystems_.end( ) ) {
                throw std::runtime_error( "Invalid subsystem id" );
            }
            return *poly_downcast<S *>( f->second.get( ) );
        }

        void start( )
        {
            for( auto &s: order_ ) {
                s->start( );
            }
        }

        void stop( )
        {
            for( auto b(order_.rbegin( )), e(order_.rend( )); b != e; ++b ) {
                (*b)->stop( );
            }
        }

        void init( )
        {
            for( auto &s: order_ ) {
                s->init( );
            }
        }

    };
}}

#endif // SUBSYSROOT_H
