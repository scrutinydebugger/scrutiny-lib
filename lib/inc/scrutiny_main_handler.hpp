//    scrutiny_main_handler.hpp
//        The main scrutiny class to be manipulated by the user
//
//   - License : MIT - See LICENSE file.
//   - Project : Scrutiny Debugger (github.com/scrutinydebugger/scrutiny-embedded)
//
//   Copyright (c) 2021-2023 Scrutiny Debugger

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
        /// @brief Initialize the scrutiny Main Handler
        /// @param config A pointer to configuration object. A copy will be made, therefore given configuration can be on the stack.
        void init(const Config *config);

        /// @brief Gets the Runtime Published Definition from its ID
        /// @param id The RPV ID
        /// @param rpv The Runtime Published Value object to writes to
        /// @return true if the RPV has been found, false otherwise
        bool get_rpv(const uint16_t id, RuntimePublishedValue *rpv) const;

        /// @brief Tells if a Runtime Published Values with the given ID has been defined.
        /// @param id The RPV ID
        /// @return True if RPV exists in configuration.
        bool rpv_exists(const uint16_t id) const;

        /// @brief Returns the type of a Runtime Published Value identified by its ID
        /// @param id The RPV ID
        /// @return The VariableType object of the RPV.  VariableType::Unknown if the given ID is not set in the configuration.
        VariableType get_rpv_type(const uint16_t id) const;

        /// @brief Periodic process loop to be called as fast as possible
        /// @param timestep_100ns The time elapsed since last call to this function, in multiple of 100ns.
        void process(const timediff_t timestep_100ns);

#if SCRUTINY_ENABLE_DATALOGGING
        /// @brief Returns the state of the datalogger. Thread safe
        inline datalogging::DataLogger::State get_datalogger_state(void) const
        {
            return m_datalogging.threadsafe_data.datalogger_state;
        }

        /// @brief  Returns true if the datalogger has data available. Thread safe
        inline bool datalogging_data_available(void) const
        {
            return m_datalogging.threadsafe_data.datalogger_state == datalogging::DataLogger::State::ACQUISITION_COMPLETED; // Thread safe.
        }

        /// @brief Returns true if the datalogger is in an error state. Thread safe
        inline bool datalogging_error(void) const
        {
            return (m_datalogging.threadsafe_data.datalogger_state == datalogging::DataLogger::State::ERROR) || m_datalogging.error != DataloggingError::NoError;
        }

        /// @brief Reads a section of memory like a memcpy does, but enforce the respect of forbidden regions
        /// @param dst Destination buffer
        /// @param src  Source buffer
        /// @param size Size of data to transfer
        /// @return true on success, false on failure
        bool read_memory(void *dst, const void *src, const uint32_t size) const;

        /// @brief Reads a variable from a memory location. Ensure the respect of forbidden regions and will not make unaligned memory access
        /// @param addr Address at which the variable is stored
        /// @param variable_type Type of variable to read
        /// @param val The output value
        /// @return true on success, false on failure
        bool fetch_variable(const void *addr, const VariableType variable_type, AnyType *val) const;

        /// @brief Reads a bitfield variable from a memory location. Ensure the respect of forbidden regions and will not make unaligned memory access
        /// @param addr Address at which the variable is stored
        /// @param var_tt Type type of the variable to read (uint, int, float. See scrutiny::VariableTypeType)
        /// @param bitoffset Offset from the address to read from. This value should normally comes from the debug symbols since it is compiler dependent
        /// @param bitsize Size in bits of the variable to read. This value should normally comes from the debug symbols since it is compiler dependent
        /// @param val The output value
        /// @param output_type The output variable type deduced from the VariableTypeType and the size
        /// @return true on success, false on failure
        bool fetch_variable_bitfield(
            const void *addr,
            const VariableTypeType var_tt,
            const uint_fast8_t bitoffset,
            const uint_fast8_t bitsize,
            AnyType *val,
            VariableType *output_type) const;

        /// @brief Returns a pointer to the datalogger object
        inline datalogging::DataLogger *datalogger(void) { return &m_datalogging.datalogger; }
#endif
        /// @brief Return the Runtime Published Value (RPV) read callback
        inline RpvReadCallback get_rpv_read_callback(void) const
        {
            return m_config.get_rpv_read_callback();
        }

        /// @brief Returns a pointer to the communication handler
        inline protocol::CommHandler *comm(void) { return &m_comm_handler; }

        /// @brief Returns a pointer the the given configuration
        inline Config *get_config(void) { return &m_config; }

    private:
        void process_loops(void);
        void process_request(const protocol::Request *const request, protocol::Response *const response);
        protocol::ResponseCode process_get_info(const protocol::Request *const request, protocol::Response *const response);
        protocol::ResponseCode process_comm_control(const protocol::Request *const request, protocol::Response *const response);
        protocol::ResponseCode process_memory_control(const protocol::Request *const request, protocol::Response *const response);
        protocol::ResponseCode process_user_command(const protocol::Request *const request, protocol::Response *const response);

#if SCRUTINY_ENABLE_DATALOGGING
        protocol::ResponseCode process_datalog_control(const protocol::Request *const request, protocol::Response *const response);
        void process_datalogging_loop_msg(LoopHandler *sender, LoopHandler::Loop2MainMessage *msg);
        void process_datalogging_logic(void);
#endif
        bool touches_forbidden_region(const MemoryBlock *block) const;
        bool touches_forbidden_region(const void *addr_start, const size_t length) const;
        bool touches_readonly_region(const MemoryBlock *block) const;
        bool touches_readonly_region(const void *addr_start, const size_t length) const;
        void check_config(void);

        Timebase m_timebase;                   // Timebase to keep track of time
        protocol::CommHandler m_comm_handler;  // The communication handler that parses the request and manages the buffers
        bool m_processing_request;             // True when a request is being processed
        bool m_disconnect_pending;             // INdicates that a disconnect request has been received and must be processed right away
        Config m_config;                       // The configuration
        bool m_enabled;                        // Indicates that scrutiny is enabled. Will be disabled if the configuration is wrong.
        bool m_process_again_timestamp_taken;  // Indicates that a timestamp has been taken on ProcessAgain response code, meaning that the timestamp should not be updated on subsequent ProcessAgain code
        timestamp_t m_process_again_timestamp; // Timestamp at which the first ProcessAgain code has been returned to ensure timeout
#if SCRUTINY_ACTUAL_PROTOCOL_VERSION == SCRUTINY_PROTOCOL_VERSION(1, 0)
        protocol::CodecV1_0 m_codec; // Communication protocol Codec
#else
#error Unsupported codec
#endif

#if SCRUTINY_ENABLE_DATALOGGING
        enum class DataloggingError
        {
            NoError,
            UnexpectedRelease,
            UnexpectedClaim
        };

        struct ThreadSafeData
        {
            datalogging::DataLogger::State datalogger_state;
            uint32_t bytes_to_acquire_from_trigger_to_completion;
            uint32_t write_counter_since_trigger;
        };

        struct
        {

            datalogging::DataLogger datalogger; // The Datalogger object
            ThreadSafeData threadsafe_data;     // Data that got read from the datalogger through IPC

            LoopHandler *owner;                       // LoopHandler that presently own the Datalogger
            LoopHandler *new_owner;                   // LoopHandler that is requested to take ownership of the  Datalogger
            DataloggingError error;                   // Error related to datalogging mechanism
            bool request_arm_trigger;                 // Flag indicating that a request has been made to arm the trigger
            bool request_ownership_release;           // Flag indicating that a request has been made to release ownership of the datalogger
            bool request_disarm_trigger;              // Flag indicating that a request has been made to darm the trigger
            bool pending_ownership_release;           // Flag indicating that a request for ownership release is presently being processed
            bool reading_in_progress;                 // Flag indicating that the datalogging data is presently being read by the user.
            uint8_t read_acquisition_rolling_counter; // Counter to validate the order of the data packet being read
            uint32_t read_acquisition_crc;            // CRC of the datalogging buffer content
        } m_datalogging;                              // All data related to the datalogging feature

#endif
    };
}

#endif