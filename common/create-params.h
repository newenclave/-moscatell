#ifndef CREATE_PARAMS_H
#define CREATE_PARAMS_H

#include "parameter.h"

#include <map>
#include <string>

namespace msctl { namespace common {

    struct create_parameters {
        using param_sptr = utilities::parameter_sptr;
        using param_map  = std::map<std::string, param_sptr>;
        param_map params;
    };

}}

#endif // CREATEPARAMS_H
