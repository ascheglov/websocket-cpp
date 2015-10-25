#include "details/frames.hpp"

#include "third_party/catch/catch.hpp"

namespace ws_details = websocket::details;

namespace
{
    struct FrameReceiverFixture
    {
        ws_details::FrameReceiver receiver;

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
        }
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
    REQUIRE(receiver.opcode() == ws_details::Opcode::Text);
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

TEST_CASE("ServerFrame construction", "[websocket]")
{
    auto&& test = [](unsigned dataLen, unsigned expectedHeaderLen, const char* expectedHeader)
    {
        std::string data(dataLen, 'x');
        ws_details::ServerFrame frame{ws_details::Opcode::Text, data};
        REQUIRE(frame.m_headerLen == expectedHeaderLen);
        REQUIRE(std::memcmp(frame.m_header, expectedHeader, frame.m_headerLen) == 0);
        REQUIRE(frame.m_data == data);
    };

    test(3, 2, "\x81\x03");
    test(125, 2, "\x81\x7d");

    test(126, 4, "\x81\x7e\x00\x7e");
    test(0xAABB, 4, "\x81\x7e\xaa\xbb");
    test(0xFFFF, 4, "\x81\x7e\xff\xff");

    test(0x10000, 10, "\x81\x7f\x00\x00\x00\x00\x00\x01\x00\x00");
    test(0x100ff, 10, "\x81\x7f\x00\x00\x00\x00\x00\x01\x00\xff");
}
