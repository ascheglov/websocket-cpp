// Websocket Server implementation
// Belongs to the public domain

#pragma once

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
    class ServerLogic
    {
    public:
        template<typename Callback>
        ServerLogic(std::ostream& log, Callback&& callback)
            : m_log{log}
            , m_callback(callback)
        {}

        using conn_t = Connection<ServerLogic>;

        void processFrame(ConnectionId id, Opcode opcode, std::string message)
        {
            if (opcode == Opcode::Text || opcode == Opcode::Binary)
            {
                m_callback(Event::Message, id, message);
            }
            else
            {
                log("#", id, ": WARNING: unknown opcode ", (int)opcode);
            }
        }

        void drop(conn_t& conn)
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

        void onAccept(boost::asio::ip::tcp::socket& clientSocket, boost::asio::yield_context& yield)
        {
            if (performHandshake(clientSocket, yield))
            {
                auto& conn = m_connTable.add(std::move(clientSocket), *this);
                m_callback(Event::NewConnection, conn.m_id, "");
            }
        }

        conn_t* find(ConnectionId id) { return m_connTable.find(id);  }

        void stop()
        {
            m_connTable.closeAll();
        }

    private:
        void operator=(const ServerLogic&) = delete;

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

        std::ostream& m_log;
        std::function<void(Event, ConnectionId, std::string)> m_callback;
        ConnectionTable<ServerLogic> m_connTable;
    };

    template<class Callback>
    class Acceptor
    {
    public:
        Acceptor(boost::asio::io_service& ioService, boost::asio::ip::tcp::endpoint endpoint, Callback& callback)
            : m_acceptor{ioService, endpoint}
            , m_callback{callback}
        {
            boost::asio::spawn(ioService, [this](boost::asio::yield_context yield) { acceptLoop(yield); });
        }

        void stop()
        {
            m_isStopped = true;
            boost::system::error_code ingnoreError;
            m_acceptor.close(ingnoreError);
        }

    private:
        void acceptLoop(boost::asio::yield_context& yield)
        {
            for (;;)
            {
                boost::asio::ip::tcp::socket clientSocket{m_acceptor.get_io_service()};
                boost::system::error_code ec;
                m_acceptor.async_accept(clientSocket, yield[ec]);

                if (m_isStopped)
                    return;

                if (!ec)
                {
                    m_callback.onAccept(clientSocket, yield);
                }
                else
                {
                    m_callback.log("accept error: ", ec);
                }
            }
        }

        bool m_isStopped{false};
        boost::asio::ip::tcp::acceptor m_acceptor;
        Callback& m_callback;
    };

    class Server::Impl
    {
    public:
        template<typename Callback>
        Impl(boost::asio::ip::tcp::endpoint endpoint,
            std::ostream& log, Callback&& callback)
            : m_logic{log, std::forward<Callback>(callback)}
            , m_acceptor{m_ioService, endpoint, m_logic}            
        {
            m_workerThread.reset(new std::thread{[this]{ workerThread(); }});
        }

        ~Impl()
        {
            if (!m_isStopped)
                stop();
        }

        void stop()
        {
            enqueue([this]
            {
                m_isStopped = true;
                m_acceptor.stop();
                m_logic.stop();
            });

            m_workerThread->join();
        }

        void send(ConnectionId connId, std::string message, bool isBinary)
        {
            enqueue([=]
            {
                if (auto conn = m_logic.find(connId))
                    conn->sendFrame(isBinary ? Opcode::Binary : Opcode::Text, message);
            });
        }

        void drop(ConnectionId connId)
        {
            enqueue([=]
            {
                if (auto conn = m_logic.find(connId))
                    m_logic.drop(*conn);
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
                    m_logic.log("ERROR: ", e.what());
                }
            }
        }

        template<typename F>
        void enqueue(F&& f)
        {
            m_ioService.post(std::forward<F>(f));
        }

        bool m_isStopped{false};

        boost::asio::io_service m_ioService;
        std::unique_ptr<std::thread> m_workerThread;

        ServerLogic m_logic;
        Acceptor<ServerLogic> m_acceptor;
    };
}