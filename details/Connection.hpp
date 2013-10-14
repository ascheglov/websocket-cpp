// Websocket Server implementation
// Belongs to the public domain

#pragma once

#include <string>
#include <deque>
#include <asio.hpp>

#include "server_fwd.hpp"
#include "FrameReceiver.hpp"

namespace websocket
{
    struct Connection
    {
        Connection(ConnectionId id, asio::ip::tcp::socket socket)
            : m_id{id}
        , m_socket{std::move(socket)}
        {}

        void close()
        {
            if (m_isClosed)
                return;

            m_isClosed = true;
            asio::error_code ignoreError;
            m_socket.cancel(ignoreError);
            m_socket.shutdown(asio::socket_base::shutdown_both, ignoreError);
            m_socket.close(ignoreError);
        }

        template<typename ReadHandler>
        void beginRecvFrame(ReadHandler&& handler)
        {
            auto&& isComplete = [this](const asio::error_code& ec, std::size_t bytesTransferred)
            {
                return ec ? 0 : m_receiver.needReceiveMore(bytesTransferred);
            };

            auto&& buffer = asio::buffer(m_receiver.getBufferTail(), m_receiver.getBufferTailSize());

            m_isReading = true;
            asio::async_read(m_socket, buffer, isComplete, handler);
        }

        ConnectionId m_id;
        asio::ip::tcp::socket m_socket;
        std::deque<std::string> m_sendQueue;
        FrameReceiver m_receiver;

        bool m_isSending{false};
        bool m_isReading{false};
        bool m_isClosed{false};
    };
}