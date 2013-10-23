// Websocket Server implementation
// Belongs to the public domain

#pragma once

#include <boost/asio.hpp>
#include <boost/asio/spawn.hpp>

namespace websocket
{
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
}