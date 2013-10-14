// Websocket Server implementation
// Belongs to the public domain

#pragma once

#include <algorithm> // required by asio.hpp, will be fixed in boost 1.55
#include <atomic>
#include <functional>
#include <memory>
#include <ostream>
#include <string>
#include <thread>
#include <unordered_map>
#include <asio.hpp>

#include "details/Acceptor.hpp"
#include "details/Connection.hpp"
#include "details/handshake.hpp"
#include "server_fwd.hpp"

namespace websocket
{
    class ServerImpl
    {
    public:
        template<typename Callback>
        ServerImpl(const std::string& ip, unsigned short port,
            std::ostream& log, Callback&& callback)
            : m_log(log)
            , m_callback(callback)
        {
            m_workerThread.reset(new std::thread{[this]{ workerThread(); }});
            m_acceptor = std::make_unique<Acceptor>(
                m_ioService, ip, port, log,
                [this](asio::ip::tcp::socket&& s){ onAccept(std::move(s)); });
        }

        ~ServerImpl()
        {
            if (!m_isStopped)
                stop();
        }

        void stop()
        {
            m_isStopped = true;

            m_acceptor->stop();

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
                    m_log << "ERROR: " << e.what() << std::endl;
                }
            }
        }

        template<typename F>
        void enqueue(F&& f)
        {
            m_ioService.post(std::forward<F>(f));
        }

        void onAccept(asio::ip::tcp::socket socket)
        {
            if (!performHandshake(socket))
                return;

            assert(!m_newConnection);
            ++m_lastConnId;
            m_newConnection = std::make_unique<Connection>(m_lastConnId, std::move(socket));

            enqueue([this]{ onHandshakeComplete(); });
        }

        void onHandshakeComplete()
        {
            auto id = m_newConnection->m_id;
            m_connections[id] = std::move(m_newConnection);
            beginRecvFrame(*m_connections[id]);
            m_callback(Event::NewConnection, id, "");
        }

        bool performHandshake(asio::ip::tcp::socket& socket)
        {
            asio::streambuf buf;
            asio::read_until(socket, buf, "\r\n\r\n");
            std::istream requestStream(&buf);

            std::ostringstream replyStream;
            auto status = handshake(requestStream, replyStream);

            asio::error_code ec;
            asio::write(socket, asio::buffer(replyStream.str()), ec);

            if (status != http::Status::OK)
            {
                m_log << "Handshake: error " << (int)status << "\n";
                return false;
            }

            if (ec)
            {
                m_log << "Handshake: write error: " << ec << "\n";
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
            asio::async_write(conn.m_socket, asio::buffer(conn.m_sendQueue.front()),
                [this, id](const asio::error_code& ec, std::size_t)
            {
                if (auto conn = find(id))
                    onSendComplete(*conn, ec);
            });
        }

        void onSendComplete(Connection& conn, const asio::error_code& ec)
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
                [this, id](const asio::error_code& ec, std::size_t bytesTransferred)
            {
                if (auto connPtr = find(id))
                    onRecvComplete(*connPtr, ec, bytesTransferred);
            });
        }

        void onRecvComplete(Connection& conn, const asio::error_code& ec, std::size_t bytesTransferred)
        {
            conn.m_isReading = false;
            if (ec)
            {
                if (ec.value() != asio::error::eof)
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

        asio::io_service m_ioService;

        std::unique_ptr<Acceptor> m_acceptor;

        std::unique_ptr<std::thread> m_workerThread;

        ConnectionId m_lastConnId{0};
        std::unordered_map<ConnectionId, std::unique_ptr<Connection>> m_connections;

        std::function<void(Event, ConnectionId, std::string)> m_callback;

        std::unique_ptr<Connection> m_newConnection;
    };
}