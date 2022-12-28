//    scrutiny_main_handler.hpp
//        The main scrutiny class to be manipulated by the user
//
//   - License : MIT - See LICENSE file.
//   - Project : Scrutiny Debugger (github.com/scrutinydebugger/scrutiny-embedded)
//
//   Copyright (c) 2021-2022 Scrutiny Debugger

#ifndef __SCRUTINY_HANDLER_H__
#define __SCRUTINY_HANDLER_H__

#include <stdint.h>

#include "scrutiny_setup.hpp"
#include "scrutiny_loop_handler.hpp"
#include "scrutiny_timebase.hpp"
#include "protocol/scrutiny_protocol.hpp"
#include "scrutiny_config.hpp"

#if SCRUTINY_ENABLE_DATALOGGING
#include "datalogging/scrutiny_datalogging.hpp"
#endif

namespace scrutiny
{
    class MainHandler
    {

    public:
        void init(const Config *config);
        bool get_rpv(const uint16_t id, RuntimePublishedValue *rpv) const;
        VariableType get_rpv_type(const uint16_t id) const;

        void process(const uint32_t timestep_us);

        void process_request(const protocol::Request *const request, protocol::Response *const response);
        protocol::ResponseCode process_get_info(const protocol::Request *const request, protocol::Response *const response);
        protocol::ResponseCode process_comm_control(const protocol::Request *const request, protocol::Response *const response);
        protocol::ResponseCode process_memory_control(const protocol::Request *const request, protocol::Response *const response);
        protocol::ResponseCode process_user_command(const protocol::Request *const request, protocol::Response *const response);
#if SCRUTINY_ENABLE_DATALOGGING
        void process_datalogging(void);
        bool read_memory(void *dst, const void *src, const uint32_t size) const;
        bool fetch_variable(const void *addr, const VariableType variable_type, AnyType *val) const;
        bool fetch_variable_bitfield(
            const void *addr,
            const VariableTypeType var_tt,
            const uint_fast8_t bitoffset,
            const uint_fast8_t bitsize,
            AnyType *val,
            VariableType *output_type) const;
#endif
        inline RpvReadCallback get_rpv_read_callback(void) const
        {
            return m_config.get_rpv_read_callback();
        }

        inline protocol::CommHandler *comm(void)
        {
            return &m_comm_handler;
        }

        inline Config *get_config(void) { return &m_config; }

    private:
        bool touches_forbidden_region(const MemoryBlock *block) const;
        bool touches_forbidden_region(const void *addr_start, const size_t length) const;
        bool touches_readonly_region(const MemoryBlock *block) const;
        bool touches_readonly_region(const void *addr_start, const size_t length) const;
        void check_config(void);

        Timebase m_timebase;
        protocol::CommHandler m_comm_handler;
        bool m_processing_request;
        bool m_disconnect_pending;
        Config m_config;
        bool m_enabled;
#if SCRUTINY_ACTUAL_PROTOCOL_VERSION == SCRUTINY_PROTOCOL_VERSION(1, 0)
        protocol::CodecV1_0 m_codec;
#endif

#if SCRUTINY_ENABLE_DATALOGGING
        datalogging::DataLogger m_datalogger;
#endif
    };
}

#endif