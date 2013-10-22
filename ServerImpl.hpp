// Websocket Server implementation
// Belongs to the public domain

#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <ostream>
#include <string>
#include <thread>
#include <unordered_map>
#include <boost/asio.hpp>
#include <boost/asio/spawn.hpp>

#include "details/Connection.hpp"
#include "details/handshake.hpp"
#include "server_fwd.hpp"

namespace websocket
{
    inline std::string makeFrame(Opcode opcode, std::string data)
    {
        std::string frame;
        
        const auto FinalFragmentFlag = 0x80;
        frame.push_back(FinalFragmentFlag | static_cast<char>(opcode));

        auto n = data.size();
        if (n <= 125)
        {
            frame.reserve(2 + n);
            frame.push_back((char)n);
        }
        else if (n <= 0xFFFF)
        {
            frame.reserve(1 + 2 + n);

            frame.push_back(126);

            frame.push_back((n >> 8) & 0xFF);
            frame.push_back(n & 0xFF);
        }
        else if (n <= 0xFFffFFff)
        {
            frame.reserve(1 + 8 + n);

            frame.push_back(127);

            frame.push_back(0);
            frame.push_back(0);
            frame.push_back(0);
            frame.push_back(0);
            frame.push_back((n >> 8 * 3) & 0xFF);
            frame.push_back((n >> 8 * 2) & 0xFF);
            frame.push_back((n >> 8 * 1) & 0xFF);
            frame.push_back(n & 0xFF);
        }
        else
        {
            throw std::length_error("websocket message is too long");
        }

        frame.append(data);
        return frame;
    }

    class ServerImpl
    {
    public:
        template<typename Callback>
        ServerImpl(boost::asio::ip::tcp::endpoint endpoint,
            std::ostream& log, Callback&& callback)
            : m_log(log)
            , m_acceptor{m_ioService, endpoint}
            , m_callback(callback)
        {
            boost::asio::spawn(m_ioService, [this](boost::asio::yield_context yield) { acceptLoop(yield); });

            m_workerThread.reset(new std::thread{[this]{ workerThread(); }});
        }

        ~ServerImpl()
        {
            if (!m_isStopped)
                stop();
        }

        void stop()
        {
            m_isStopped = true;

            boost::system::error_code ingnoreError;
            m_acceptor.close(ingnoreError);

            m_connTable.closeAll();

            m_workerThread->join();
        }

        void send(ConnectionId connId, std::string message, bool isBinary)
        {
            enqueue([=]
            {
                if (auto conn = m_connTable.find(connId))
                    sendFrame(*conn, isBinary ? Opcode::Binary : Opcode::Text, message);
            });
        }

        void drop(ConnectionId connId)
        {
            enqueue([=]
            {
                if (auto conn = m_connTable.find(connId))
                    dropImpl(*conn);
            });
        }

    private:
        void workerThread()
        {
            while (!m_isStopped)
            {
                try
                {
                    m_ioService.run();
                    assert(m_isStopped);
                }
                catch (std::exception& e)
                {
                    log("ERROR: ", e.what());
                }
            }
        }

        template<typename F>
        void enqueue(F&& f)
        {
            m_ioService.post(std::forward<F>(f));
        }

        void acceptLoop(boost::asio::yield_context& yield)
        {
            for (;;)
            {
                boost::asio::ip::tcp::socket clientSocket{m_ioService};
                boost::system::error_code ec;
                m_acceptor.async_accept(clientSocket, yield[ec]);

                if (!ec)
                {
                    if (performHandshake(clientSocket, yield))
                    {
                        auto& conn = m_connTable.add(std::move(clientSocket));
                        beginRecvFrame(conn);
                        m_callback(Event::NewConnection, conn.m_id, "");
                    }
                }
                else
                {
                    if (m_isStopped)
                        return;

                    log("accept error: ", ec);
                }
            }
        }

        bool performHandshake(boost::asio::ip::tcp::socket& socket, boost::asio::yield_context& yield)
        {
            boost::system::error_code ec;
            boost::asio::streambuf buf;
            boost::asio::async_read_until(socket, buf, "\r\n\r\n", yield[ec]);
            if (ec)
            {
                log("Handshake: read error: ", ec);
                return false;
            }

            std::istream requestStream(&buf);
            std::ostringstream replyStream;
            auto status = handshake(requestStream, replyStream);

            boost::asio::async_write(socket, boost::asio::buffer(replyStream.str()), yield[ec]);

            if (status != http::Status::OK)
            {
                log("Handshake: error ", (int)status);
                return false;
            }

            if (ec)
            {
                log("Handshake: write error: ", ec);
                return false;
            }

            return true;
        }

        void sendFrame(Connection& conn, Opcode opcode, std::string data)
        {
            assert(std::this_thread::get_id() == m_workerThread->get_id());
            conn.m_sendQueue.push_back(makeFrame(opcode, data));
            if (conn.m_sendQueue.size() == 1)
                sendNext(conn);
        }

        void sendNext(Connection& conn)
        {
            conn.m_isSending = true;
            boost::asio::async_write(conn.m_socket, boost::asio::buffer(conn.m_sendQueue.front()),
                [this, &conn](const boost::system::error_code& ec, std::size_t)
            {
                onSendComplete(conn, ec);
            });
        }

        void onSendComplete(Connection& conn, const boost::system::error_code& ec)
        {
            conn.m_isSending = false;
            if (ec)
            {
                log("#", conn.m_id, ": send error: ", ec);
            }
            else if (!conn.m_isClosed)
            {
                assert(std::this_thread::get_id() == m_workerThread->get_id());
                conn.m_sendQueue.pop_front();
                if (!conn.m_sendQueue.empty())
                    sendNext(conn);

                return;
            }

            dropImpl(conn);
        }

        void beginRecvFrame(Connection& conn)
        {
            conn.beginRecvFrame(
                [this, &conn](const boost::system::error_code& ec, std::size_t bytesTransferred)
            {
                onRecvComplete(conn, ec, bytesTransferred);
            });
        }

        void onRecvComplete(Connection& conn, const boost::system::error_code& ec, std::size_t bytesTransferred)
        {
            conn.m_isReading = false;
            if (ec)
            {
                if (ec.value() != boost::asio::error::eof)
                    log("#", conn.m_id, ": recv error: ", ec);
            }
            else if (!conn.m_isClosed)
            {
                conn.m_receiver.addBytes(bytesTransferred);
                if (conn.m_receiver.isValidFrame())
                {
                    if (conn.m_receiver.opcode() == Opcode::Close)
                    {
                        sendFrame(conn, Opcode::Close, {});
                    }
                    else
                    {
                        processMessage(conn.m_id, conn.m_receiver);
                        conn.m_receiver.shiftBuffer();
                        beginRecvFrame(conn);
                        return;
                    }
                }
                else
                {
                    log("#", conn.m_id, ": invalid frame");
                }
            }

            dropImpl(conn);
        }

        void processMessage(ConnectionId id, FrameReceiver& receiver)
        {
            auto opcode = receiver.opcode();

            if (opcode == Opcode::Text || opcode == Opcode::Binary)
            {
                receiver.unmask();
                m_callback(Event::Message, id, receiver.message());
            }
            else
            {
                log("#", id, ": WARNING: unknown opcode ", (int)opcode);
            }
        }

        void dropImpl(Connection& conn)
        {
            if (!conn.m_isClosed)
            {
                conn.close();
                m_callback(Event::Disconnect, conn.m_id, "");
            }

            if (!conn.m_isReading && !conn.m_isSending)
                m_connTable.erase(conn);
        }

        template<typename... Ts>
        void log(Ts&&... t)
        {
            int h[]{(m_log << t, 0)...};
            (void)h;
            m_log << std::endl;
        }

        std::atomic<bool> m_isStopped{false};
        std::ostream& m_log;

        boost::asio::io_service m_ioService;

        boost::asio::ip::tcp::acceptor m_acceptor;

        std::unique_ptr<std::thread> m_workerThread;

        ConnectionTable m_connTable;

        std::function<void(Event, ConnectionId, std::string)> m_callback;
    };
}