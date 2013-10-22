// Websocket Server implementation
// Belongs to the public domain

#pragma once

#include <string>
#include <deque>
#include <boost/asio.hpp>

#include "server_fwd.hpp"
#include "frames.hpp"

namespace websocket
{
    template<typename Callback>
    struct Connection
    {
        Connection(ConnectionId id, boost::asio::ip::tcp::socket socket, Callback& callback)
            : m_id{id}
            , m_socket{std::move(socket)}
            , m_callback(callback)
        {
            beginRecvFrame();
        }

        void close()
        {
            if (m_isClosed)
                return;

            m_isClosed = true;
            boost::system::error_code ignoreError;
            m_socket.cancel(ignoreError);
            m_socket.shutdown(boost::asio::socket_base::shutdown_both, ignoreError);
            m_socket.close(ignoreError);
        }

        void beginRecvFrame()
        {
            auto&& isComplete = [this](const boost::system::error_code& ec, std::size_t bytesTransferred)
            {
                return ec ? 0 : m_receiver.needReceiveMore(bytesTransferred);
            };

            auto&& buffer = boost::asio::buffer(m_receiver.getBufferTail(), m_receiver.getBufferTailSize());

            m_isReading = true;
            boost::asio::async_read(m_socket, buffer, isComplete, [this](const boost::system::error_code& ec, std::size_t bytesTransferred)
            {
                onRecvComplete(ec, bytesTransferred);
            });
        }

        void onRecvComplete(const boost::system::error_code& ec, std::size_t bytesTransferred)
        {
            m_isReading = false;

            if (ec)
            {
                if (ec.value() != boost::asio::error::eof)
                    m_callback.log("#", m_id, ": recv error: ", ec);
            }
            else if (!m_isClosed)
            {
                m_receiver.addBytes(bytesTransferred);
                if (m_receiver.isValidFrame())
                {
                    if (m_receiver.opcode() == Opcode::Close)
                    {
                        sendFrame(Opcode::Close, {});
                    }
                    else
                    {
                        m_callback.processMessage(m_id, m_receiver);
                        m_receiver.shiftBuffer();
                        beginRecvFrame();
                        return;
                    }
                }
                else
                {
                    m_callback.log("#", m_id, ": invalid frame");
                }
            }

            m_callback.dropImpl(*this);
        }

        void sendFrame(Opcode opcode, std::string data)
        {
            m_sendQueue.push_back(makeFrame(opcode, data));
            if (m_sendQueue.size() == 1)
                sendNext();
        }

        void sendNext()
        {
            m_isSending = true;
            boost::asio::async_write(m_socket, boost::asio::buffer(m_sendQueue.front()),
                [this](const boost::system::error_code& ec, std::size_t)
                {
                    onSendComplete(ec);
                });
        }

        void onSendComplete(const boost::system::error_code& ec)
        {
            m_isSending = false;
            if (ec)
            {
                m_callback.log("#", m_id, ": send error: ", ec);
            }
            else if (!m_isClosed)
            {
                m_sendQueue.pop_front();
                if (!m_sendQueue.empty())
                    sendNext();

                return;
            }

            m_callback.dropImpl(*this);
        }

        ConnectionId m_id;
        boost::asio::ip::tcp::socket m_socket;
        std::deque<std::string> m_sendQueue;
        FrameReceiver m_receiver;
        Callback& m_callback;
        bool m_isSending{false};
        bool m_isReading{false};
        bool m_isClosed{false};
    };

    template<typename Callback>
    class ConnectionTable
    {
    public:
        using conn_t = Connection<Callback>;

        conn_t& add(boost::asio::ip::tcp::socket&& socket, Callback& callback)
        {
            ++m_lastConnId;
            auto&& pair = m_connections.emplace(m_lastConnId,
                std::make_unique<conn_t>(m_lastConnId, std::move(socket), callback));
            return *pair.first->second;
        }

        conn_t* find(ConnectionId connId)
        {
            auto iter = m_connections.find(connId);
            return iter == m_connections.end() ? nullptr : iter->second.get();
        }

        void erase(conn_t& conn)
        {
            m_connections.erase(conn.m_id);
        }

        void closeAll()
        {
            for (auto&& conn : m_connections)
                conn.second->close();
        }

    private:
        ConnectionId m_lastConnId{0};
        std::unordered_map<ConnectionId, std::unique_ptr<conn_t>> m_connections;
    };
}