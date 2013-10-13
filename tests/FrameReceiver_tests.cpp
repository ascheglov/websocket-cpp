#include "details/FrameReceiver.hpp"

#include "catch_wrap.hpp"

namespace
{
    struct FrameReceiverFixture
    {
        websocket::FrameReceiver receiver;

        template<std::size_t N>
        std::size_t write(const char(&data)[N])
        {
            const auto dataSize = N - 1;
            memcpy(receiver.getBufferTail(), data, dataSize);
            return dataSize;
        }

        template<std::size_t N>
        std::size_t write_n_check_more(const char(&data)[N])
        {
            const auto dataSize = write(data);
            return receiver.needReceiveMore(dataSize);
        };
    };
}

TEST_CASE_METHOD(FrameReceiverFixture, "is frame complete", "[websocket]")
{
    REQUIRE(write_n_check_more("") > 0);
    REQUIRE(write_n_check_more("\x81") > 0);
    REQUIRE(write_n_check_more("\x81\x81") > 0);
    REQUIRE(write_n_check_more("\x81\x81" "kkk") > 0);
    REQUIRE(write_n_check_more("\x81\x81" "kkkk") > 0);
    REQUIRE(write_n_check_more("\x81\x80" "kkkk") == 0);
    REQUIRE(write_n_check_more("\x81\x81" "kkkk" "d") == 0);
}

TEST_CASE_METHOD(FrameReceiverFixture, "not final fragment", "[websocket]")
{
    REQUIRE(write_n_check_more("\x00") == 0);
    REQUIRE_FALSE(receiver.isValidFrame(1));
}

TEST_CASE_METHOD(FrameReceiverFixture, "not masked", "[websocket]")
{
    REQUIRE(write_n_check_more("\x81\x01") == 0);
    REQUIRE_FALSE(receiver.isValidFrame(2));
}

TEST_CASE_METHOD(FrameReceiverFixture, "too long", "[websocket]")
{
    REQUIRE(write_n_check_more("\x81\xfe") == 0);
    REQUIRE_FALSE(receiver.isValidFrame(2));

    REQUIRE(write_n_check_more("\x81\xff") == 0);
    REQUIRE_FALSE(receiver.isValidFrame(2));
}

TEST_CASE_METHOD(FrameReceiverFixture, "parse opcode", "[websocket]")
{
    receiver.addBytes(write("\x81\x80KKKK"));
    REQUIRE(receiver.opcode() == websocket::Opcode::Text);
}

TEST_CASE_METHOD(FrameReceiverFixture, "parse length", "[websocket]")
{
    receiver.addBytes(write("\x81\x81KKKKD"));
    REQUIRE(receiver.payloadLen() == 1);
}

TEST_CASE_METHOD(FrameReceiverFixture, "unmask", "[websocket]")
{
    receiver.addBytes(write("\x81\x85" "\x1\x1\x1\x1" "10325"));
    receiver.unmask();
    REQUIRE(receiver.message() == "01234");
}
