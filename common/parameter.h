#ifndef PARAMETER_H
#define PARAMETER_H

#include <memory>

namespace utilities {

    struct parameter {
        virtual ~parameter( ) { }
        virtual void apply( ) = 0;
    };
    using parameter_sptr = std::shared_ptr<parameter>;
}

#endif // PARAMETER_H
