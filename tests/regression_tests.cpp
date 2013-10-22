#include "Server.hpp"

#include "catch_wrap.hpp"

#include <thread>
#include <tuple>
#include <boost/asio.hpp>

namespace
{
    template<std::size_t N>
    std::string str(const char(&s)[N])
    {
        return{s, s + N - 1};
    }

    const auto ServerIp = "127.0.0.1";
    const unsigned short ServerPort = 8888;

    using event_t = std::tuple<websocket::Event, websocket::ConnectionId, std::string>;
}

namespace std
{
    ostream& operator<<(ostream& o, const event_t& e)
    {
        o << '#' << std::get<1>(e) << ' ';
        switch (std::get<0>(e))
        {
        case websocket::Event::NewConnection: o << "connected"; break;
        case websocket::Event::Message: o << "says"; break;
        case websocket::Event::Disconnect: o << "disconnected"; break;
        default: o << "???"; break;
        }
        o << " '" << std::get<2>(e) << '\'';
        return o;
    }
}

namespace
{
    struct Client
    {
        boost::asio::io_service m_ioService;
        boost::asio::ip::tcp::socket m_socket{ m_ioService };

        Client()
        {
            boost::asio::ip::tcp::endpoint serverEndpoint{ boost::asio::ip::address_v4::from_string(ServerIp), ServerPort };
            m_socket.connect(serverEndpoint);

            std::string request =
                "GET / HTTP/1.1" "\r\n"
                "Host: localhost" "\r\n"
                "Upgrade: websocket" "\r\n"
                "Connection: Upgrade" "\r\n"
                "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==" "\r\n"
                "Sec-WebSocket-Version: 13" "\r\n"
                "\r\n";
            boost::asio::write(m_socket, boost::asio::buffer(request));

            boost::asio::streambuf replyBuf;
            boost::asio::read_until(m_socket, replyBuf, "\r\n\r\n");
            std::stringstream replyStream;
            replyStream << &replyBuf;
            auto replyStr = replyStream.str();

            std::string expectedReply =
                "HTTP/1.1 101 Switching Protocols\r\n"
                "Upgrade: websocket\r\n"
                "Connection: Upgrade\r\n"
                "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n"
                "\r\n";

            REQUIRE(replyStr == expectedReply);
        }

        template<std::size_t N>
        void sendFrame(const char(&s)[N])
        {
            std::string frame{s, s + N - 1};
            boost::asio::write(m_socket, boost::asio::buffer(frame));
        }

        std::string recvFrame()
        {
            const unsigned bufSize = 0x20000;
            static unsigned char buf[bufSize];
            auto n = boost::asio::read(m_socket, boost::asio::buffer(buf), boost::asio::transfer_at_least(1));

            if (buf[1] == 126)
            {
                unsigned headerSize = 2 + 2;
                assert(n > headerSize);
                unsigned dataSize = (buf[2] << 8) | buf[3];
                if (n < dataSize + headerSize)
                    n += boost::asio::read(m_socket, boost::asio::buffer(buf + n, bufSize - n), boost::asio::transfer_at_least(1));
            }
            else if (buf[1] == 127)
            {
                unsigned headerSize = 2 + 8;
                assert(n > headerSize);
                unsigned dataSize = (buf[6] << 24) | (buf[7] << 16) | (buf[8] << 8) | buf[9];
                if (n < dataSize + headerSize)
                    n += boost::asio::read(m_socket, boost::asio::buffer(buf + n, bufSize - n), boost::asio::transfer_at_least(1));
            }

            auto s = (const char*)buf;
            return {s, s + n};
        }

        ~Client()
        {
            m_socket.close();
        }
    };

    struct WebsocketTestsFixture
    {
        websocket::Server server;

        WebsocketTestsFixture()
        {
            server.start(ServerIp, ServerPort, std::cout);
        }

        event_t waitServerEvent()
        {
            for (auto n = 0; n < 10; ++n)
            {
                websocket::Event event;
                websocket::ConnectionId connId;
                std::string message;
                if (server.poll(event, connId, message))
                    return event_t(event, connId, message);

                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }

            FAIL("timeout");
            return{}; // suppress warning
        }

        void waitServerEvent(websocket::Event expectedEvent)
        {
            auto&& e = waitServerEvent();
            REQUIRE(std::get<0>(e) == expectedEvent);
        }

        ~WebsocketTestsFixture()
        {
            server.stop();
        }
    };
}

TEST_CASE_METHOD(WebsocketTestsFixture, "New connection", "[websocket][slow]")
{
    Client client;
    REQUIRE(waitServerEvent() == event_t(websocket::Event::NewConnection, 1, ""));
}

TEST_CASE_METHOD(WebsocketTestsFixture, "Client message", "[websocket][slow]")
{
    Client client;
    waitServerEvent(websocket::Event::NewConnection);

    client.sendFrame("\x81\x84" "\x14\x7b\x35\x0f" "\x60\x1e\x46\x7b");
    REQUIRE(waitServerEvent() == event_t(websocket::Event::Message, 1, "test"));
}

TEST_CASE_METHOD(WebsocketTestsFixture, "Server message", "[websocket][slow]")
{
    Client client;
    waitServerEvent(websocket::Event::NewConnection);

    server.sendText(1, "test");
    REQUIRE(client.recvFrame() == "\x81\x04test");
}

TEST_CASE_METHOD(WebsocketTestsFixture, "Long server messages", "[websocket][slow]")
{
    Client client;
    waitServerEvent(websocket::Event::NewConnection);

    auto test = [&](int msgLen, const std::string& expectedFrameHeader)
    {
        std::string msg(msgLen, 'x');
        server.sendText(1, msg);
        auto actualFrame = client.recvFrame();
        return actualFrame == expectedFrameHeader + msg;
    };

//     REQUIRE(test(125, "\x81\x7d"));
//     REQUIRE(test(126, str("\x81\x7e\x00\x7e")));
    REQUIRE(test(0x10000, str("\x81\x7f" "\x00\x00\x00\x00" "\x00\x01\x00\x00")));
}

TEST_CASE_METHOD(WebsocketTestsFixture, "Client closes socket", "[websocket][slow]")
{
    {
        Client client;
        waitServerEvent(websocket::Event::NewConnection);
    }

    REQUIRE(waitServerEvent() == event_t(websocket::Event::Disconnect, 1, ""));
}

TEST_CASE_METHOD(WebsocketTestsFixture, "Client closes connection", "[websocket][slow]")
{
    Client client;
    waitServerEvent(websocket::Event::NewConnection);

    client.sendFrame("\x88\x80" "\xAA\xBB\xCC\xDD");
    REQUIRE(waitServerEvent() == event_t(websocket::Event::Disconnect, 1, ""));

    REQUIRE(client.recvFrame() == str("\x88\x00"));
}
