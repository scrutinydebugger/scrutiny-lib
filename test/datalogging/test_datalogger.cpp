//    test_datalogger.cpp
//        Test suite for the datalogger object. Test its capacity to log, trigger access bitfields
//        and report error on bad config.
//
//   - License : MIT - See LICENSE file.
//   - Project : Scrutiny Debugger (github.com/scrutinydebugger/scrutiny-embedded)
//
//   Copyright (c) 2021 Scrutiny Debugger

#include <gtest/gtest.h>
#include <vector>

#include "scrutiny_test.hpp"
#include "scrutiny.hpp"
#include "raw_format_parser.hpp"
#include <algorithm>
#include <cmath>
#include <string>

using namespace scrutiny;
using namespace std;

static uint32_t g_u32_rpv1000 = 0;
static uint32_t g_trigger_callback_count = 0;

static bool rpv_read_callback(RuntimePublishedValue rpv, AnyType *outval)
{
    if (rpv.id == 0x1234 && rpv.type == VariableType::uint32)
    {
        outval->uint32 = 0xaabbccdd;
    }
    else if (rpv.id == 0x5678 && rpv.type == VariableType::float32)
    {
        outval->float32 = 3.1415926f;
    }
    else if (rpv.id == 0x1000 && rpv.type == VariableType::uint32)
    {
        outval->uint32 = g_u32_rpv1000;
    }
    else
    {
        return false;
    }

    return true;
}

static void trigger_callback(void)
{
    g_trigger_callback_count++;
}

class TestDatalogger : public ScrutinyTest
{
protected:
    void check_canaries();
    Timebase tb;
    MainHandler scrutiny_handler;
    Config config;
    datalogging::DataLogger datalogger;

    uint8_t _rx_buffer[128];
    uint8_t _tx_buffer[128];

    uint8_t forbidden_buffer[128];
    uint8_t forbidden_buffer2[128];
    uint8_t readonly_buffer[128];
    uint8_t readonly_buffer2[128];

    AddressRange readonly_ranges[2] = {
        tools::make_address_range(readonly_buffer, sizeof(readonly_buffer)),
        tools::make_address_range(readonly_buffer2, sizeof(readonly_buffer2))};

    AddressRange forbidden_ranges[2] = {
        tools::make_address_range(forbidden_buffer, sizeof(forbidden_buffer)),
        tools::make_address_range(forbidden_buffer2, sizeof(forbidden_buffer2))};

    RuntimePublishedValue rpvs[3] = {
        {0x1234, VariableType::uint32},
        {0x5678, VariableType::float32},
        {0x1000, VariableType::uint32}};

    uint8_t buffer_canary_1[512];
    uint8_t dlbuffer[128];
    uint8_t buffer_canary_2[512];

    TestDatalogger() : ScrutinyTest(),
                       tb{},
                       scrutiny_handler{},
                       config{},
                       datalogger{},
                       _rx_buffer{},
                       _tx_buffer{},
                       forbidden_buffer{},
                       forbidden_buffer2{},
                       readonly_buffer{},
                       readonly_buffer2{},
                       buffer_canary_1{},
                       dlbuffer{},
                       buffer_canary_2{}
    {
    }

    virtual void SetUp()
    {
        config.set_buffers(_rx_buffer, sizeof(_rx_buffer), _tx_buffer, sizeof(_tx_buffer));
        config.set_readonly_address_range(readonly_ranges, sizeof(readonly_ranges) / sizeof(readonly_ranges[0]));
        config.set_forbidden_address_range(forbidden_ranges, sizeof(forbidden_ranges) / sizeof(forbidden_ranges[0]));
        config.set_published_values(rpvs, sizeof(rpvs) / sizeof(rpvs[0]), rpv_read_callback);

        scrutiny_handler.init(&config);
        datalogger.init(&scrutiny_handler, dlbuffer, sizeof(dlbuffer), trigger_callback);

        memset(buffer_canary_1, 0xAA, sizeof(buffer_canary_1));
        memset(buffer_canary_2, 0x55, sizeof(buffer_canary_2));

        g_trigger_callback_count = 0;
    }
};

void TestDatalogger::check_canaries()
{
    ASSERT_BUF_SET(buffer_canary_1, 0xAA, sizeof(buffer_canary_1));
    ASSERT_BUF_SET(buffer_canary_2, 0x55, sizeof(buffer_canary_2));
}

TEST_F(TestDatalogger, TriggerBasics)
{
    float my_var = 0.0;
    float logged_var = 0.0;

    datalogging::Configuration dlconfig;
    dlconfig.items_count = 1;
    dlconfig.items_to_log[0].type = datalogging::LoggableType::MEMORY;
    dlconfig.items_to_log[0].data.memory.size = sizeof(logged_var);
    dlconfig.items_to_log[0].data.memory.address = &logged_var;
    dlconfig.decimation = 1;
    dlconfig.timeout_100ns = 0;
    dlconfig.probe_location = 128;
    dlconfig.trigger.hold_time_100ns = 0;
    dlconfig.trigger.operand_count = 2;
    dlconfig.trigger.condition = datalogging::SupportedTriggerConditions::Equal;

    dlconfig.trigger.operands[0].type = datalogging::OperandType::VAR;
    dlconfig.trigger.operands[0].data.var.addr = &my_var;
    dlconfig.trigger.operands[0].data.var.datatype = scrutiny::VariableType::float32;

    dlconfig.trigger.operands[1].type = datalogging::OperandType::LITERAL;
    dlconfig.trigger.operands[1].data.literal.val = 3.1415926f;

    datalogger.config()->copy_from(&dlconfig);
    datalogger.configure(&tb);

    EXPECT_FALSE(datalogger.check_trigger());
    my_var = 3.1415926f;
    EXPECT_FALSE(datalogger.check_trigger());

    datalogger.arm_trigger();
    my_var = 0;
    EXPECT_FALSE(datalogger.check_trigger());
    my_var = 3.1415926f;
    EXPECT_TRUE(datalogger.check_trigger());

    check_canaries();
}

TEST_F(TestDatalogger, TriggerHoldTime)
{
    float my_var = 0.0;
    float logged_var = 0.0;

    datalogging::Configuration dlconfig;
    dlconfig.items_count = 1;
    dlconfig.items_to_log[0].type = datalogging::LoggableType::MEMORY;
    dlconfig.items_to_log[0].data.memory.size = sizeof(logged_var);
    dlconfig.items_to_log[0].data.memory.address = &logged_var;

    dlconfig.decimation = 1;
    dlconfig.timeout_100ns = 0;
    dlconfig.probe_location = 128;
    dlconfig.trigger.hold_time_100ns = 100;
    dlconfig.trigger.operand_count = 2;
    dlconfig.trigger.condition = datalogging::SupportedTriggerConditions::Equal;

    dlconfig.trigger.operands[0].type = datalogging::OperandType::VAR;
    dlconfig.trigger.operands[0].data.var.addr = &my_var;
    dlconfig.trigger.operands[0].data.var.datatype = scrutiny::VariableType::float32;

    dlconfig.trigger.operands[1].type = datalogging::OperandType::LITERAL;
    dlconfig.trigger.operands[1].data.literal.val = 3.1415926f;

    datalogger.config()->copy_from(&dlconfig);
    datalogger.configure(&tb);
    datalogger.arm_trigger();

    EXPECT_FALSE(datalogger.check_trigger());
    my_var = 3.1415926f;
    EXPECT_FALSE(datalogger.check_trigger());
    tb.step(99);
    EXPECT_FALSE(datalogger.check_trigger());
    tb.step(1);
    EXPECT_TRUE(datalogger.check_trigger());

    check_canaries();
}

TEST_F(TestDatalogger, BasicAcquisition)
{
    float my_var = 0.0;

    datalogging::Configuration dlconfig;
    dlconfig.items_count = 1;
    dlconfig.items_to_log[0].type = datalogging::LoggableType::MEMORY;
    dlconfig.items_to_log[0].data.memory.size = sizeof(my_var);
    dlconfig.items_to_log[0].data.memory.address = &my_var;
    dlconfig.decimation = 2;
    dlconfig.timeout_100ns = 0;
    dlconfig.probe_location = 128;
    dlconfig.trigger.hold_time_100ns = 100;
    dlconfig.trigger.operand_count = 2;
    dlconfig.trigger.condition = datalogging::SupportedTriggerConditions::GreaterThan;

    dlconfig.trigger.operands[0].type = datalogging::OperandType::VAR;
    dlconfig.trigger.operands[0].data.var.addr = &my_var;
    dlconfig.trigger.operands[0].data.var.datatype = scrutiny::VariableType::float32;

    dlconfig.trigger.operands[1].type = datalogging::OperandType::LITERAL;
    dlconfig.trigger.operands[1].data.literal.val = 100;

    datalogger.config()->copy_from(&dlconfig);
    datalogger.configure(&tb);

    datalogger.process();
    tb.step(100);
    datalogger.process();
    tb.step(100);
    EXPECT_FALSE(datalogger.data_acquired());

    my_var = 200.0f;

    for (unsigned int i = 0; i < 100; i++)
    {
        datalogger.process();
        tb.step(100);
        my_var += 1.0;
    }
    EXPECT_FALSE(datalogger.data_acquired());
    EXPECT_EQ(g_trigger_callback_count, 0u);
    datalogger.arm_trigger();

    for (unsigned int i = 0; i < 100; i++)
    {
        datalogger.process();
        tb.step(100);
        my_var += 1.0;
    }
    EXPECT_TRUE(datalogger.data_acquired());
    EXPECT_EQ(g_trigger_callback_count, 1u);

    check_canaries();
}

TEST_F(TestDatalogger, ComplexAcquisition)
{
    float var1 = 0.0;
    int32_t var2 = 0;
    float trigger_val = 0.0f;

    datalogging::Configuration dlconfig{};
    dlconfig.items_count = 4;
    dlconfig.items_to_log[0].type = datalogging::LoggableType::MEMORY;
    dlconfig.items_to_log[0].data.memory.size = sizeof(var1);
    dlconfig.items_to_log[0].data.memory.address = &var1;

    dlconfig.items_to_log[1].type = datalogging::LoggableType::MEMORY;
    dlconfig.items_to_log[1].data.memory.size = sizeof(var2);
    dlconfig.items_to_log[1].data.memory.address = &var2;

    dlconfig.items_to_log[2].type = datalogging::LoggableType::RPV;
    dlconfig.items_to_log[2].data.rpv.id = 0x1000;

    dlconfig.items_to_log[3].type = datalogging::LoggableType::TIME;

    dlconfig.decimation = 2;

    dlconfig.trigger.hold_time_100ns = 50;
    dlconfig.trigger.operand_count = 2;
    dlconfig.trigger.condition = datalogging::SupportedTriggerConditions::Equal;

    dlconfig.trigger.operands[0].type = datalogging::OperandType::VAR;
    dlconfig.trigger.operands[0].data.var.addr = &trigger_val;
    dlconfig.trigger.operands[0].data.var.datatype = scrutiny::VariableType::float32;

    dlconfig.trigger.operands[1].type = datalogging::OperandType::LITERAL;
    dlconfig.trigger.operands[1].data.literal.val = 100.0f;

    uint8_t probe_location = 0;
    for (uint16_t probe_loop = 0; probe_loop <= 255; probe_loop++)
    {
        var1 = 0.0;
        var2 = 0;
        trigger_val = 0.0f;
        probe_location = static_cast<uint8_t>(probe_loop);
        dlconfig.probe_location = probe_location;
        std::string error_msg = "probe_location="s + std::to_string(probe_location);
        datalogger.config()->copy_from(&dlconfig);
        datalogger.configure(&tb);

        datalogger.process();
        tb.step(10);
        datalogger.process();
        tb.step(10);
        EXPECT_FALSE(datalogger.data_acquired()) << error_msg;

        for (unsigned int i = 0; i < 100; i++)
        {
            datalogger.process();
            tb.step(10);
        }
        EXPECT_FALSE(datalogger.data_acquired()) << error_msg;

        datalogger.arm_trigger();

        float var1_at_trigger = 0;
        int32_t var2_at_trigger = 0;
        uint32_t rpv1000_at_trigger = 0;

        for (unsigned int i = 0; i < 100; i++)
        {
            datalogger.process();
            tb.step(10);
            var1 += 1.0f;
            var2--;
            g_u32_rpv1000 += 1;
        }

        trigger_val = 100.0f;

        for (unsigned int i = 0; i < 500; i++)
        {
            datalogger.process();
            if (i == 6) // 50us delay
            {
                var1_at_trigger = var1;
                var2_at_trigger = var2;
                rpv1000_at_trigger = g_u32_rpv1000;
            }
            tb.step(10);
            var1 += 1.0f;
            var2--;
            g_u32_rpv1000 += 1;
        }

        EXPECT_TRUE(datalogger.data_acquired()) << error_msg;
        ASSERT_FALSE(datalogger.get_encoder()->error()) << error_msg;
        check_canaries();

#if SCRUTINY_DATALOGGING_ENCODING == SCRUTINY_DATALOGGING_ENCODING_RAW
        RawFormatParser parser;
        constexpr size_t output_buffer_required_size = sizeof(dlbuffer) + 4;
#else
#error "Unsupported parser"
#endif

        uint8_t output_buffer_canary1[512];
        uint8_t output_buffer[output_buffer_required_size];
        uint8_t output_buffer_canary2[512];

        memset(output_buffer_canary1, 0xAA, sizeof(output_buffer_canary1));
        memset(output_buffer_canary2, 0x55, sizeof(output_buffer_canary2));

        datalogging::DataReader *reader = datalogger.get_reader();
        reader->reset();

        uint32_t copied_count = 0;
        while (!reader->finished())
        {
            ASSERT_FALSE(reader->error()) << error_msg;
            copied_count += reader->read(&output_buffer[copied_count], 10);
            ASSERT_LE(copied_count, sizeof(output_buffer)) << error_msg;
        }

        ASSERT_BUF_SET(output_buffer_canary1, 0xAA, sizeof(output_buffer_canary1)) << error_msg;
        ASSERT_BUF_SET(output_buffer_canary2, 0x55, sizeof(output_buffer_canary2)) << error_msg;

        EXPECT_GE(copied_count, 9 * sizeof(dlbuffer) / 10) << error_msg; // 90% usage at least

        parser.init(&scrutiny_handler, &dlconfig, output_buffer, sizeof(output_buffer));
        parser.parse(reader->get_entry_count());

        ASSERT_FALSE(datalogger.in_error()) << error_msg;
        ASSERT_FALSE(parser.error()) << error_msg;

        // Validate data now;

        vector<vector<vector<uint8_t>>> data = parser.get();

        for (size_t i = 0; i < data.size(); i++)
        {
            vector<vector<uint8_t>> entry = data[i];
            ASSERT_EQ(entry.size(), 4); // 4 signals logged
            if (i > 0)
            {
                vector<vector<uint8_t>> last_entry = data[i - 1];
                float check_var1 = *reinterpret_cast<float *>(entry[0].data());
                float check_last_var1 = *reinterpret_cast<float *>(last_entry[0].data());

                int32_t check_var2 = *reinterpret_cast<int32_t *>(entry[1].data());
                int32_t check_last_var2 = *reinterpret_cast<int32_t *>(last_entry[1].data());

                uint32_t check_rpv1000 = codecs::decode_32_bits_big_endian(entry[2].data());
                uint32_t check_last_rpv1000 = codecs::decode_32_bits_big_endian(last_entry[2].data());

                uint32_t check_time = codecs::decode_32_bits_big_endian(entry[3].data());
                uint32_t check_last_time = codecs::decode_32_bits_big_endian(last_entry[3].data());

                EXPECT_EQ(check_var1 - check_last_var1, 2.0f) << "i=" << i << "," << error_msg;
                EXPECT_EQ(check_var2 - check_last_var2, -2) << "i=" << i << "," << error_msg;
                EXPECT_EQ(check_rpv1000 - check_last_rpv1000, 2u) << "i=" << i << "," << error_msg;
                EXPECT_EQ(check_time - check_last_time, 20u) << "i=" << i << "," << error_msg; // 2*10us
            }
        }

        uint32_t trigger_location = static_cast<uint32_t>(std::round(static_cast<float>(probe_location) / 255 * (reader->get_entry_count() - 1)));
        ASSERT_GT(data.size(), trigger_location);
        ASSERT_GT(data.size(), 0);
        ASSERT_GE(data.size(), datalogger.log_points_after_trigger());

        EXPECT_NEAR(trigger_location, static_cast<uint32_t>(data.size() - datalogger.log_points_after_trigger()), 1u);

        float mid_var1 = *reinterpret_cast<float *>(data[trigger_location][0].data());
        int32_t mid_var2 = *reinterpret_cast<int32_t *>(data[trigger_location][1].data());
        uint32_t mid_rpv1000 = codecs::decode_32_bits_big_endian(data[trigger_location][2].data());

        // Validate position of trigger and end of graph. Allow a margin of 3 (1.5 entry because decimation=2)
        EXPECT_LE(std::abs(var1_at_trigger - mid_var1), 3.0f) << error_msg;
        EXPECT_LE(std::abs(var2_at_trigger - mid_var2), 3) << error_msg;
        EXPECT_LE(std::abs(static_cast<int32_t>(rpv1000_at_trigger - mid_rpv1000)), 3) << error_msg;
    }
}

TEST_F(TestDatalogger, TestAlwaysUseFullBuffer)
{
    float var1 = 0.0;
    int32_t var2 = 0;

    datalogging::Configuration dlconfig;
    dlconfig.items_count = 4;
    dlconfig.items_to_log[0].type = datalogging::LoggableType::MEMORY;
    dlconfig.items_to_log[0].data.memory.size = sizeof(var1);
    dlconfig.items_to_log[0].data.memory.address = &var1;
    dlconfig.items_to_log[1].type = datalogging::LoggableType::MEMORY;
    dlconfig.items_to_log[1].data.memory.size = sizeof(var2);
    dlconfig.items_to_log[1].data.memory.address = &var2;
    dlconfig.items_to_log[2].type = datalogging::LoggableType::RPV;
    dlconfig.items_to_log[2].data.rpv.id = 0x1000;
    dlconfig.items_to_log[3].type = datalogging::LoggableType::TIME;

    dlconfig.decimation = 1;
    dlconfig.trigger.hold_time_100ns = 0;
    dlconfig.timeout_100ns = 0;
    dlconfig.trigger.operand_count = 0;
    dlconfig.trigger.condition = datalogging::SupportedTriggerConditions::AlwaysTrue;

    uint8_t probe_location = 0;
    for (uint16_t probe_loop = 0; probe_loop <= 255; probe_loop++)
    {
        var1 = 0.0;
        var2 = 0;
        probe_location = static_cast<uint8_t>(probe_loop);
        dlconfig.probe_location = probe_location;
        std::string error_msg = "probe_location="s + std::to_string(probe_location);
        datalogger.reset();
        datalogger.config()->copy_from(&dlconfig);
        datalogger.configure(&tb);
        datalogger.arm_trigger();

        for (unsigned int i = 0; i < sizeof(dlbuffer) / 4; i++)
        {
            datalogger.process();
            tb.step(10);
            if (datalogger.data_acquired())
            {
                break;
            }
        }
        ASSERT_TRUE(datalogger.data_acquired()) << error_msg;
        ASSERT_FALSE(datalogger.get_encoder()->error()) << error_msg;
        check_canaries();

        datalogging::DataReader *reader = datalogger.get_reader();
        reader->reset();

        EXPECT_GE(reader->get_total_size(), 9 * sizeof(dlbuffer) / 10) << error_msg; // 90% usage at least
    }
}

TEST_F(TestDatalogger, TestAquireTimeCorrectly)
{
    datalogging::Configuration dlconfig;
    dlconfig.items_count = 1;
    dlconfig.items_to_log[0].type = datalogging::LoggableType::TIME;

    dlconfig.decimation = 1;
    dlconfig.timeout_100ns = 0;
    dlconfig.probe_location = 128;
    dlconfig.trigger.hold_time_100ns = 0;
    dlconfig.trigger.operand_count = 0;
    dlconfig.trigger.condition = datalogging::SupportedTriggerConditions::AlwaysTrue;

    datalogger.config()->copy_from(&dlconfig);
    datalogger.configure(&tb);
    uint32_t max_loop = sizeof(dlbuffer) * 8; // don't think we can beat 1 bit per sample
    uint32_t i = 0;
    while (!datalogger.get_encoder()->buffer_full() && i < max_loop)
    {
        datalogger.process();
        tb.step(5);
        i++;
    }
    datalogger.arm_trigger();
    datalogger.force_trigger();
    i = 0;
    while (datalogger.get_state() != scrutiny::datalogging::DataLogger::State::ACQUISITION_COMPLETED && i < max_loop)
    {
        datalogger.process();
        tb.step(5);
        i++;
    }
    datalogger.process();
    ASSERT_EQ(datalogger.get_state(), scrutiny::datalogging::DataLogger::State::ACQUISITION_COMPLETED);

#if SCRUTINY_DATALOGGING_ENCODING == SCRUTINY_DATALOGGING_ENCODING_RAW
    RawFormatParser parser;
    constexpr size_t output_buffer_required_size = sizeof(dlbuffer) + 4;
#else
#error "Unsupported parser"
#endif
    uint8_t output_buffer[output_buffer_required_size] = {0};
    datalogger.get_reader()->reset();
    datalogger.get_reader()->read(output_buffer, sizeof(output_buffer));
    parser.init(&scrutiny_handler, &dlconfig, output_buffer, sizeof(output_buffer));
    parser.parse(datalogger.get_reader()->get_entry_count());
    ASSERT_FALSE(parser.error());
    vector<vector<vector<uint8_t>>> data = parser.get();
    static_assert(sizeof(timestamp_t) == sizeof(uint32_t), "Expect timestamp to be 32 bits");

    ASSERT_EQ(data.size(), datalogger.get_reader()->get_entry_count());
    for (size_t i = 0; i < data.size(); i++)
    {
        vector<vector<uint8_t>> entry = data[i];
        ASSERT_EQ(entry.size(), 1); // 1 signal = time

        scrutiny::timestamp_t timestamp = codecs::decode_32_bits_big_endian(entry[0].data());
        if (i > 0)
        {
            vector<vector<uint8_t>> last_entry = data[i - 1];
            scrutiny::timestamp_t last_timestamp = codecs::decode_32_bits_big_endian(last_entry[0].data());
            ASSERT_EQ(timestamp - last_timestamp, 5) << "Entry #" << i;
        }
    }

    check_canaries();
}