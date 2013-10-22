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

            for (auto&& conn : m_connections)
                conn.second->close();

            enqueue([]{}); // JIC, wake up queue
            m_workerThread->join();
        }

        void send(ConnectionId connId, std::string message, bool isBinary)
        {
            enqueue([=]
            {
                if (auto conn = find(connId))
                    send(*conn, message, isBinary);
            });
        }

        void drop(ConnectionId connId)
        {
            enqueue([=]{ dropImpl(connId); });
        }

    private:
        void workerThread()
        {
            while (!m_isStopped)
            {
                try
                {
                    m_ioService.run_one();
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
                        createNewConnection(clientSocket);
                }
                else
                {
                    if (m_isStopped)
                        return;

                    log("accept error: ", ec);
                }
            }
        }

        void createNewConnection(boost::asio::ip::tcp::socket& socket)
        {
            ++m_lastConnId;
            m_connections[m_lastConnId] = std::make_unique<Connection>(m_lastConnId, std::move(socket));
            beginRecvFrame(*m_connections[m_lastConnId]);
            m_callback(Event::NewConnection, m_lastConnId, "");
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

        void send(Connection& conn, const std::string& message, bool isBinary)
        {
            std::string frame;
            frame.reserve(2 + message.size());
            frame.push_back(isBinary ? 0x82 : 0x81);

            auto n = message.size();
            if (message.size() <= 125)
            {
                frame.push_back((char)n);
            }
            else if (message.size() <= 0xFFFF)
            {
                frame.push_back(126);
                frame.push_back((n >> 8) & 0xFF);
                frame.push_back(n & 0xFF);
            }
            else if (message.size() <= 0xFFffFFff)
            {
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

            for (auto c : message)
                frame.push_back(c);

            sendFrame(conn, std::move(frame));
        }

        void sendFrame(Connection& conn, std::string frame)
        {
            assert(std::this_thread::get_id() == m_workerThread->get_id());
            conn.m_sendQueue.push_back(std::move(frame));
            if (conn.m_sendQueue.size() == 1)
                sendNext(conn);
        }

        void sendNext(Connection& conn)
        {
            auto id = conn.m_id;
            conn.m_isSending = true;
            boost::asio::async_write(conn.m_socket, boost::asio::buffer(conn.m_sendQueue.front()),
                [this, id](const boost::system::error_code& ec, std::size_t)
            {
                if (auto conn = find(id))
                    onSendComplete(*conn, ec);
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

            dropImpl(conn.m_id);
        }

        void beginRecvFrame(Connection& conn)
        {
            auto id = conn.m_id;
            conn.beginRecvFrame(
                [this, id](const boost::system::error_code& ec, std::size_t bytesTransferred)
            {
                if (auto connPtr = find(id))
                    onRecvComplete(*connPtr, ec, bytesTransferred);
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
                        sendFrame(conn, {"\x88\x00", 2});
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

            dropImpl(conn.m_id);
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

        void dropImpl(ConnectionId connId)
        {
            auto iter = m_connections.find(connId);
            if (iter == m_connections.end())
                return;

            auto& conn = *iter->second;
            if (!conn.m_isClosed)
            {
                conn.close();
                m_callback(Event::Disconnect, connId, "");
            }

            if (!conn.m_isReading && !conn.m_isSending)
                m_connections.erase(iter);
        }

        Connection* find(ConnectionId connId)
        {
            auto iter = m_connections.find(connId);
            return iter == m_connections.end() ? nullptr : iter->second.get();
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

        ConnectionId m_lastConnId{0};
        std::unordered_map<ConnectionId, std::unique_ptr<Connection>> m_connections;

        std::function<void(Event, ConnectionId, std::string)> m_callback;

        std::unique_ptr<Connection> m_newConnection;
    };
}