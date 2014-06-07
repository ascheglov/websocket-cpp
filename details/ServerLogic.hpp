// Websocket Server implementation
// Belongs to the public domain

#pragma once

#include <functional>
#include <ostream>
#include <string>
#include <boost/asio.hpp>

#include "Connection.hpp"
#include "handshake.hpp"
#include "../server_fwd.hpp"

namespace websocket { namespace details
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

        conn_t* find(ConnectionId id) { return m_connTable.find(id); }

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
}}
