#ifndef CREATE_PARAMS_H
#define CREATE_PARAMS_H

#include "parameter.h"

#include <map>
#include <string>
#include <cstdint>

namespace msctl { namespace common {

    struct create_parameters {

        using param_sptr = utilities::parameter_sptr;
        using param_map  = std::map<std::string, param_sptr>;

        struct direction {

        };

        param_map     params;
        bool          tcp_nowait = false;
        std::uint32_t max_queue  = 10;

        direction rcv;
        direction snd;
    };

}}

#endif // CREATEPARAMS_H
