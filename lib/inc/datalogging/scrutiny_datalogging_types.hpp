//    scrutiny_datalogging_types.hpp
//        Types used across datalogging feature
//
//   - License : MIT - See LICENSE file.
//   - Project : Scrutiny Debugger (github.com/scrutinydebugger/scrutiny-embedded)
//
//   Copyright (c) 2021-2022 Scrutiny Debugger

#ifndef ___SCRUTINY_DATALOGGING_TYPES_H___
#define ___SCRUTINY_DATALOGGING_TYPES_H___

#if SCRUTINY_ENABLE_DATALOGGING == 0
#error "Not enabled"
#endif

#include "scrutiny_types.hpp"

#include "string.h"

namespace scrutiny
{
    namespace datalogging
    {
        constexpr unsigned int MAX_OPERANDS = 2;

        enum class EncodingType
        {
            RAW
        };

        union AnyTypeCompare
        {
            uint_biggest_t _uint;
            int_biggest_t _sint;
            float _float;
        };

        enum class VariableTypeCompare
        {
            _float = static_cast<int>(scrutiny::VariableType::float32),
            _uint = static_cast<int>(scrutiny::BiggestUint),
            _sint = static_cast<int>(scrutiny::BiggestSint)
        };

        enum class OperandType
        {
            LITERAL,
            VAR,
            VARBIT,
            RPV
        };

        union OperandData
        {
            struct
            {
                float val;
            } literal;
            struct
            {
                void *addr;
                VariableType datatype;
            } var;
            struct
            {
                void *addr;
                VariableType datatype;
                uint8_t bitoffset;
                uint8_t bitsize;
            } varbit;
            struct
            {
                uint16_t id;
            } rpv;
        };

        struct Operand
        {
            OperandType type;
            OperandData data;
        };

        enum class SupportedTriggerConditions
        {
            Equal,
            NotEqual,
            LessThan,
            LessOrEqualThan,
            GreaterThan,
            GreaterOrEqualThan,
            ChangeMoreThan
        };

        struct TriggerConfig
        {
            void copy_from(TriggerConfig *other)
            {
                for (unsigned int i = 0; i < MAX_OPERANDS; i++)
                {
                    memcpy(&operands[i], &other->operands[i], sizeof(Operand));
                }

                condition = other->condition;
                operand_count = other->operand_count;
                hold_time_us = other->hold_time_us;
            }

            SupportedTriggerConditions condition;
            uint8_t operand_count;
            uint32_t hold_time_us;
            Operand operands[MAX_OPERANDS];
        };

        struct Configuration
        {
            void copy_from(Configuration *other)
            {
                block_count = other->block_count;
                decimation = other->decimation;
                decimation = other->probe_location;
                decimation = other->timeout_us;
                trigger.copy_from(&other->trigger);
                if (block_count <= SCRUTINY_DATALOGGING_MAX_BLOCK)
                {
                    for (unsigned int i = 0; i < block_count; i++)
                    {
                        memblocks[i] = other->memblocks[i];
                        blocksizes[i] = other->blocksizes[i];
                    }
                }
            }

            void *memblocks[SCRUTINY_DATALOGGING_MAX_BLOCK];
            uint16_t blocksizes[SCRUTINY_DATALOGGING_MAX_BLOCK];
            uint8_t block_count;
            uint16_t decimation;
            uint8_t probe_location;
            uint32_t timeout_us;
            TriggerConfig trigger;
        };

    }
}

#endif // ___SCRUTINY_DATALOGGING_TYPES_H___