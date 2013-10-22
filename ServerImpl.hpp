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

            m_connTable.closeAll();

            m_workerThread->join();
        }

        void send(ConnectionId connId, std::string message, bool isBinary)
        {
            enqueue([=]
            {
                if (auto conn = m_connTable.find(connId))
                    conn->sendFrame(isBinary ? Opcode::Binary : Opcode::Text, message);
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
        using conn_t = Connection<ServerImpl>;
        friend struct conn_t;

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
                        auto& conn = m_connTable.add(std::move(clientSocket), *this);
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

        void dropImpl(conn_t& conn)
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

        ConnectionTable<ServerImpl> m_connTable;

        std::function<void(Event, ConnectionId, std::string)> m_callback;
    };
}