// Websocket Server implementation
// Belongs to the public domain

#pragma once

#include <atomic>
#include <functional>
#include <ostream>
#include <thread>
#include <memory>
#include <boost/asio.hpp>

namespace websocket
{
    class Acceptor
    {
    public:
        template<typename Callback>
        Acceptor(boost::asio::io_service& ioService,
            const std::string& ip, unsigned short port,
            std::ostream& log, Callback&& callback)
            : m_ioService{ioService}
            , m_acceptor{m_ioService, boost::asio::ip::tcp::endpoint{boost::asio::ip::address_v4::from_string(ip), port}}
            , m_log{&log}
            , m_callback{callback}
        {
            beginAccept();
        }
    
        void stop()
        {
            m_stop = true;
            boost::system::error_code ignoreError;
            m_acceptor.cancel(ignoreError);
            m_acceptor.close(ignoreError);
        }

    private:
        void beginAccept()
        {
            m_clientSocket = std::make_unique<boost::asio::ip::tcp::socket>(m_ioService);
            m_acceptor.async_accept(*m_clientSocket, [this](boost::system::error_code ec){ onAccept(ec); });
        }

        void onAccept(boost::system::error_code ec)
        {
            if (m_stop)
                return;

            if (!ec)
            {
                callCallback();
            }
            else
            {
                (*m_log) << "accept error: " << ec << '\n';
            }

            beginAccept();
        }

        void callCallback()
        {
            try
            {
                m_callback(std::move(*m_clientSocket));
            }
            catch (std::exception& e)
            {
                (*m_log) << "accept callback error: " << e.what() << '\n';
            }
        }

        std::atomic<bool> m_stop{false};
        std::reference_wrapper<boost::asio::io_service> m_ioService;
        std::ostream* m_log;
        boost::asio::ip::tcp::acceptor m_acceptor;
        std::function<void(boost::asio::ip::tcp::socket&&)> m_callback;
        std::unique_ptr<boost::asio::ip::tcp::socket> m_clientSocket;
    };
}