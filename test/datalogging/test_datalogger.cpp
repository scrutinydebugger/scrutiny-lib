#include <gtest/gtest.h>

#include "scrutiny_test.hpp"
#include "scrutiny.hpp"

using namespace scrutiny;

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
    else
    {
        return false;
    }

    return true;
}

class TestDatalogger : public ScrutinyTest
{
public:
    TestDatalogger() : ScrutinyTest(),
                       datalogger(dlbuffer, sizeof(dlbuffer))
    {
    }

protected:
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

    RuntimePublishedValue rpvs[2] = {
        {0x1234, VariableType::uint32},
        {0x5678, VariableType::float32}};

    uint8_t dlbuffer[128];

    virtual void SetUp()
    {
        config.set_buffers(_rx_buffer, sizeof(_rx_buffer), _tx_buffer, sizeof(_tx_buffer));
        config.set_readonly_address_range(readonly_ranges, sizeof(readonly_ranges) / sizeof(readonly_ranges[0]));
        config.set_forbidden_address_range(forbidden_ranges, sizeof(forbidden_ranges) / sizeof(forbidden_ranges[0]));
        config.set_published_values(rpvs, sizeof(rpvs) / sizeof(rpvs[0]), rpv_read_callback);

        scrutiny_handler.init(&config);
        datalogger.init(&scrutiny_handler, &tb);
    }
};

TEST_F(TestDatalogger, TriggerBasics)
{
    float my_var = 0.0;
    float logged_var = 0.0;

    datalogging::Configuration dlconfig;
    dlconfig.block_count = 1;
    dlconfig.blocksizes[0] = sizeof(logged_var);
    dlconfig.memblocks[0] = &logged_var;
    dlconfig.decimation = 1;
    dlconfig.trigger.hold_time_us = 0;
    dlconfig.trigger.operand_count = 2;
    dlconfig.trigger.condition = datalogging::SupportedTriggerConditions::Equal;

    dlconfig.trigger.operands[0].type = datalogging::OperandType::VAR;
    dlconfig.trigger.operands[0].data.var.addr = &my_var;
    dlconfig.trigger.operands[0].data.var.datatype = scrutiny::VariableType::float32;

    dlconfig.trigger.operands[1].type = datalogging::OperandType::LITERAL;
    dlconfig.trigger.operands[1].data.literal.val = 3.1415926f;

    datalogger.configure(&dlconfig);

    EXPECT_FALSE(datalogger.check_trigger());
    my_var = 3.1415926f;
    EXPECT_FALSE(datalogger.check_trigger());

    datalogger.arm_trigger();
    my_var = 0;
    EXPECT_FALSE(datalogger.check_trigger());
    my_var = 3.1415926f;
    EXPECT_TRUE(datalogger.check_trigger());
}

TEST_F(TestDatalogger, TriggerHoldTime)
{
    float my_var = 0.0;
    float logged_var = 0.0;

    datalogging::Configuration dlconfig;
    dlconfig.block_count = 1;
    dlconfig.blocksizes[0] = sizeof(logged_var);
    dlconfig.memblocks[0] = &logged_var;

    dlconfig.decimation = 1;
    dlconfig.trigger.hold_time_us = 100;
    dlconfig.trigger.operand_count = 2;
    dlconfig.trigger.condition = datalogging::SupportedTriggerConditions::Equal;

    dlconfig.trigger.operands[0].type = datalogging::OperandType::VAR;
    dlconfig.trigger.operands[0].data.var.addr = &my_var;
    dlconfig.trigger.operands[0].data.var.datatype = scrutiny::VariableType::float32;

    dlconfig.trigger.operands[1].type = datalogging::OperandType::LITERAL;
    dlconfig.trigger.operands[1].data.literal.val = 3.1415926f;

    datalogger.configure(&dlconfig);
    datalogger.arm_trigger();

    EXPECT_FALSE(datalogger.check_trigger());
    my_var = 3.1415926f;
    EXPECT_FALSE(datalogger.check_trigger());
    tb.step(99);
    EXPECT_FALSE(datalogger.check_trigger());
    tb.step(1);
    EXPECT_TRUE(datalogger.check_trigger());
}

TEST_F(TestDatalogger, BasicAcquisition)
{
    float my_var = 0.0;

    datalogging::Configuration dlconfig;
    dlconfig.block_count = 1;
    dlconfig.memblocks[0] = &my_var;
    dlconfig.blocksizes[0] = sizeof(float);
    dlconfig.decimation = 1;
    dlconfig.trigger.hold_time_us = 100;
    dlconfig.trigger.operand_count = 2;
    dlconfig.trigger.condition = datalogging::SupportedTriggerConditions::GreaterThan;

    dlconfig.trigger.operands[0].type = datalogging::OperandType::VAR;
    dlconfig.trigger.operands[0].data.var.addr = &my_var;
    dlconfig.trigger.operands[0].data.var.datatype = scrutiny::VariableType::float32;

    dlconfig.trigger.operands[1].type = datalogging::OperandType::LITERAL;
    dlconfig.trigger.operands[1].data.literal.val = 100;

    datalogger.configure(&dlconfig);

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

    datalogger.arm_trigger();

    for (unsigned int i = 0; i < 100; i++)
    {
        datalogger.process();
        tb.step(100);
        my_var += 1.0;
    }
    EXPECT_TRUE(datalogger.data_acquired());
}