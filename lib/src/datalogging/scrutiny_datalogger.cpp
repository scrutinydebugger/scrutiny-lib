//    scrutiny_datalogger.cpp
//        The implementation of the datalogger object that should exist in each loop
//
//   - License : MIT - See LICENSE file.
//   - Project : Scrutiny Debugger (github.com/scrutinydebugger/scrutiny-embedded)
//
//   Copyright (c) 2021 Scrutiny Debugger

#include "scrutiny_setup.hpp"
#include "datalogging/scrutiny_datalogger.hpp"
#include "datalogging/scrutiny_datalogging.hpp"
#include "scrutiny_ipc.hpp"
#include "scrutiny_tools.hpp"
#include "scrutiny_main_handler.hpp"

#if SCRUTINY_ENABLE_DATALOGGING == 0
#error "Not enabled"
#endif

namespace scrutiny
{
    namespace datalogging
    {
        void DataLogger::init(
            MainHandler const *const main_handler,
            uint8_t *const buffer,
            buffer_size_t const buffer_size,
            trigger_callback_t trigger_callback)
        {
            m_timebase = nullptr;
            m_main_handler = main_handler;
            m_buffer_size = buffer_size;
            m_trigger_callback = trigger_callback;

            m_encoder.init(main_handler, &m_config, buffer, buffer_size);
            m_acquisition_id = 0;

            reset();
        }

        void DataLogger::reset(void)
        {
            m_state = State::IDLE;
            m_trigger.previous_val = false;
            m_trigger.rising_edge_timestamp = 0;
            m_trigger.active_condition = nullptr;

            m_trigger_cursor_location = 0;
            m_trigger_timestamp = 0;
            m_remaining_data_to_write = 0;
            m_config_valid = false;
            m_manual_trigger = false;

            m_decimation_counter = 0;
            m_log_points_after_trigger = 0;
        }

        void DataLogger::configure(Timebase *timebase, uint16_t config_id)
        {
            reset();

            m_config_valid = true;
            m_timebase = timebase;
            m_config_id = config_id;

            if (m_config.items_count > SCRUTINY_DATALOGGING_MAX_SIGNAL || m_config.items_count == 0)
            {
                m_config_valid = false;
            }

            if (m_config.trigger.operand_count > datalogging::MAX_OPERANDS)
            {
                m_config_valid = false;
            }

            switch (m_config.trigger.condition)
            {
            case SupportedTriggerConditions::AlwaysTrue:
                m_trigger.active_condition = &m_trigger.conditions.always_true;
                break;
            case SupportedTriggerConditions::Equal:
                m_trigger.active_condition = &m_trigger.conditions.eq;
                break;
            case SupportedTriggerConditions::NotEqual:
                m_trigger.active_condition = &m_trigger.conditions.neq;
                break;
            case SupportedTriggerConditions::LessThan:
                m_trigger.active_condition = &m_trigger.conditions.lt;
                break;
            case SupportedTriggerConditions::LessOrEqualThan:
                m_trigger.active_condition = &m_trigger.conditions.let;
                break;
            case SupportedTriggerConditions::GreaterThan:
                m_trigger.active_condition = &m_trigger.conditions.gt;
                break;
            case SupportedTriggerConditions::GreaterOrEqualThan:
                m_trigger.active_condition = &m_trigger.conditions.get;
                break;
            case SupportedTriggerConditions::ChangeMoreThan:
                m_trigger.active_condition = &m_trigger.conditions.cmt;
                break;
            case SupportedTriggerConditions::IsWithin:
                m_trigger.active_condition = &m_trigger.conditions.within;
                break;
            default:
                m_trigger.active_condition = &m_trigger.conditions.always_true; // fallback to avoid nullptr;
                m_config_valid = false;
            }

            if (m_config.trigger.operand_count != m_trigger.active_condition->get_operand_count())
            {
                m_config_valid = false;
            }

            // Size are consistent so far, we can read the operand and items definition without crashing anything
            if (m_config_valid)
            {
                for (uint8_t i = 0; i < m_config.trigger.operand_count; i++)
                {
                    if (m_config.trigger.operands[i].type == OperandType::LITERAL)
                    {
                        if (!tools::is_float_finite(m_config.trigger.operands[i].data.literal.val))
                        {
                            m_config_valid = false;
                        }
                    }
                    else if (m_config.trigger.operands[i].type == OperandType::RPV)
                    {
                        if (!m_main_handler->get_config_ro()->is_read_published_values_configured())
                        {
                            m_config_valid = false;
                        }

                        if (!m_main_handler->rpv_exists(m_config.trigger.operands[i].data.rpv.id))
                        {
                            m_config_valid = false;
                        }
                    }
                    else if (m_config.trigger.operands[i].type == OperandType::VAR)
                    {
                        if (!tools::is_supported_type(m_config.trigger.operands[i].data.varbit.datatype))
                        {
                            m_config_valid = false;
                        }
                    }
                    else if (m_config.trigger.operands[i].type == OperandType::VARBIT)
                    {
                        // Works with and without 64bits support
                        if (m_config.trigger.operands[i].data.varbit.bitoffset > (sizeof(scrutiny::BiggestUint) * 8 - 1) || m_config.trigger.operands[i].data.varbit.bitsize > sizeof(scrutiny::BiggestUint) * 8)
                        {
                            m_config_valid = false;
                        }

                        if (!tools::is_supported_type(m_config.trigger.operands[i].data.varbit.datatype))
                        {
                            m_config_valid = false;
                        }

                        if (m_config.trigger.operands[i].data.varbit.bitoffset + m_config.trigger.operands[i].data.varbit.bitsize > tools::get_type_size(m_config.trigger.operands[i].data.varbit.datatype))
                        {
                            m_config_valid = false;
                        }
                    }
                    else
                    {
                        m_config_valid = false;
                    }
                }

                for (uint8_t i = 0; i < m_config.items_count; i++)
                {
                    if (m_config.items_to_log[i].type == LoggableType::RPV)
                    {
                        if (!m_main_handler->get_config_ro()->is_read_published_values_configured())
                        {
                            m_config_valid = false;
                        }

                        if (!m_main_handler->rpv_exists(m_config.items_to_log[i].data.rpv.id))
                        {
                            m_config_valid = false;
                        }
                    }
                    else if (m_config.items_to_log[i].type == LoggableType::MEMORY)
                    {
                        // Nothing to validate
                    }
                    else if (m_config.items_to_log[i].type == LoggableType::TIME)
                    {
                        // Nothing to validate
                    }
                    else
                    {
                        m_config_valid = false;
                    }
                }
            }

            // The configuration is good. Let's initialize to do an start logging
            if (m_config_valid)
            {
                m_encoder.set_timebase(m_timebase);
                m_trigger.active_condition->reset(m_trigger.conditions.data());
                m_encoder.reset();
                m_state = State::CONFIGURED;
            }
            else
            {
                m_state = State::ERROR;
            }
        }

        void DataLogger::arm_trigger(void)
        {
            if (m_state == State::CONFIGURED || m_state == State::ACQUISITION_COMPLETED || m_state == State::TRIGGERED)
            {
                m_state = State::ARMED;
            }
        }

        void DataLogger::disarm_trigger(void)
        {
            if (m_state == State::ARMED || m_state == State::ACQUISITION_COMPLETED || m_state == State::TRIGGERED)
            {
                m_state = State::CONFIGURED;
            }
        }

        void DataLogger::process(void)
        {
            // IDLE --> CONFIGURED --> ARMED --> TRIGGERED --> ACQUISITION_COMPLETED.
            // We acquire in both CONFIGURED and ARMED state so that we can have data before the trigger as well.

            switch (m_state)
            {
            case State::IDLE:
            case State::ERROR:
            case State::ACQUISITION_COMPLETED:
                break;
            case State::CONFIGURED:
            case State::ARMED:
            case State::TRIGGERED:
                if (m_encoder.error())
                {
                    m_state = State::ERROR;
                }
                else
                {
                    process_acquisition();
                    if (m_state == State::ARMED)
                    {
                        if (check_trigger())
                        {
                            if (m_trigger_callback != nullptr)
                            {
                                m_trigger_callback();
                            }
                            stamp_trigger_point();
                            m_state = State::TRIGGERED;
                        }
                    }

                    if (m_state == State::TRIGGERED)
                    {
                        if (acquisition_completed())
                        {
                            m_acquisition_id++;
                            m_state = State::ACQUISITION_COMPLETED;
                            m_log_points_after_trigger = m_encoder.get_entry_write_counter();
                        }
                    }
                    break;
                }
            }
        }

        void DataLogger::stamp_trigger_point(void)
        {
            m_trigger_cursor_location = m_encoder.get_write_cursor();
            m_trigger_timestamp = m_timebase->get_timestamp();
            m_encoder.reset_write_counter(); // Completion logic uses that counter directly without processing

            uint64_t const multiplier = static_cast<uint64_t>((1 << (sizeof(m_config.probe_location) * 8)) - 1 - m_config.probe_location);
            m_remaining_data_to_write = static_cast<buffer_size_t>((static_cast<uint64_t>(m_buffer_size) * multiplier) >> (sizeof(m_config.probe_location) * 8));
            if (!m_encoder.buffer_full())
            {
                m_remaining_data_to_write = SCRUTINY_MAX(m_remaining_data_to_write, m_encoder.remaining_bytes_to_full());
            }

            if (m_remaining_data_to_write > m_buffer_size)
            {
                m_remaining_data_to_write = m_buffer_size;
            }
        }

        bool DataLogger::acquisition_completed(void)
        {
            if (m_state == State::ACQUISITION_COMPLETED)
            {
                return true;
            }

            if (m_state == State::TRIGGERED)
            {
                if (m_config.timeout_100ns > 0)
                {
                    if (m_timebase->has_expired(m_trigger_timestamp, m_config.timeout_100ns))
                    {
                        return true;
                    }
                }

                if (m_encoder.get_data_write_counter() >= m_remaining_data_to_write)
                {
                    return true;
                }
            }

            return false;
        }

        datalogging::buffer_size_t DataLogger::get_bytes_to_acquire_from_trigger_to_completion(void) const
        {
            return (m_state == State::TRIGGERED) ? m_remaining_data_to_write : 0;
        }

        datalogging::buffer_size_t DataLogger::data_counter_since_trigger(void) const
        {
            // This counter gets reset when trigger happens.
            return (m_state == State::TRIGGERED) ? m_encoder.get_data_write_counter() : 0;
        }

        void DataLogger::process_acquisition(void)
        {
            if (++m_decimation_counter >= m_config.decimation)
            {
                m_encoder.encode_next_entry();
                m_decimation_counter = 0;
            }
        }

        bool DataLogger::check_trigger(void)
        {
            static_assert(MAX_OPERANDS >= 2, "Expect at least 2 operands for relational comparison");
            if (m_state != State::ARMED)
            {
                return false;
            }

            bool outval = false;
            AnyType opvals[MAX_OPERANDS];
            VariableType optypes[MAX_OPERANDS];
            const unsigned int nb_operand = m_trigger.active_condition->get_operand_count();

            if (nb_operand > MAX_OPERANDS)
            {
                return false;
            }

            if (m_manual_trigger)
            {
                m_manual_trigger = false;
                outval = true;
                m_trigger.previous_val = true;
            }
            else
            {

                for (unsigned int i = 0; i < nb_operand; i++)
                {
                    if (fetch_operand(m_main_handler, &m_config.trigger.operands[i], &opvals[i], &optypes[i]) == false)
                    {
                        return false;
                    }
                    convert_to_compare_type(&optypes[i], &opvals[i]);
                }

                bool condition_result = m_trigger.active_condition->evaluate(
                    m_trigger.conditions.data(),
                    reinterpret_cast<VariableTypeCompare *>(optypes),
                    reinterpret_cast<AnyTypeCompare *>(opvals));

                if (condition_result)
                {
                    if (m_trigger.previous_val == false)
                    {
                        m_trigger.rising_edge_timestamp = m_timebase->get_timestamp();
                    }

                    if (m_timebase->has_expired(m_trigger.rising_edge_timestamp, m_config.trigger.hold_time_100ns))
                    {
                        outval = true;
                    }
                }
                m_trigger.previous_val = condition_result;
            }
            return outval;
        }
    }
}