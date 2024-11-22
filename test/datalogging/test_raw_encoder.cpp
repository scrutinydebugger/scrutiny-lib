//    test_raw_encoder.cpp
//        Test suite for the RawFormat encoder.
//
//   - License : MIT - See LICENSE file.
//   - Project : Scrutiny Debugger (github.com/scrutinydebugger/scrutiny-embedded)
//
//   Copyright (c) 2021 Scrutiny Debugger

#include <gtest/gtest.h>

#include "scrutiny_test.hpp"
#include "scrutiny.hpp"

#if SCRUTINY_DATALOGGING_ENCODING == SCRUTINY_DATALOGGING_ENCODING_RAW

using namespace scrutiny;

class TestRawEncoder : public ScrutinyTest
{
protected:
    void check_canaries();

    MainHandler scrutiny_handler;
    Config config;
    datalogging::Configuration dlconfig;
    datalogging::RawFormatEncoder encoder;

    uint8_t _rx_buffer[128];
    uint8_t _tx_buffer[128];

    uint8_t buffer_canary_1[512];
    uint8_t dlbuffer[128];
    uint8_t buffer_canary_2[512];

    TestRawEncoder() : ScrutinyTest(),
                       scrutiny_handler{},
                       config{},
                       dlconfig{},
                       encoder{},
                       _rx_buffer{},
                       _tx_buffer{},
                       buffer_canary_1{},
                       dlbuffer{},
                       buffer_canary_2{}
    {
    }

    virtual void SetUp()
    {
        config.set_buffers(_rx_buffer, sizeof(_rx_buffer), _tx_buffer, sizeof(_tx_buffer));
        scrutiny_handler.init(&config);

        memset(buffer_canary_1, 0xAA, sizeof(buffer_canary_1));
        memset(buffer_canary_2, 0x55, sizeof(buffer_canary_2));
    }
};

void TestRawEncoder::check_canaries()
{
    for (size_t i = 0; i < sizeof(buffer_canary_1); i++)
    {
        ASSERT_EQ(buffer_canary_1[i], 0xAA) << "Overflow before buffer at i=" << i;
    }

    for (size_t i = 0; i < sizeof(buffer_canary_2); i++)
    {
        ASSERT_EQ(buffer_canary_2[i], 0x55) << "Overflow after buffer at i=" << i;
    }
}

TEST_F(TestRawEncoder, BasicEncoding)
{
    Timebase timebase;
    uint8_t dst_buffer[10];
    uint8_t compare_buf[128];
    float var1;
    uint32_t var2;

    dlconfig.items_count = 3;

    dlconfig.items_to_log[0].type = datalogging::LoggableType::MEMORY;
    dlconfig.items_to_log[0].data.memory.size = sizeof(var1);
    dlconfig.items_to_log[0].data.memory.address = &var1;

    dlconfig.items_to_log[1].type = datalogging::LoggableType::MEMORY;
    dlconfig.items_to_log[1].data.memory.size = sizeof(var2);
    dlconfig.items_to_log[1].data.memory.address = &var2;

    dlconfig.items_to_log[2].type = datalogging::LoggableType::TIME;

    encoder.init(&scrutiny_handler, &dlconfig, dlbuffer, sizeof(dlbuffer));
    encoder.set_timebase(&timebase);
    timebase.reset();

    var1 = 1.0f;
    var2 = 0x1111u;
    encoder.encode_next_entry();
    timebase.step(100);
    var1 = 2.0f;
    var2 = 0x2222u;
    encoder.encode_next_entry();
    timebase.step(100);
    var1 = 3.0f;
    var2 = 0x3333u;
    encoder.encode_next_entry();

    datalogging::RawFormatReader *reader = encoder.get_reader();
    reader->reset();

    EXPECT_EQ(reader->get_entry_count(), 3u);
    EXPECT_EQ(reader->get_total_size(), (sizeof(var1) + sizeof(var2) + 4u) * 3u); // entry_size*entry_count

    uint32_t expected_time;

    // Chunk 1
    var1 = 1.0f;
    var2 = 0x1111;
    expected_time = 0;
    memcpy(&compare_buf[0], &var1, 4);
    memcpy(&compare_buf[4], &var2, 4);
    scrutiny::codecs::encode_32_bits_big_endian(expected_time, &compare_buf[8]);

    var1 = 2.0f;
    var2 = 0x2222;
    expected_time = 100;
    memcpy(&compare_buf[12], &var1, 4);
    memcpy(&compare_buf[16], &var2, 4);
    scrutiny::codecs::encode_32_bits_big_endian(expected_time, &compare_buf[20]);

    var1 = 3.0f;
    var2 = 0x3333;
    expected_time = 200u;
    memcpy(&compare_buf[24], &var1, 4);
    memcpy(&compare_buf[28], &var2, 4);
    scrutiny::codecs::encode_32_bits_big_endian(expected_time, &compare_buf[32]);

    uint8_t const chunk_size = sizeof(dst_buffer);
    uint8_t const nbchunk = static_cast<uint8_t>(static_cast<float>(reader->get_total_size()) / chunk_size + 0.5f);
    uint32_t total_read = 0;
    for (uint8_t i = 0; i < nbchunk; i++)
    {
        uint32_t nread = reader->read(dst_buffer, chunk_size);
        total_read += nread;
        if (reader->finished())
        {
            EXPECT_BUF_EQ(dst_buffer, &compare_buf[i * chunk_size], nread);
        }
        else
        {
            EXPECT_EQ(nread, chunk_size);
            EXPECT_BUF_EQ(dst_buffer, &compare_buf[i * chunk_size], chunk_size);
        }
    }

    EXPECT_EQ(total_read, reader->get_total_size());
}

#endif