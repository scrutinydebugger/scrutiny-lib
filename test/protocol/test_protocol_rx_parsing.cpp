//    test_protocol_rx_parsing.cpp
//        Make sure reception of request bytes are correctly decoded
//
//   - License : MIT - See LICENSE file.
//   - Project : Scrutiny Debugger (github.com/scrutinydebugger/scrutiny-embedded)
//
//   Copyright (c) 2021-2022 Scrutiny Debugger

#include <gtest/gtest.h>

#include "scrutiny.hpp"
#include "scrutiny_test.hpp"

class TestRxParsing : public ScrutinyTest
{
protected:
    scrutiny::Timebase tb;
    scrutiny::protocol::CommHandler comm;

    uint8_t _rx_buffer[128];
    uint8_t _tx_buffer[128];

    virtual void SetUp()
    {
        comm.init(_rx_buffer, sizeof(_rx_buffer), _tx_buffer, sizeof(_tx_buffer), &tb);
        comm.connect();
    }
};

//=============================================================================
TEST_F(TestRxParsing, TestRx_ZeroLen_AllInOne)
{
    uint8_t data[8] = { 1,2,0,0 };
    add_crc(data, 4);
    comm.receive_data(data, sizeof(data));

    ASSERT_TRUE(comm.request_received());
    scrutiny::protocol::Request* req = comm.get_request();
    EXPECT_EQ(req->command_id, 1);
    EXPECT_EQ(req->subfunction_id, 2);
    EXPECT_EQ(req->data_length, 0);

    EXPECT_EQ(comm.get_rx_error(), scrutiny::protocol::RxError::None);
}

//=============================================================================
TEST_F(TestRxParsing, TestRx_ZeroLen_BytePerByte)
{
    uint8_t data[8] = { 1,2,0,0 };
    add_crc(data, 4);

    for (unsigned int i = 0; i < sizeof(data); i++)
    {
        comm.receive_data(&data[i], 1);
    }

    ASSERT_TRUE(comm.request_received());
    scrutiny::protocol::Request* req = comm.get_request();
    EXPECT_EQ(req->command_id, 1);
    EXPECT_EQ(req->subfunction_id, 2);
    EXPECT_EQ(req->data_length, 0);

    EXPECT_EQ(comm.get_rx_error(), scrutiny::protocol::RxError::None);
}

//=============================================================================
TEST_F(TestRxParsing, TestRx_NonZeroLen_AllInOne)
{
    uint8_t data[11] = { 1,2,0,3, 0x11, 0x22, 0x33 };
    add_crc(data, 7);
    comm.receive_data(data, sizeof(data));

    ASSERT_TRUE(comm.request_received());
    scrutiny::protocol::Request* req = comm.get_request();
    EXPECT_EQ(req->command_id, 1);
    EXPECT_EQ(req->subfunction_id, 2);
    EXPECT_EQ(req->data_length, 3);
    EXPECT_EQ(req->data[0], 0x11);
    EXPECT_EQ(req->data[1], 0x22);
    EXPECT_EQ(req->data[2], 0x33);

    EXPECT_EQ(comm.get_rx_error(), scrutiny::protocol::RxError::None);
}

//=============================================================================
TEST_F(TestRxParsing, TestRx_NonZeroLen_BytePerByte)
{
    uint8_t data[11] = { 1,2,0,3, 0x11, 0x22, 0x33 };
    add_crc(data, 7);

    for (unsigned int i = 0; i < sizeof(data); i++)
    {
        comm.receive_data(&data[i], 1);
    }

    ASSERT_TRUE(comm.request_received());
    scrutiny::protocol::Request* req = comm.get_request();
    EXPECT_EQ(req->command_id, 1);
    EXPECT_EQ(req->subfunction_id, 2);
    EXPECT_EQ(req->data_length, 3);
    EXPECT_EQ(req->data[0], 0x11);
    EXPECT_EQ(req->data[1], 0x22);
    EXPECT_EQ(req->data[2], 0x33);

    EXPECT_EQ(comm.get_rx_error(), scrutiny::protocol::RxError::None);
}

//=============================================================================
TEST_F(TestRxParsing, TestRx_UseAllBuffer)
{
    ASSERT_LT(sizeof(_rx_buffer), scrutiny::protocol::MAXIMUM_RX_BUFFER_SIZE);  // Lengths are 16bits maximum by protocol definition

    uint16_t datalen = sizeof(_rx_buffer);

    uint8_t data[sizeof(_rx_buffer) + 8] = { 1,2, static_cast<uint8_t>((datalen >> 8) & 0xFF) , static_cast<uint8_t>(datalen & 0xFF) };
    add_crc(data, sizeof(_rx_buffer) + 4);

    comm.receive_data(data, sizeof(data));

    ASSERT_TRUE(comm.request_received());
}

//=============================================================================
TEST_F(TestRxParsing, TestRx_Overflow)
{
    ASSERT_LT(sizeof(_rx_buffer), scrutiny::protocol::MAXIMUM_RX_BUFFER_SIZE);  // Lengths are 16bits maximum by protocol definition

    uint16_t datalen = sizeof(_rx_buffer) + 1;

    uint8_t data[sizeof(_rx_buffer) + 8] = { 1,2, static_cast<uint8_t>((datalen >> 8) & 0xFF) , static_cast<uint8_t>(datalen & 0xFF) };
    add_crc(data, sizeof(_rx_buffer) + 4);

    comm.receive_data(data, sizeof(data));

    ASSERT_FALSE(comm.request_received());
    EXPECT_EQ(comm.get_rx_error(), scrutiny::protocol::RxError::Overflow);
}

//=============================================================================
TEST_F(TestRxParsing, TestRx_OverflowRestoreAfterDelay)
{
    ASSERT_LT(sizeof(_rx_buffer), scrutiny::protocol::MAXIMUM_RX_BUFFER_SIZE);  // Lengths are 16bits maximum by protocol definition

    uint16_t datalen = sizeof(_rx_buffer) + 1;

    uint8_t data[sizeof(_rx_buffer) + 8] = { 1,2, static_cast<uint8_t>((datalen >> 8) & 0xFF) , static_cast<uint8_t>(datalen & 0xFF) };
    add_crc(data, sizeof(_rx_buffer) + 4);

    comm.receive_data(data, sizeof(data));

    ASSERT_FALSE(comm.request_received());
    EXPECT_EQ(comm.get_rx_error(), scrutiny::protocol::RxError::Overflow);

    tb.step(SCRUTINY_COMM_RX_TIMEOUT_US);   // Increase the timesbase from enough time for comm to restart

    datalen = sizeof(_rx_buffer);
    uint8_t data2[sizeof(_rx_buffer) + 8] = { 1,2, static_cast<uint8_t>((datalen >> 8) & 0xFF) , static_cast<uint8_t>(datalen & 0xFF) };
    add_crc(data2, sizeof(_rx_buffer) + 4);

    comm.receive_data(data2, sizeof(data2));

    ASSERT_TRUE(comm.request_received());
}

//=============================================================================
TEST_F(TestRxParsing, TestRx_Timeout)
{
    uint8_t data[11] = { 1,2,0,3, 0x11, 0x22, 0x33 };
    add_crc(data, 7);

    for (uint8_t i = 1; i < sizeof(data) - 1; i++)
    {
        comm.receive_data(&data[0], i);
        ASSERT_FALSE(comm.request_received());
        tb.step(SCRUTINY_COMM_RX_TIMEOUT_US);
        comm.receive_data(&data[i], sizeof(data) - 1);
        ASSERT_FALSE(comm.request_received());
        comm.reset();
    }
}

//=============================================================================
TEST_F(TestRxParsing, TestRx_BadCRC)
{
    uint8_t data[11] = { 1,2,0,3, 0x11, 0x22, 0x33 };
    add_crc(data, 7);
    data[10] = ~data[10]; // Force bad CRC
    comm.receive_data(data, sizeof(data));

    ASSERT_FALSE(comm.request_received());
}
