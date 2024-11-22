//    scrutiny_main_handler.cpp
//        The main scrutiny class to be manipulated by the user.
//
//   - License : MIT - See LICENSE file.
//   - Project : Scrutiny Debugger (github.com/scrutinydebugger/scrutiny-embedded)
//
//   Copyright (c) 2021 Scrutiny Debugger

#include <string.h>
#include "scrutiny_setup.hpp"
#include "scrutiny_main_handler.hpp"
#include "scrutiny_software_id.hpp"
#include "scrutiny_common_codecs.hpp"
#include "scrutiny_loop_handler.hpp"

namespace scrutiny
{
    MainHandler::MainHandler(void) : m_timebase{},
                                     m_comm_handler{},
                                     m_processing_request{},
                                     m_disconnect_pending{},
                                     m_config{},
                                     m_enabled{},
                                     m_process_again_timestamp_taken{},
                                     m_process_again_timestamp{},
                                     m_codec{}
#if SCRUTINY_ENABLE_DATALOGGING
                                     ,
                                     m_datalogging{}
#endif
    {
    }

    void MainHandler::init(Config const *const config)
    {
        m_processing_request = false;
        m_disconnect_pending = false;
        m_process_again_timestamp_taken = false;
        m_config = *config;

        m_comm_handler.init(
            m_config.m_rx_buffer, m_config.m_rx_buffer_size,
            m_config.m_tx_buffer, m_config.m_tx_buffer_size,
            &m_timebase, m_config.session_counter_seed);

        check_config();
        if (!m_enabled)
        {
            m_comm_handler.disable();
        }

        // If there's an init error with the comm handler, we disable as well.
        if (!m_comm_handler.is_enabled())
        {
            m_enabled = false;
        }

        for (uint16_t i = 0; i < m_config.m_loop_count; i++)
        {
            m_config.m_loops[i]->init(this);
        }

#if SCRUTINY_ENABLE_DATALOGGING
        m_datalogging.datalogger.init(this, m_config.m_datalogger_buffer, m_config.m_datalogger_buffer_size, m_config.m_datalogger_trigger_callback);
        m_datalogging.owner = nullptr;
        m_datalogging.new_owner = nullptr;
        m_datalogging.error = DataloggingError::NoError;
        m_datalogging.request_arm_trigger = false;
        m_datalogging.request_ownership_release = false;
        m_datalogging.pending_ownership_release = false;
        m_datalogging.request_disarm_trigger = false;
        m_datalogging.reading_in_progress = false;
        m_datalogging.read_acquisition_rolling_counter = 0;

        m_datalogging.threadsafe_data.datalogger_state = m_datalogging.datalogger.get_state();
        m_datalogging.threadsafe_data.bytes_to_acquire_from_trigger_to_completion = 0;
        m_datalogging.threadsafe_data.write_counter_since_trigger = 0;
#endif
    }

    void MainHandler::check_config()
    {
        m_enabled = true;
        if (m_config.m_rx_buffer == nullptr || m_config.m_rx_buffer_size < protocol::MINIMUM_RX_BUFFER_SIZE || m_config.m_rx_buffer_size > protocol::MAXIMUM_RX_BUFFER_SIZE)
        {
            m_enabled = false;
        }

        if (m_config.m_tx_buffer == nullptr || m_config.m_tx_buffer_size < protocol::MINIMUM_TX_BUFFER_SIZE || m_config.m_tx_buffer_size > protocol::MAXIMUM_TX_BUFFER_SIZE)
        {
            m_enabled = false;
        }

        for (uint32_t i = 0; i < m_config.m_rpv_count; i++)
        {
            if (!tools::is_supported_type(m_config.m_rpvs[i].type))
            {
                m_enabled = false;
            }
        }
    }

#if SCRUTINY_ENABLE_DATALOGGING

    bool MainHandler::read_memory(void *dst, void const *const src, uint32_t const size) const
    {
        if (touches_forbidden_region(src, size))
        {
            return false;
        }

        memcpy(dst, src, size);
        return true;
    }

    bool MainHandler::fetch_variable(void const *const addr, VariableType const variable_type, AnyType *const val) const
    {
        uint8_t typesize = tools::get_type_size(variable_type);
        if (typesize == 0 || typesize > sizeof(AnyType))
        {
            return false;
        }

        if (read_memory(val, addr, typesize) == false)
        {
            memset(val, 0, sizeof(AnyType));
            return false;
        }
        return true;
    }

    bool MainHandler::fetch_variable_bitfield(
        void const *const addr,
        VariableTypeType const var_tt,
        uint_fast8_t const bitoffset,
        uint_fast8_t const bitsize,
        AnyType *const val,
        VariableType *const output_type) const
    {
        bool success = true;
        uint_fast8_t const fetch_required_size = ((bitoffset + bitsize - 1) >> 3) + 1;
        uint_fast8_t const output_required_size = ((bitsize - 1) >> 3) + 1;
        VariableTypeSize const fetch_type_size = tools::get_required_type_size(fetch_required_size);
        VariableTypeSize const output_type_size = tools::get_required_type_size(output_required_size);
        VariableType const fetch_variable_type = tools::make_type(VariableTypeType::_uint, fetch_type_size);
        VariableType const output_variable_type = tools::make_type(var_tt, output_type_size);

        if (touches_forbidden_region(addr, tools::get_type_size(fetch_type_size)))
        {
            success = false;
        }
        else if (bitsize == 0)
        {
            success = false;
        }
        else if (var_tt == VariableTypeType::_sint || var_tt == VariableTypeType::_uint || var_tt == VariableTypeType::_boolean)
        {
            success = fetch_variable(addr, fetch_variable_type, val);
            if (success)
            {
                if (fetch_type_size == VariableTypeSize::_8)
                {
                    val->uint8 >>= bitoffset;
                }
                else if (fetch_type_size == VariableTypeSize::_16)
                {
                    val->uint16 >>= bitoffset;
                }
                else if (fetch_type_size == VariableTypeSize::_32)
                {
                    val->uint32 >>= bitoffset;
                }
#if SCRUTINY_SUPPORT_64BITS
                else if (fetch_type_size == VariableTypeSize::_64)
                {
                    val->uint64 >>= bitoffset;
                }
#endif
                else // Unsupported
                {
                    success = false;
                }

                if (success)
                {
                    AnyTypeFast mask;
                    uint_fast8_t i;
                    if (output_type_size == VariableTypeSize::_8)
                    {
                        mask.uint8 = 1;
                        for (i = 1; i < bitsize; i++)
                        {
                            mask.uint8 |= (static_cast<uint_fast8_t>(1) << i);
                        }
                        val->uint8 &= mask.uint8;
                        if (var_tt == VariableTypeType::_sint)
                        {
                            if (val->uint8 >> (bitsize - 1))
                            {
                                val->uint8 |= (~mask.uint8);
                            }
                        }
                    }
                    else if (output_type_size == VariableTypeSize::_16)
                    {
                        mask.uint16 = 0x1FF;
                        for (i = 9; i < bitsize; i++)
                        {
                            mask.uint16 |= (static_cast<uint_fast16_t>(1) << i);
                        }
                        val->uint16 &= mask.uint16;
                        if (var_tt == VariableTypeType::_sint)
                        {
                            if (val->uint16 >> (bitsize - 1))
                            {
                                val->uint16 |= (~mask.uint16);
                            }
                        }
                    }
                    else if (output_type_size == VariableTypeSize::_32)
                    {
                        mask.uint32 = 0x1FFFF;
                        for (i = 17; i < bitsize; i++)
                        {
                            mask.uint32 |= (static_cast<uint_fast32_t>(1) << i);
                        }
                        val->uint32 &= mask.uint32;
                        if (var_tt == VariableTypeType::_sint)
                        {
                            if (val->uint32 >> (bitsize - 1))
                            {
                                val->uint32 |= (~mask.uint32);
                            }
                        }
                    }
#if SCRUTINY_SUPPORT_64BITS
                    else if (output_type_size == VariableTypeSize::_64)
                    {
                        mask.uint64 = 0x1FFFFFFFF;
                        for (i = 33; i < bitsize; i++)
                        {
                            mask.uint64 |= (static_cast<uint_fast64_t>(1) << i);
                        }
                        val->uint64 &= mask.uint64;
                        if (var_tt == VariableTypeType::_sint)
                        {
                            if (val->uint64 >> (bitsize - 1))
                            {
                                val->uint64 |= (~mask.uint64);
                            }
                        }
                    }
#endif
                    else // Unsupported
                    {
                        success = false;
                    }
                }
            }
        }

        if (!success)
        {
            *output_type = VariableType::unknown;
            memset(val, 0, sizeof(AnyType));
        }
        else
        {
            *output_type = output_variable_type;
        }

        return success;
    }

    void MainHandler::process_datalogging_loop_msg(LoopHandler *const sender, LoopHandler::Loop2MainMessage *const msg)
    {
        switch (msg->message_id)
        {
        case LoopHandler::Loop2MainMessageID::DATALOGGER_OWNERSHIP_TAKEN:
        {
            if (m_datalogging.owner != nullptr)
            {
                m_datalogging.error = DataloggingError::UnexpectedClaim;
            }
            m_datalogging.owner = sender;
            break;
        }
        case LoopHandler::Loop2MainMessageID::DATALOGGER_OWNERSHIP_RELEASED:
        {
            if (sender != m_datalogging.owner)
            {
                m_datalogging.error = DataloggingError::UnexpectedRelease;
            }

            m_datalogging.owner = nullptr;
            m_datalogging.datalogger.reset();
            m_datalogging.pending_ownership_release = false;
            break;
        }
        case LoopHandler::Loop2MainMessageID::DATALOGGER_STATUS_UPDATE:
        {
            m_datalogging.threadsafe_data.datalogger_state = msg->data.datalogger_status_update.state;
            m_datalogging.threadsafe_data.bytes_to_acquire_from_trigger_to_completion = msg->data.datalogger_status_update.bytes_to_acquire_from_trigger_to_completion;
            m_datalogging.threadsafe_data.write_counter_since_trigger = msg->data.datalogger_status_update.write_counter_since_trigger;
            if (m_datalogging.threadsafe_data.datalogger_state != datalogging::DataLogger::State::ACQUISITION_COMPLETED)
            {
                m_datalogging.reading_in_progress = false;
            }
            break;
        }
        default:
            break;
        }
    }

    void MainHandler::process_datalogging_logic(void)
    {
        if (m_datalogging.error != DataloggingError::NoError)
        {
            return;
        }

        if (m_datalogging.owner == nullptr) // no owner
        {
            // No owner, can read directly. Otherwise will be updated by an IPC message
            m_datalogging.threadsafe_data.datalogger_state = m_datalogging.datalogger.get_state();

            if (m_datalogging.new_owner != nullptr)
            {
                if (!m_datalogging.new_owner->ipc_main2loop()->has_content())
                {
                    LoopHandler::Main2LoopMessage msg;
                    msg.message_id = LoopHandler::Main2LoopMessageID::TAKE_DATALOGGER_OWNERSHIP;
                    m_datalogging.new_owner->ipc_main2loop()->send(msg);
                    m_datalogging.new_owner = nullptr;
                }
            }

            // No message from loop that can move these back to false.
            m_datalogging.request_arm_trigger = false;
            m_datalogging.request_disarm_trigger = false;
        }
        else
        {
            if (!m_datalogging.owner->ipc_main2loop()->has_content())
            {
                LoopHandler::Main2LoopMessage msg;
                if (m_datalogging.request_ownership_release)
                {
                    msg.message_id = LoopHandler::Main2LoopMessageID::RELEASE_DATALOGGER_OWNERSHIP;
                    m_datalogging.owner->ipc_main2loop()->send(msg);
                    m_datalogging.request_ownership_release = false;
                    m_datalogging.pending_ownership_release = true;
                }
                else if (m_datalogging.request_arm_trigger)
                {
                    msg.message_id = LoopHandler::Main2LoopMessageID::DATALOGGER_ARM_TRIGGER;
                    m_datalogging.owner->ipc_main2loop()->send(msg);
                    m_datalogging.request_arm_trigger = false;
                }
                else if (m_datalogging.request_disarm_trigger)
                {
                    msg.message_id = LoopHandler::Main2LoopMessageID::DATALOGGER_DISARM_TRIGGER;
                    m_datalogging.owner->ipc_main2loop()->send(msg);
                    m_datalogging.request_disarm_trigger = false;
                }
            }
        }
    }

#endif

    void MainHandler::process_loops(void)
    {
        for (uint_fast8_t i = 0; i < m_config.m_loop_count; i++)
        {
            LoopHandler *const loop = m_config.m_loops[i];
            if (loop->ipc_loop2main()->has_content())
            {
                LoopHandler::Loop2MainMessage msg = loop->ipc_loop2main()->pop();
                static_cast<void>(msg);
#if SCRUTINY_ENABLE_DATALOGGING
                process_datalogging_loop_msg(loop, &msg);
#endif
            }
        }
    }

    void MainHandler::process(timediff_t const timestep_100ns)
    {
        if (!m_enabled)
        {
            m_processing_request = false;
            m_disconnect_pending = false;
            m_comm_handler.reset();
#if SCRUTINY_ENABLE_DATALOGGING
            m_datalogging.datalogger.reset();
#endif
            return;
        }
        m_timebase.step(timestep_100ns);
        m_comm_handler.process();
        process_loops();
#if SCRUTINY_ENABLE_DATALOGGING
        process_datalogging_logic();
#endif

        if (m_comm_handler.request_received() && !m_processing_request)
        {
            protocol::Response *response = m_comm_handler.prepare_response();
            process_request(m_comm_handler.get_request(), response);

            if (static_cast<protocol::ResponseCode>(response->response_code) == protocol::ResponseCode::ProcessAgain)
            {
                m_processing_request = false;
                if (!m_process_again_timestamp_taken)
                {
                    m_process_again_timestamp = m_timebase.get_timestamp();
                    m_process_again_timestamp_taken = true;
                }
                else
                {
                    if (m_timebase.has_expired(m_process_again_timestamp, SCRUTINY_REQUEST_MAX_PROCESS_TIME_US * 10))
                    {
                        // Set only response code. All other fields are set in process_request()
                        response->response_code = static_cast<uint8_t>(protocol::ResponseCode::FailureToProceed);
                        m_comm_handler.send_response(response);
                        m_processing_request = true;
                    }
                }
                // comm handler will stay in standby until we process the request. Data in rx buffer is guaranteed to stay valid until then
            }
            else if (static_cast<protocol::ResponseCode>(response->response_code) == protocol::ResponseCode::NoResponseToSend)
            {
                m_processing_request = true;
                // Will not be transmitting, therefore automatically wait for next request below
            }
            else
            {
                m_processing_request = true;
                m_comm_handler.send_response(response);
            }
        }

        check_finished_sending();

        // Some commands affect loops and datalogging, so we reprocess right away
        process_loops();
#if SCRUTINY_ENABLE_DATALOGGING
        process_datalogging_logic();
#endif
    }

    void MainHandler::check_finished_sending(void)
    {
        if (m_processing_request)
        {
            if (!m_comm_handler.transmitting()) // Will be false if NoResponseToSend or if finished transmitting
            {
                m_comm_handler.wait_next_request(); // Allow reception of next request
                m_processing_request = false;
                m_process_again_timestamp_taken = false;

                if (m_disconnect_pending)
                {
                    m_comm_handler.disconnect();
                    m_disconnect_pending = false;
                }
            }
        }
    }

    bool MainHandler::rpv_exists(uint16_t const id) const
    {
        uint16_t const rpv_count = m_config.get_rpv_count();
        bool found = false;
        for (uint16_t i = 0; i < rpv_count; i++) // if unset this count will be 0
        {
            if (m_config.get_rpvs_array()[i].id == id)
            {
                found = true;
                break;
            }
        }

        return found;
    }

    bool MainHandler::get_rpv(uint16_t const id, RuntimePublishedValue *const rpv) const
    {
        uint16_t const rpv_count = m_config.get_rpv_count();
        bool found = false;
        for (uint16_t i = 0; i < rpv_count; i++) // if unset this count will be 0
        {
            if (m_config.get_rpvs_array()[i].id == id)
            {
                found = true;
                *rpv = m_config.get_rpvs_array()[i];
                break;
            }
        }

        return found;
    }

    VariableType MainHandler::get_rpv_type(uint16_t const id) const
    {
        RuntimePublishedValue rpv;
        bool const found = get_rpv(id, &rpv);
        return (found) ? rpv.type : VariableType::unknown;
    }

    void MainHandler::process_request(protocol::Request const *const request, protocol::Response *const response)
    {
        protocol::ResponseCode code = protocol::ResponseCode::FailureToProceed;
        response->reset();

        response->command_id = request->command_id;
        response->subfunction_id = request->subfunction_id;

        switch (static_cast<protocol::CommandId>(request->command_id))
        {
            // ============= [GetInfo] ============
        case protocol::CommandId::GetInfo:
            code = process_get_info(request, response);
            break;

            // ============= [CommControl] ============
        case protocol::CommandId::CommControl:
            code = process_comm_control(request, response);
            break;

            // ============= [MemoryControl] ============
        case protocol::CommandId::MemoryControl:
            code = process_memory_control(request, response);
            break;

#if SCRUTINY_ENABLE_DATALOGGING
            // ============= [DataLogControl] ===========
        case protocol::CommandId::DataLogControl:
            code = process_datalog_control(request, response);
            break;
#endif

            // ============= [UserCommand] ===========
        case protocol::CommandId::UserCommand:
            code = process_user_command(request, response);
            break;

            // ============================================
        default:
            code = protocol::ResponseCode::UnsupportedFeature;
            break;
        }

        response->response_code = static_cast<uint8_t>(code);
        if (code != protocol::ResponseCode::OK)
        {
            response->data_length = 0;
        }
    }

    // ============= [GetInfo] ============
    protocol::ResponseCode MainHandler::process_get_info(protocol::Request const *const request, protocol::Response *const response)
    {
        union
        {
            struct
            {
                protocol::ResponseData::GetInfo::GetProtocolVersion response_data;
            } get_protocol_version;

            struct
            {
                protocol::ResponseData::GetInfo::GetSpecialMemoryRegionCount response_data;
            } get_special_memory_region_count;

            struct
            {
                protocol::RequestData::GetInfo::GetSpecialMemoryRegionLocation request_data;
                protocol::ResponseData::GetInfo::GetSpecialMemoryRegionLocation response_data;
            } get_special_memory_region_location;

            struct
            {
                protocol::ResponseData::GetInfo::GetSupportedFeatures response_data;
            } get_supported_features;

            struct
            {
                protocol::ResponseData::GetInfo::GetRPVCount response_data;
            } get_rpv_count;

            struct
            {
                RuntimePublishedValue const *rpvs;
                protocol::RequestData::GetInfo::GetRPVDefinition request_data;
                protocol::GetRPVDefinitionResponseEncoder *response_encoder;
            } get_prv_def;

            struct
            {
                protocol::ResponseData::GetInfo::GetLoopCount response_data;
            } get_loop_count;

            struct
            {
                protocol::RequestData::GetInfo::GetLoopDefinition request_data;
                protocol::ResponseData::GetInfo::GetLoopDefinition response_data;
            } get_loop_def;

        } stack;

        protocol::ResponseCode code = protocol::ResponseCode::FailureToProceed;

        switch (static_cast<protocol::GetInfo::Subfunction>(request->subfunction_id))
        {
            // =========== [GetprotocolVersion] ==========
        case protocol::GetInfo::Subfunction::GetprotocolVersion:
        {
            stack.get_protocol_version.response_data.major = SCRUTINY_PROTOCOL_VERSION_MAJOR(SCRUTINY_ACTUAL_PROTOCOL_VERSION);
            stack.get_protocol_version.response_data.minor = SCRUTINY_PROTOCOL_VERSION_MINOR(SCRUTINY_ACTUAL_PROTOCOL_VERSION);
            code = m_codec.encode_response_protocol_version(&stack.get_protocol_version.response_data, response);
            break;
        }

            // =========== [GetSoftwareID] ==========
        case protocol::GetInfo::Subfunction::GetSoftwareId:
        {
            code = m_codec.encode_response_software_id(response);
            break;
        }
            // =========== [GetSupportedFeatures] ==========
        case protocol::GetInfo::Subfunction::GetSupportedFeatures:
        {
            stack.get_supported_features.response_data.memory_write = m_config.memory_write_enable;
#if SCRUTINY_ENABLE_DATALOGGING
            stack.get_supported_features.response_data.datalogging = m_config.is_datalogging_configured() && m_config.has_at_least_one_loop_with_datalogging();
#else
            stack.get_supported_features.response_data.datalogging = false;
#endif
            stack.get_supported_features.response_data.user_command = m_config.is_user_command_callback_set();
#if SCRUTINY_SUPPORT_64BITS
            stack.get_supported_features.response_data._64bits = true;
#else
            stack.get_supported_features.response_data._64bits = false;
#endif

            code = m_codec.encode_response_supported_features(&stack.get_supported_features.response_data, response);
            break;
        }
            // =========== [GetSpecialMemoryRegionCount] ==========
        case protocol::GetInfo::Subfunction::GetSpecialMemoryRegionCount:
        {
            stack.get_special_memory_region_count.response_data.nbr_readonly_region = m_config.readonly_ranges_count();
            stack.get_special_memory_region_count.response_data.nbr_forbidden_region = m_config.forbidden_ranges_count();
            code = m_codec.encode_response_special_memory_region_count(&stack.get_special_memory_region_count.response_data, response);
            break;
        }

            // =========== [GetSpecialMemoryLocation] ==========
        case protocol::GetInfo::Subfunction::GetSpecialMemoryLocation:
        {
            code = m_codec.decode_request_get_special_memory_region_location(request, &stack.get_special_memory_region_location.request_data);
            if (code != protocol::ResponseCode::OK)
            {
                break;
            }

            if (static_cast<protocol::GetInfo::MemoryRegionType>(stack.get_special_memory_region_location.request_data.region_type) == protocol::GetInfo::MemoryRegionType::ReadOnly)
            {
                if (!m_config.is_readonly_address_range_set())
                {
                    code = protocol::ResponseCode::FailureToProceed;
                    break;
                }

                if (stack.get_special_memory_region_location.request_data.region_index >= m_config.readonly_ranges_count())
                {
                    code = protocol::ResponseCode::FailureToProceed;
                    break;
                }

                uint8_t const index = stack.get_special_memory_region_location.request_data.region_index;
                stack.get_special_memory_region_location.response_data.start = reinterpret_cast<uintptr_t>(m_config.readonly_ranges()[index].start);
                stack.get_special_memory_region_location.response_data.end = reinterpret_cast<uintptr_t>(m_config.readonly_ranges()[index].end);
            }
            else if (static_cast<protocol::GetInfo::MemoryRegionType>(stack.get_special_memory_region_location.request_data.region_type) == protocol::GetInfo::MemoryRegionType::Forbidden)
            {
                if (!m_config.is_forbidden_address_range_set())
                {
                    code = protocol::ResponseCode::FailureToProceed;
                    break;
                }

                if (stack.get_special_memory_region_location.request_data.region_index >= m_config.forbidden_ranges_count())
                {
                    code = protocol::ResponseCode::FailureToProceed;
                    break;
                }

                uint8_t const index = stack.get_special_memory_region_location.request_data.region_index;
                stack.get_special_memory_region_location.response_data.start = reinterpret_cast<uintptr_t>(m_config.forbidden_ranges()[index].start);
                stack.get_special_memory_region_location.response_data.end = reinterpret_cast<uintptr_t>(m_config.forbidden_ranges()[index].end);
            }
            else
            {
                code = protocol::ResponseCode::InvalidRequest;
                break;
            }

            stack.get_special_memory_region_location.response_data.region_type = stack.get_special_memory_region_location.request_data.region_type;
            stack.get_special_memory_region_location.response_data.region_index = stack.get_special_memory_region_location.request_data.region_index;

            code = m_codec.encode_response_special_memory_region_location(&stack.get_special_memory_region_location.response_data, response);
            break;
        }
            // =================================

        case protocol::GetInfo::Subfunction::GetRuntimePublishedValuesCount:
        {
            stack.get_rpv_count.response_data.count = m_config.get_rpv_count();
            code = m_codec.encode_response_get_rpv_count(&stack.get_rpv_count.response_data, response);
            break;
        }
            // =================================

        case protocol::GetInfo::Subfunction::GetRuntimePublishedValuesDefinition:
        {
            code = m_codec.decode_request_get_rpv_definition(request, &stack.get_prv_def.request_data);
            if (code != protocol::ResponseCode::OK)
            {
                break;
            }

            stack.get_prv_def.response_encoder = m_codec.encode_response_get_rpv_definition(response, m_comm_handler.tx_buffer_size());

            if (stack.get_prv_def.request_data.start_index >= m_config.get_rpv_count())
            {
                code = protocol::ResponseCode::FailureToProceed;
                break;
            }

            if (stack.get_prv_def.request_data.start_index + stack.get_prv_def.request_data.count > m_config.get_rpv_count())
            {
                code = protocol::ResponseCode::FailureToProceed;
                break;
            }

            stack.get_prv_def.rpvs = m_config.get_rpvs_array();
            for (int32_t i = stack.get_prv_def.request_data.start_index; i < stack.get_prv_def.request_data.start_index + stack.get_prv_def.request_data.count; i++)
            {
                stack.get_prv_def.response_encoder->write(&stack.get_prv_def.rpvs[i]);
                if (stack.get_prv_def.response_encoder->overflow()) // If it doesn't fit the transmit buffer
                {
                    code = protocol::ResponseCode::Overflow;
                    break;
                }
            }

            break;
        }
            // =================================

        case protocol::GetInfo::Subfunction::GetLoopCount:
        {
            stack.get_loop_count.response_data.count = m_config.m_loop_count; // Should be 0 if not set by user.
            code = m_codec.encode_response_get_loop_count(&stack.get_loop_count.response_data, response);
            break;
        }

        case protocol::GetInfo::Subfunction::GetLoopDefinition:
        {
            m_codec.decode_request_get_loop_definition(request, &stack.get_loop_def.request_data);
            uint8_t const loop_id = stack.get_loop_def.request_data.loop_id;
            if (!m_config.is_loop_handlers_configured() || loop_id > m_config.m_loop_count)
            {
                code = protocol::ResponseCode::FailureToProceed;
                break;
            }

            const LoopType loop_type = m_config.m_loops[loop_id]->loop_type();
            stack.get_loop_def.response_data.loop_id = loop_id;
            stack.get_loop_def.response_data.loop_type = static_cast<uint8_t>(loop_type);
#if SCRUTINY_ENABLE_DATALOGGING
            stack.get_loop_def.response_data.support_datalogging = m_config.m_loops[loop_id]->datalogging_allowed();
#else
            stack.get_loop_def.response_data.support_datalogging = false;
#endif
            if (loop_type == LoopType::FIXED_FREQ)
            {
                stack.get_loop_def.response_data.loop_type_specific.fixed_freq.timestep_100ns = m_config.m_loops[loop_id]->get_timestep_100ns();
            }

            char const *const loop_name = m_config.m_loops[loop_id]->get_name();
            uint8_t const loop_name_length = static_cast<uint8_t>(tools::strnlen(loop_name, protocol::MAX_LOOP_NAME_LENGTH));
            stack.get_loop_def.response_data.loop_name_length = loop_name_length;
            stack.get_loop_def.response_data.loop_name = loop_name;

            code = m_codec.encode_response_get_loop_definition(&stack.get_loop_def.response_data, response);
            break;
        }

        default:
        {
            code = protocol::ResponseCode::UnsupportedFeature;
            break;
        }
        }

        return code;
    }

    // ============= [CommControl] ============
    protocol::ResponseCode MainHandler::process_comm_control(protocol::Request const *const request, protocol::Response *const response)
    {
        union
        {
            struct
            {
                protocol::RequestData::CommControl::Discover request_data;
                protocol::ResponseData::CommControl::Discover response_data;
            } discover;

            struct
            {
                protocol::RequestData::CommControl::Connect request_data;
                protocol::ResponseData::CommControl::Connect response_data;
            } connect;

            struct
            {
                protocol::ResponseData::CommControl::GetParams response_data;
            } get_params;

            struct
            {
                protocol::RequestData::CommControl::Heartbeat request_data;
                protocol::ResponseData::CommControl::Heartbeat response_data;
            } heartbeat;

            struct
            {
                protocol::RequestData::CommControl::Disconnect request_data;
            } disconnect;
        } stack;

        protocol::ResponseCode code = protocol::ResponseCode::FailureToProceed;

        switch (static_cast<protocol::CommControl::Subfunction>(request->subfunction_id))
        {
            // =========== [Discover] ==========
        case protocol::CommControl::Subfunction::Discover:
        {
            code = m_codec.decode_request_comm_discover(request, &stack.discover.request_data);
            if (code != protocol::ResponseCode::OK)
                break;

            // Magic validation is done by the codec.
            stack.discover.response_data.display_name = m_config.display_name;
            stack.discover.response_data.display_name_length = static_cast<uint8_t>(strnlen(m_config.display_name, scrutiny::protocol::MAX_DISPLAY_NAME_LENGTH));

            code = m_codec.encode_response_comm_discover(&stack.discover.response_data, response);
            break;
        }

            // =========== [Heartbeat] ==========
        case protocol::CommControl::Subfunction::Heartbeat:
        {
            code = m_codec.decode_request_comm_heartbeat(request, &stack.heartbeat.request_data);
            if (code != protocol::ResponseCode::OK)
                break;

            if (stack.heartbeat.request_data.session_id != m_comm_handler.get_session_id())
            {
                code = protocol::ResponseCode::InvalidRequest;
                break;
            }

            bool const success = m_comm_handler.heartbeat(stack.heartbeat.request_data.challenge);
            if (!success)
            {
                code = protocol::ResponseCode::InvalidRequest;
                break;
            }

            stack.heartbeat.response_data.session_id = m_comm_handler.get_session_id();
            stack.heartbeat.response_data.challenge_response = ~stack.heartbeat.request_data.challenge;

            code = m_codec.encode_response_comm_heartbeat(&stack.heartbeat.response_data, response);
            break;
        }

            // =========== [GetParams] ==========
        case protocol::CommControl::Subfunction::GetParams:
        {
            stack.get_params.response_data.data_tx_buffer_size = m_config.m_tx_buffer_size;
            stack.get_params.response_data.data_rx_buffer_size = m_config.m_rx_buffer_size;
            stack.get_params.response_data.max_bitrate = m_config.max_bitrate;
            stack.get_params.response_data.comm_rx_timeout = SCRUTINY_COMM_RX_TIMEOUT_US;
            stack.get_params.response_data.heartbeat_timeout = SCRUTINY_COMM_HEARTBEAT_TIMEOUT_US;
            stack.get_params.response_data.address_size = sizeof(void *);
            code = m_codec.encode_response_comm_get_params(&stack.get_params.response_data, response);
            break;
        }
            // =========== [Connect] ==========
        case protocol::CommControl::Subfunction::Connect:
        {
            code = m_codec.decode_request_comm_connect(request, &stack.connect.request_data);
            if (code != protocol::ResponseCode::OK)
            {
                break;
            }

            if (m_comm_handler.is_connected())
            {
                code = protocol::ResponseCode::Busy;
                break;
            }

            if (m_comm_handler.connect() == false)
            {
                code = protocol::ResponseCode::FailureToProceed;
                break;
            }

            stack.connect.response_data.session_id = m_comm_handler.get_session_id();
            memcpy(stack.connect.response_data.magic, protocol::CommControl::CONNECT_MAGIC, sizeof(protocol::CommControl::CONNECT_MAGIC));
            code = m_codec.encode_response_comm_connect(&stack.connect.response_data, response);
            break;
        }

            // =========== [Disconnect] ==========
        case protocol::CommControl::Subfunction::Disconnect:
        {
            code = m_codec.decode_request_comm_disconnect(request, &stack.disconnect.request_data);
            if (code != protocol::ResponseCode::OK)
                break;

            if (m_comm_handler.is_connected())
            {
                if (m_comm_handler.get_session_id() == stack.disconnect.request_data.session_id)
                {
                    m_disconnect_pending = true;
                }
                else
                {
                    code = protocol::ResponseCode::InvalidRequest;
                    break;
                }
            }

            // empty data
            code = protocol::ResponseCode::OK;
            break;
        }

            // =================================
        default:
        {
            code = protocol::ResponseCode::UnsupportedFeature;
            break;
        }
        }

        return code;
    }

    protocol::ResponseCode MainHandler::process_memory_control(protocol::Request const *const request, protocol::Response *const response)
    {
        protocol::ResponseCode code = protocol::ResponseCode::FailureToProceed;

        // Make sure the compiler optimize stack space. Because it may well not (don't trust this guy.)
        union
        {
            struct
            {
                protocol::ReadMemoryBlocksRequestParser *readmem_parser;
                protocol::ReadMemoryBlocksResponseEncoder *readmem_encoder;
                MemoryBlock block;
            } read_mem;

            struct
            {
                MemoryBlock block;
                protocol::WriteMemoryBlocksRequestParser *writemem_parser;
                protocol::WriteMemoryBlocksResponseEncoder *writemem_encoder;
            } write_mem;

            struct
            {
                protocol::ReadRPVRequestParser *readrpv_parser;
                protocol::ReadRPVResponseEncoder *readrpv_encoder;

                RuntimePublishedValue rpv;
                scrutiny::AnyType v;
                uint16_t id;
            } read_rpv;

            struct
            {
                protocol::WriteRPVRequestParser *writerpv_parser;
                protocol::WriteRPVResponseEncoder *writerpv_encoder;
                RuntimePublishedValue rpv;
                scrutiny::AnyType v;
            } write_rpv;

        } stack;

        switch (static_cast<protocol::MemoryControl::Subfunction>(request->subfunction_id))
        {
            // =========== [Read] ==========
        case protocol::MemoryControl::Subfunction::Read:
        {
            code = protocol::ResponseCode::OK;

            stack.read_mem.readmem_parser = m_codec.decode_request_memory_control_read(request);
            stack.read_mem.readmem_encoder = m_codec.encode_response_memory_control_read(response, m_comm_handler.tx_buffer_size());

            // We avoid playing in memory unless we are 100% sure the request is good.
            if (!stack.read_mem.readmem_parser->is_valid())
            {
                code = protocol::ResponseCode::InvalidRequest;
                break;
            }

            if (stack.read_mem.readmem_parser->required_tx_buffer_size() > m_comm_handler.tx_buffer_size())
            {
                code = protocol::ResponseCode::Overflow;
                break;
            }

            while (!stack.read_mem.readmem_parser->finished())
            {
                stack.read_mem.readmem_parser->next(&stack.read_mem.block);

                if (!stack.read_mem.readmem_parser->is_valid())
                {
                    code = protocol::ResponseCode::InvalidRequest;
                    break;
                }

                if (touches_forbidden_region(&stack.read_mem.block))
                {
                    code = protocol::ResponseCode::Forbidden;
                    break;
                }

                stack.read_mem.readmem_encoder->write(&stack.read_mem.block);

                if (stack.read_mem.readmem_encoder->overflow())
                {
                    code = protocol::ResponseCode::Overflow;
                    break;
                }
            }
            break;
        }

            // =========== [Write] ==========
        case protocol::MemoryControl::Subfunction::Write: // fall through
        case protocol::MemoryControl::Subfunction::WriteMasked:
        {
            bool const masked = static_cast<protocol::MemoryControl::Subfunction>(request->subfunction_id) == protocol::MemoryControl::Subfunction::WriteMasked;
            code = protocol::ResponseCode::OK;

            if (m_config.memory_write_enable == false)
            {
                code = protocol::ResponseCode::Forbidden;
                break;
            }

            stack.write_mem.writemem_parser = m_codec.decode_request_memory_control_write(request, masked);
            stack.write_mem.writemem_encoder = m_codec.encode_response_memory_control_write(response, m_comm_handler.tx_buffer_size());
            if (!stack.write_mem.writemem_parser->is_valid())
            {
                code = protocol::ResponseCode::InvalidRequest;
                break;
            }

            if (stack.write_mem.writemem_parser->required_tx_buffer_size() > m_comm_handler.tx_buffer_size())
            {
                code = protocol::ResponseCode::Overflow;
                break;
            }

            while (!stack.write_mem.writemem_parser->finished())
            {
                stack.write_mem.writemem_parser->next(&stack.write_mem.block);

                if (!stack.write_mem.writemem_parser->is_valid())
                {
                    code = protocol::ResponseCode::InvalidRequest;
                    break;
                }

                if (touches_forbidden_region(&stack.write_mem.block))
                {
                    code = protocol::ResponseCode::Forbidden;
                    break;
                }

                if (touches_readonly_region(&stack.write_mem.block))
                {
                    code = protocol::ResponseCode::Forbidden;
                    break;
                }

                stack.write_mem.writemem_encoder->write(&stack.write_mem.block);
                // We don't check overflow here as we rely on the request parser to be right on the required buffer size.

                if (!masked)
                {
                    memcpy(stack.write_mem.block.start_address, stack.write_mem.block.source_data, stack.write_mem.block.length);
                }
                else
                {
                    for (uint16_t i = 0; i < stack.write_mem.block.length; i++)
                    {
                        uint8_t temp;
                        temp = stack.write_mem.block.start_address[i];
                        temp |= (stack.write_mem.block.source_data[i] & stack.write_mem.block.mask[i]);    // Bit to 1
                        temp &= (stack.write_mem.block.source_data[i] | (~stack.write_mem.block.mask[i])); // Bit to 0
                        stack.write_mem.block.start_address[i] = temp;
                    }
                }
            }
            break;
        }

        // =========== [Read RPV] ==========
        case protocol::MemoryControl::Subfunction::ReadRPV:
        {
            code = protocol::ResponseCode::OK;
            if (!m_config.is_read_published_values_configured())
            {
                code = protocol::ResponseCode::UnsupportedFeature;
                break;
            }

            stack.read_rpv.readrpv_parser = m_codec.decode_request_memory_control_read_rpv(request);
            stack.read_rpv.readrpv_encoder = m_codec.encode_response_memory_control_read_rpv(response, m_comm_handler.tx_buffer_size());

            if (!stack.read_rpv.readrpv_parser->is_valid())
            {
                code = protocol::ResponseCode::InvalidRequest;
                break;
            }

            while (!stack.read_rpv.readrpv_parser->finished())
            {
                bool const ok_to_process = stack.read_rpv.readrpv_parser->next(&stack.read_rpv.id);

                if (!stack.read_rpv.readrpv_parser->is_valid())
                {
                    code = protocol::ResponseCode::InvalidRequest;
                    break;
                }

                if (ok_to_process)
                {
                    bool const rpv_found = get_rpv(stack.read_rpv.id, &stack.read_rpv.rpv);
                    if (!rpv_found)
                    {
                        code = protocol::ResponseCode::FailureToProceed;
                        break;
                    }

                    bool const callback_success = m_config.get_rpv_read_callback()(stack.read_rpv.rpv, &stack.read_rpv.v);
                    if (!callback_success)
                    {
                        code = protocol::ResponseCode::FailureToProceed;
                        break;
                    }

                    stack.read_rpv.readrpv_encoder->write(&stack.read_rpv.rpv, stack.read_rpv.v);

                    if (stack.read_rpv.readrpv_encoder->overflow())
                    {
                        code = protocol::ResponseCode::Overflow;
                        break;
                    }
                }
            }
            break;
        }

        case protocol::MemoryControl::Subfunction::WriteRPV:
        {
            code = protocol::ResponseCode::OK;
            if (!m_config.is_write_published_values_configured())
            {
                code = protocol::ResponseCode::UnsupportedFeature;
                break;
            }

            stack.write_rpv.writerpv_parser = m_codec.decode_request_memory_control_write_rpv(request, this);
            stack.write_rpv.writerpv_encoder = m_codec.encode_response_memory_control_write_rpv(response, m_comm_handler.tx_buffer_size());

            if (!stack.write_rpv.writerpv_parser->is_valid())
            {
                code = protocol::ResponseCode::InvalidRequest;
                break;
            }

            while (!stack.write_rpv.writerpv_parser->finished())
            {
                bool const ok_to_process = stack.write_rpv.writerpv_parser->next(&stack.write_rpv.rpv, &stack.write_rpv.v);

                if (!stack.write_rpv.writerpv_parser->is_valid())
                {
                    code = protocol::ResponseCode::InvalidRequest;
                    break;
                }

                if (!ok_to_process)
                {
                    continue;
                }

                bool const write_success = m_config.get_rpv_write_callback()(stack.write_rpv.rpv, &stack.write_rpv.v);
                if (!write_success)
                {
                    code = protocol::ResponseCode::FailureToProceed;
                    break;
                }

                stack.write_rpv.writerpv_encoder->write(&stack.write_rpv.rpv);

                if (stack.write_rpv.writerpv_encoder->overflow())
                {
                    code = protocol::ResponseCode::Overflow;
                    break;
                }
            }

            break;
        }

            // =================================
        default:
        {
            code = protocol::ResponseCode::UnsupportedFeature;
            break;
        }
        }

        return code;
    }

    bool MainHandler::touches_forbidden_region(MemoryBlock const *const block) const
    {
        return touches_forbidden_region(block->start_address, block->length);
    }

    bool MainHandler::touches_forbidden_region(void const *const addr_start, size_t const length) const
    {
        if (!m_config.is_forbidden_address_range_set())
        {
            return false;
        }

        uintptr_t const block_start = reinterpret_cast<uintptr_t>(addr_start);
        uintptr_t const block_end = block_start + length;

        for (unsigned int i = 0; i < m_config.forbidden_ranges_count(); i++)
        {
            AddressRange const &range = m_config.forbidden_ranges()[i];

            if (block_start >= reinterpret_cast<uintptr_t>(range.start) && block_start <= reinterpret_cast<uintptr_t>(range.end))
            {
                return true;
            }

            if (block_end >= reinterpret_cast<uintptr_t>(range.start) && block_end <= reinterpret_cast<uintptr_t>(range.end))
            {
                return true;
            }
        }
        return false;
    }

    bool MainHandler::touches_readonly_region(MemoryBlock const *const block) const
    {
        return touches_readonly_region(block->start_address, block->length);
    }

    bool MainHandler::touches_readonly_region(void const *const addr_start, size_t const length) const
    {
        if (!m_config.is_readonly_address_range_set())
        {
            return false;
        }

        uintptr_t const block_start = reinterpret_cast<uintptr_t>(addr_start);
        uintptr_t const block_end = block_start + length;
        for (unsigned int i = 0; i < m_config.readonly_ranges_count(); i++)
        {
            AddressRange const &range = m_config.readonly_ranges()[i];

            if (block_start >= reinterpret_cast<uintptr_t>(range.start) && block_start <= reinterpret_cast<uintptr_t>(range.end))
            {
                return true;
            }

            if (block_end >= reinterpret_cast<uintptr_t>(range.start) && block_end <= reinterpret_cast<uintptr_t>(range.end))
            {
                return true;
            }
        }
        return false;
    }

    protocol::ResponseCode MainHandler::process_user_command(protocol::Request const *const request, protocol::Response *const response)
    {
        protocol::ResponseCode code = protocol::ResponseCode::FailureToProceed;

        if (m_config.is_user_command_callback_set())
        {
            uint16_t response_data_length = 0;
            // Calling user callback;
            m_config.get_user_command_callback()(request->subfunction_id, request->data, request->data_length, response->data, &response_data_length, m_comm_handler.tx_buffer_size());
            if (response_data_length > m_comm_handler.tx_buffer_size())
            {
                code = protocol::ResponseCode::Overflow;
            }
            else
            {
                response->data_length = response_data_length;
                code = protocol::ResponseCode::OK;
            }
        }
        else
        {
            code = protocol::ResponseCode::UnsupportedFeature;
        }

        return code;
    }

#if SCRUTINY_ENABLE_DATALOGGING
    protocol::ResponseCode MainHandler::process_datalog_control(protocol::Request const *const request, protocol::Response *const response)
    {
        union
        {
            struct
            {
                protocol::ResponseData::DataLogControl::GetSetup response_data;
            } get_setup;

            struct
            {
                protocol::RequestData::DataLogControl::Configure request_data;
            } configure;

            struct
            {
                protocol::ResponseData::DataLogControl::GetStatus response_data;
            } get_status;

            struct
            {
                protocol::ResponseData::DataLogControl::GetAcquisitionMetadata response_data;
            } get_acq_metadata;

            struct
            {
                protocol::ResponseData::DataLogControl::ReadAcquisition response_data;
            } read_acquisition;

        } stack;

        if (!m_config.is_datalogging_configured())
        {
            return protocol::ResponseCode::UnsupportedFeature;
        }

        protocol::ResponseCode code = protocol::ResponseCode::FailureToProceed;
        switch (static_cast<protocol::DataLogControl::Subfunction>(request->subfunction_id))
        {

        case protocol::DataLogControl::Subfunction::GetSetup:
        {
            static_assert(sizeof(stack.get_setup.response_data.buffer_size) >= sizeof(m_config.m_datalogger_buffer_size), "Data won't fit in protocol");

            stack.get_setup.response_data.buffer_size = static_cast<uint32_t>(m_config.m_datalogger_buffer_size);
            stack.get_setup.response_data.data_encoding = static_cast<uint8_t>(m_datalogging.datalogger.get_encoder()->get_encoding());
            stack.get_setup.response_data.max_signal_count = SCRUTINY_DATALOGGING_MAX_SIGNAL;
            code = m_codec.encode_response_datalogging_get_setup(&stack.get_setup.response_data, response);
            break;
        }
        case protocol::DataLogControl::Subfunction::ConfigureDatalog:
        {
            m_datalogging.reading_in_progress = false; // Make sure to update this quickly because we can.

            // Make sure the datalogger is released before writing the config object to avoid race conditions.
            if (m_datalogging.owner != nullptr)
            {
                if (!m_datalogging.pending_ownership_release)
                {
                    m_datalogging.request_ownership_release = true;
                }
                code = protocol::ResponseCode::ProcessAgain;
                break;
            }

            code = m_codec.decode_datalogging_configure_request(request, &stack.configure.request_data, m_datalogging.datalogger.config());
            if (code != protocol::ResponseCode::OK)
            {
                break;
            }

            if (stack.configure.request_data.loop_id >= m_config.m_loop_count)
            {
                code = protocol::ResponseCode::FailureToProceed;
                break;
            }

            if (!m_config.m_loops[stack.configure.request_data.loop_id]->datalogging_allowed())
            {
                code = protocol::ResponseCode::Forbidden;
                break;
            }

            const datalogging::Configuration *const config = m_datalogging.datalogger.config();

            for (uint_fast8_t i = 0; i < config->trigger.operand_count; i++)
            {
                if (config->trigger.operands[i].type == datalogging::OperandType::VAR)
                {
                    if (touches_forbidden_region(config->trigger.operands[i].data.var.addr, tools::get_type_size(config->trigger.operands[i].data.var.datatype)))
                    {
                        code = protocol::ResponseCode::Forbidden;
                        break;
                    }
                }
                else if (config->trigger.operands[i].type == datalogging::OperandType::VARBIT)
                {
                    if (touches_forbidden_region(
                            config->trigger.operands[i].data.varbit.addr,
                            config->trigger.operands[i].data.varbit.bitoffset + config->trigger.operands[i].data.varbit.bitsize))
                    {
                        code = protocol::ResponseCode::Forbidden;
                        break;
                    }
                }
                else if (config->trigger.operands[i].type == datalogging::OperandType::RPV)
                {

                    if (!m_config.is_read_published_values_configured() || !rpv_exists(config->trigger.operands[i].data.rpv.id))
                    {
                        code = protocol::ResponseCode::FailureToProceed;
                        break;
                    }
                }
            }

            for (uint_fast8_t i = 0; i < config->items_count; i++)
            {
                if (config->items_to_log[i].type == datalogging::LoggableType::MEMORY)
                {
                    if (touches_forbidden_region(config->items_to_log[i].data.memory.address, config->items_to_log[i].data.memory.size))
                    {
                        code = protocol::ResponseCode::Forbidden;
                        break;
                    }
                }
                else if (config->items_to_log[i].type == datalogging::LoggableType::RPV)
                {
                    if (!m_config.is_read_published_values_configured() || !rpv_exists(config->items_to_log[i].data.rpv.id))
                    {
                        code = protocol::ResponseCode::FailureToProceed;
                        break;
                    }
                }
            }

            if (code != protocol::ResponseCode::OK)
            {
                break;
            }

            LoopHandler *const loop = m_config.m_loops[stack.configure.request_data.loop_id];
            m_datalogging.datalogger.configure(loop->get_timebase(), stack.configure.request_data.config_id); // Expect config object to be set

            if (m_datalogging.datalogger.config_valid())
            {
                m_datalogging.new_owner = loop; // Will trigger a request for ownership
            }
            else
            {
                code = protocol::ResponseCode::InvalidRequest;
            }
            break;
        }

        case protocol::DataLogControl::Subfunction::ArmTrigger:
        {

            if (m_datalogging.owner == nullptr || m_datalogging.pending_ownership_release)
            {
                code = protocol::ResponseCode::FailureToProceed;
                break;
            }

            // Do not wait on feedback from loop here on purpose
            // That would be additionnal complexity for minimal gain. We just don't arm if it can't be done. Keep silent.
            m_datalogging.request_arm_trigger = true;
            code = protocol::ResponseCode::OK;

            break;
        }
        case protocol::DataLogControl::Subfunction::DisarmTrigger:
        {

            if (m_datalogging.owner == nullptr || m_datalogging.pending_ownership_release)
            {
                code = protocol::ResponseCode::FailureToProceed;
                break;
            }

            // Do not wait on feedback from loop here on purpose
            // That would be additionnal complexity for minimal gain. We just don't arm if it can't be done. Keep silent.
            m_datalogging.request_disarm_trigger = true;
            code = protocol::ResponseCode::OK;

            break;
        }
        case protocol::DataLogControl::Subfunction::GetStatus:
        {
            static_assert(sizeof(stack.get_status.response_data.write_counter_since_trigger) >= sizeof(m_datalogging.threadsafe_data.write_counter_since_trigger), "Data cannot fit in protocol");
            static_assert(sizeof(stack.get_status.response_data.bytes_to_acquire_from_trigger_to_completion) >= sizeof(m_datalogging.threadsafe_data.bytes_to_acquire_from_trigger_to_completion), "Data cannot fit in protocol");

            stack.get_status.response_data.state = static_cast<uint8_t>(m_datalogging.threadsafe_data.datalogger_state);
            stack.get_status.response_data.bytes_to_acquire_from_trigger_to_completion = static_cast<uint32_t>(m_datalogging.threadsafe_data.bytes_to_acquire_from_trigger_to_completion);
            stack.get_status.response_data.write_counter_since_trigger = static_cast<uint32_t>(m_datalogging.threadsafe_data.write_counter_since_trigger);
            code = m_codec.encode_response_datalogging_status(&stack.get_status.response_data, response);
            break;
        }
        case protocol::DataLogControl::Subfunction::GetAcquisitionMetadata:
        {
            static_assert(sizeof(stack.get_acq_metadata.response_data.number_of_points) >= sizeof(datalogging::buffer_size_t), "Data won't fit in protocol");
            static_assert(sizeof(stack.get_acq_metadata.response_data.data_size) >= sizeof(datalogging::buffer_size_t), "Data won't fit in protocol");
            static_assert(sizeof(stack.get_acq_metadata.response_data.points_after_trigger) >= sizeof(datalogging::buffer_size_t), "Data won't fit in protocol");

            if (!datalogging_data_available())
            {
                code = protocol::ResponseCode::FailureToProceed;
                break;
            }
            const datalogging::DataReader *const reader = m_datalogging.datalogger.get_reader();

            stack.get_acq_metadata.response_data.acquisition_id = m_datalogging.datalogger.get_acquisition_id();
            stack.get_acq_metadata.response_data.config_id = m_datalogging.datalogger.get_config_id();
            stack.get_acq_metadata.response_data.number_of_points = reader->get_entry_count();
            stack.get_acq_metadata.response_data.data_size = reader->get_total_size();
            stack.get_acq_metadata.response_data.points_after_trigger = m_datalogging.datalogger.log_points_after_trigger();
            code = m_codec.encode_response_datalogging_get_acquisition_metadata(&stack.get_acq_metadata.response_data, response);
            break;
        }
        case protocol::DataLogControl::Subfunction::ReadAcquisition:
        {
            if (m_datalogging.owner == nullptr) // no owner
            {
                code = protocol::ResponseCode::FailureToProceed;
                break;
            }

            if (datalogging_data_available())
            {
                datalogging::DataReader *const reader = m_datalogging.datalogger.get_reader();
                if (m_datalogging.reading_in_progress == false)
                {
                    reader->reset();
                    m_datalogging.reading_in_progress = true;
                    m_datalogging.read_acquisition_rolling_counter = 0;
                    m_datalogging.read_acquisition_crc = 0;
                }

                stack.read_acquisition.response_data.acquisition_id = m_datalogging.datalogger.get_acquisition_id();
                stack.read_acquisition.response_data.reader = reader;
                stack.read_acquisition.response_data.rolling_counter = m_datalogging.read_acquisition_rolling_counter;
                stack.read_acquisition.response_data.crc = &m_datalogging.read_acquisition_crc;

                bool finished = false;
                code = m_codec.encode_response_datalogging_read_acquisition(&stack.read_acquisition.response_data, response, &finished);
                m_datalogging.read_acquisition_rolling_counter++;

                if (code != protocol::ResponseCode::OK)
                {
                    m_datalogging.reading_in_progress = false;
                    break;
                }

                if (finished)
                {
                    m_datalogging.reading_in_progress = false;
                }

                break;
            }
            else
            {
                code = protocol::ResponseCode::FailureToProceed;
                m_datalogging.reading_in_progress = false;
                break;
            }
            break;
        }

        case protocol::DataLogControl::Subfunction::ResetDatalogger:
        {
            if (m_datalogging.owner != nullptr)
            {
                if (!m_datalogging.pending_ownership_release)
                {
                    m_datalogging.request_ownership_release = true;
                }
                code = protocol::ResponseCode::ProcessAgain;
            }
            else
            {
                m_datalogging.datalogger.reset();
                code = protocol::ResponseCode::OK;
            }
            break;
        }
        default:
        {
            code = protocol::ResponseCode::UnsupportedFeature;
        }
        }

        if (static_cast<protocol::DataLogControl::Subfunction>(request->subfunction_id) == protocol::DataLogControl::Subfunction::ConfigureDatalog)
        {
            if (code != protocol::ResponseCode::OK && code != protocol::ResponseCode::ProcessAgain)
            {
                m_datalogging.datalogger.reset();
            }
        }

        return code;
    }
#endif

}