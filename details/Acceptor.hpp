// Websocket Server implementation
// Belongs to the public domain

#pragma once

#include <atomic>
#include <functional>
#include <ostream>
#include <thread>
#include <memory>
#include <asio.hpp>

namespace websocket
{
    class Acceptor
    {
    public:
        template<typename Callback>
        Acceptor(asio::io_service& ioService,
            const std::string& ip, unsigned short port,
            std::ostream& log, Callback&& callback)
            : m_ioService{ioService}
            , m_acceptor{m_ioService, asio::ip::tcp::endpoint{asio::ip::address_v4::from_string(ip), port}}
            , m_log{&log}
            , m_callback{callback}
        {
            m_acceptThread.reset(new std::thread{[this]{ acceptThread(); }});
        }
    
        void stop()
        {
            m_stop = true;
            m_acceptor.cancel();
            m_acceptor.close();
            m_acceptThread->join();
        }

    private:
        void acceptThread()
        {
            while (!m_stop)
            {
                asio::ip::tcp::socket socket{m_ioService};
                asio::error_code ec;
                m_acceptor.accept(socket, ec);
                if (ec)
                {
                    if (m_stop)
                        return;

                    (*m_log) << "accept error: " << ec << '\n';
                    continue;
                }

                try
                {
                    m_callback(std::move(socket));
                }
                catch (std::exception& e)
                {
                    (*m_log) << "accept callback error: " << e.what() << '\n';
                }
            }
        }

        std::atomic<bool> m_stop{false};
        std::reference_wrapper<asio::io_service> m_ioService;
        std::ostream* m_log;
        asio::ip::tcp::acceptor m_acceptor;
        std::unique_ptr<std::thread> m_acceptThread;
        std::function<void(asio::ip::tcp::socket&&)> m_callback;
    };
}