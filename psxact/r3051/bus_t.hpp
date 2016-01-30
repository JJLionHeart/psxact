#ifndef __r3051_bus_t_hpp__
#define __r3051_bus_t_hpp__

#include <stdint.h>

namespace r3051 {
    class cop0_t;

    enum bus_size_t {
        BYTE = 0,
        HALF = 1,
        WORD = 2
    };

    class bus_t {
    private:
        cop0_t &cop0;

    public:
        bus_t(cop0_t&);

        ~bus_t(void);

        uint32_t read_code(uint32_t);

        uint32_t read_data(bus_size_t, uint32_t);

        void write_data(bus_size_t, uint32_t, uint32_t);
    };
}

#endif