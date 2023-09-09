//    scrutiny_common_codecs.cpp
//        Some common encoding/decoding functions used across the project.
//
//   - License : MIT - See LICENSE file.
//   - Project : Scrutiny Debugger (github.com/scrutinydebugger/scrutiny-embedded)
//
//   Copyright (c) 2021-2023 Scrutiny Debugger

#include "scrutiny_setup.hpp"
#include "scrutiny_common_codecs.hpp"
#include "scrutiny_tools.hpp"
#include <stdint.h>

namespace scrutiny
{
    namespace codecs
    {
        uint8_t decode_address_big_endian(const uint8_t *buf, uintptr_t *addr)
        {
            constexpr unsigned int addr_size = sizeof(void *);
            static_assert(addr_size == 1 || addr_size == 2 || addr_size == 4 || addr_size == 8, "Unsupported address size");

            uintptr_t computed_addr = 0;
            unsigned int i = 0;

            if (addr_size >= 8)
            {
                computed_addr |= ((static_cast<uintptr_t>(buf[i++]) << ((addr_size >= 8) * 56)));
                computed_addr |= ((static_cast<uintptr_t>(buf[i++]) << ((addr_size >= 8) * 48)));
                computed_addr |= ((static_cast<uintptr_t>(buf[i++]) << ((addr_size >= 8) * 40)));
                computed_addr |= ((static_cast<uintptr_t>(buf[i++]) << ((addr_size >= 8) * 32)));
            }

            if (addr_size >= 4)
            {
                computed_addr |= ((static_cast<uintptr_t>(buf[i++]) << ((addr_size >= 4) * 24)));
                computed_addr |= ((static_cast<uintptr_t>(buf[i++]) << ((addr_size >= 4) * 16)));
            }

            if (addr_size >= 2)
            {
                computed_addr |= ((static_cast<uintptr_t>(buf[i++]) << ((addr_size >= 2) * 8)));
            }

            if (addr_size >= 1)
            {
                computed_addr |= ((static_cast<uintptr_t>(buf[i++]) << ((addr_size >= 1) * 0)));
            }

            *addr = computed_addr;

            return static_cast<uint8_t>(addr_size);
        }

        uint8_t encode_address_big_endian(const void *addr, uint8_t *buf)
        {
            return encode_address_big_endian(reinterpret_cast<uintptr_t>(addr), buf);
        }

        uint8_t encode_address_big_endian(const uintptr_t addr, uint8_t *buf)
        {
            constexpr unsigned int addr_size = sizeof(void *);
            static_assert(addr_size == 1 || addr_size == 2 || addr_size == 4 || addr_size == 8, "Unsupported address size");

            unsigned int i = addr_size - 1;

            if (addr_size >= 1)
            {
                buf[i--] = static_cast<uint8_t>((addr >> (0 * (addr_size >= 1))) & 0xFF);
            }

            if (addr_size >= 2)
            {
                buf[i--] = static_cast<uint8_t>((addr >> (8 * (addr_size >= 2))) & 0xFF);
            }

            if (addr_size >= 4)
            {
                buf[i--] = static_cast<uint8_t>((addr >> (16 * (addr_size >= 4))) & 0xFF);
                buf[i--] = static_cast<uint8_t>((addr >> (24 * (addr_size >= 4))) & 0xFF);
            }

            if (addr_size == 8)
            {
                buf[i--] = static_cast<uint8_t>((addr >> (32 * (addr_size >= 8))) & 0xFF);
                buf[i--] = static_cast<uint8_t>((addr >> (40 * (addr_size >= 8))) & 0xFF);
                buf[i--] = static_cast<uint8_t>((addr >> (48 * (addr_size >= 8))) & 0xFF);
                buf[i--] = static_cast<uint8_t>((addr >> (56 * (addr_size >= 8))) & 0xFF);
            }

            return static_cast<uint8_t>(addr_size);
        }

        uint8_t encode_anytype_big_endian(const AnyType *val, const VariableType vartype, uint8_t *buffer)
        {
            const uint8_t typesize = tools::get_type_size(vartype);
            return encode_anytype_big_endian(val, typesize, buffer);
        }

        uint8_t encode_anytype_big_endian(const AnyType *val, const uint8_t typesize, uint8_t *buffer)
        {

            switch (typesize)
            {
            case 1:
                *buffer = val->uint8;
                break;
            case 2:
                codecs::encode_16_bits_big_endian(val->uint16, buffer);
                break;
            case 4:

                codecs::encode_32_bits_big_endian(val->uint32, buffer);
                break;
#if SCRUTINY_SUPPORT_64BITS
                qqqqqqcase 8 : codecs::encode_64_bits_big_endian(val->uint64, buffer);
                break;
#endif
            default:
                return 0;
            }
            return typesize;
        }
    }
}
