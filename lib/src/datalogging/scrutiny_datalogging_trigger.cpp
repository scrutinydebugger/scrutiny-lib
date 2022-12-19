//    scrutiny_datalogging_trigger.cpp
//        The implementation of the datalogging trigger conditions operators
//
//   - License : MIT - See LICENSE file.
//   - Project : Scrutiny Debugger (github.com/scrutinydebugger/scrutiny-embedded)
//
//   Copyright (c) 2021-2022 Scrutiny Debugger

#include "scrutiny_tools.hpp"
#include "scrutiny_main_handler.hpp"
#include "datalogging/scrutiny_datalogging.hpp"
#include "datalogging/scrutiny_datalogging_trigger.hpp"
#include "datalogging/scrutiny_datalogging_types.hpp"

#include "string.h"

#if SCRUTINY_ENABLE_DATALOGGING == 0
#error "Not enabled"
#endif
namespace scrutiny
{
    namespace datalogging
    {
        namespace trigger
        {
            namespace relational_operators
            {
                template <class T1, class T2>
                class eq
                {
                public:
                    static inline bool eval(const T1 v1, const T2 v2)
                    {
                        return v1 == v2;
                    }
                };

                template <class T1, class T2>
                class neq
                {
                public:
                    static inline bool eval(const T1 v1, const T2 v2)
                    {
                        return v1 != v2;
                    }
                };

                template <class T1, class T2>
                class gt
                {
                public:
                    static inline bool eval(const T1 v1, const T2 v2)
                    {
                        return v1 > v2;
                    }
                };

                template <class T1, class T2>
                class get
                {
                public:
                    static inline bool eval(const T1 v1, const T2 v2)
                    {
                        return v1 >= v2;
                    }
                };

                template <class T1, class T2>
                class lt
                {
                public:
                    static inline bool eval(const T1 v1, const T2 v2)
                    {
                        return v1 < v2;
                    }
                };

                template <class T1, class T2>
                class let
                {
                public:
                    static inline bool eval(const T1 v1, const T2 v2)
                    {
                        return v1 <= v2;
                    }
                };

            }

            template <template <class, class> class OPERATOR>
            bool RelationalCompare(const VariableTypeCompare operand_types[], const AnyTypeCompare operand_vals[])
            {
                // Now our values are stored either in a float32 or an integer of the biggest supported types.
                // Number of type comparison will greatly be reduced
                if (operand_types[0] == VariableTypeCompare::_float)
                {
                    if (operand_types[1] == VariableTypeCompare::_float)
                        return OPERATOR<float, float>::eval(operand_vals[0]._float, operand_vals[1]._float);
                    else if (operand_types[1] == VariableTypeCompare::_sint)
                        return OPERATOR<float, float>::eval(operand_vals[0]._float, static_cast<float>(operand_vals[1]._sint));
                    else if (operand_types[1] == VariableTypeCompare::_uint)
                        return OPERATOR<float, float>::eval(operand_vals[0]._float, static_cast<float>(operand_vals[1]._uint));
                }
                else if (operand_types[0] == VariableTypeCompare::_sint)
                {
                    if (operand_types[1] == VariableTypeCompare::_float)
                        return OPERATOR<float, float>::eval(static_cast<float>(operand_vals[0]._sint), operand_vals[1]._float);
                    else if (operand_types[1] == VariableTypeCompare::_sint)
                        return OPERATOR<int_biggest_t, int_biggest_t>::eval(operand_vals[0]._sint, operand_vals[1]._sint);
                    else if (operand_types[1] == VariableTypeCompare::_uint)
                        return OPERATOR<int_biggest_t, int_biggest_t>::eval(operand_vals[0]._sint, static_cast<int_biggest_t>(operand_vals[1]._uint));
                }
                else if (operand_types[0] == VariableTypeCompare::_uint)
                {
                    if (operand_types[1] == VariableTypeCompare::_float)
                        return OPERATOR<float, float>::eval(static_cast<float>(operand_vals[0]._uint), operand_vals[1]._float);
                    else if (operand_types[1] == VariableTypeCompare::_sint)
                        return OPERATOR<int_biggest_t, int_biggest_t>::eval(static_cast<int_biggest_t>(operand_vals[0]._uint), operand_vals[1]._sint);
                    else if (operand_types[1] == VariableTypeCompare::_uint)
                        return OPERATOR<uint_biggest_t, uint_biggest_t>::eval(operand_vals[0]._uint, operand_vals[1]._uint);
                }

                return false;
            }

            bool EqualCondition::evaluate(const VariableTypeCompare operand_types[], const AnyTypeCompare operand_vals[])
            {
                return RelationalCompare<relational_operators::eq>(operand_types, operand_vals);
            }

            bool NotEqualCondition::evaluate(const VariableTypeCompare operand_types[], const AnyTypeCompare operand_vals[])
            {
                return RelationalCompare<relational_operators::neq>(operand_types, operand_vals);
            }

            bool GreaterThanCondition::evaluate(const VariableTypeCompare operand_types[], const AnyTypeCompare operand_vals[])
            {
                return RelationalCompare<relational_operators::gt>(operand_types, operand_vals);
            }

            bool GreaterOrEqualThanCondition::evaluate(const VariableTypeCompare operand_types[], const AnyTypeCompare operand_vals[])
            {
                return RelationalCompare<relational_operators::get>(operand_types, operand_vals);
            }

            bool LessThanCondition::evaluate(const VariableTypeCompare operand_types[], const AnyTypeCompare operand_vals[])
            {
                return RelationalCompare<relational_operators::lt>(operand_types, operand_vals);
            }

            bool LessOrEqualThanCondition::evaluate(const VariableTypeCompare operand_types[], const AnyTypeCompare operand_vals[])
            {
                return RelationalCompare<relational_operators::let>(operand_types, operand_vals);
            }

            bool ChangeMoreThanCondition::evaluate(const VariableTypeCompare operand_types[], const AnyTypeCompare operand_vals[])
            {
                float float_val;

                if (operand_types[0] == VariableTypeCompare::_float)
                    float_val = static_cast<float>(operand_vals[0]._float);
                else if (operand_types[0] == VariableTypeCompare::_uint)
                    float_val = static_cast<float>(operand_vals[0]._uint);
                else if (operand_types[0] == VariableTypeCompare::_sint)
                    float_val = static_cast<float>(operand_vals[0]._sint);
                else
                    return false;

                if (m_initialized == false)
                {
                    m_initialized = true;
                    m_previous_val = float_val;
                }
                else
                {
                    float delta = 0;

                    if (operand_types[1] == VariableTypeCompare::_float)
                        delta = static_cast<float>(operand_vals[1]._float);
                    else if (operand_types[1] == VariableTypeCompare::_uint)
                        delta = static_cast<float>(operand_vals[1]._uint);
                    else if (operand_types[1] == VariableTypeCompare::_sint)
                        delta = static_cast<float>(operand_vals[1]._sint);

                    if (delta >= 0)
                    {
                        return (float_val + delta > m_previous_val);
                    }
                    else
                    {
                        return (float_val + delta < m_previous_val);
                    }
                }

                return false;
            }
        }
    }
}
