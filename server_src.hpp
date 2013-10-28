// Websocket Server implementation
// Belongs to the public domain

#pragma once

#include "Server.hpp"

#include <memory>
#include <mutex>
#include <thread>
#include <tuple>
#include <ostream>
#include <boost/asio.hpp>

#include "details/Acceptor.hpp"
#include "details/ServerLogic.hpp"

namespace websocket
{
    class Server::Impl
    {
    public:
        template<typename Callback>
        Impl(boost::asio::ip::tcp::endpoint endpoint, std::ostream& log, Callback&& callback)
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
                {
                    auto op = isBinary ? details::Opcode::Binary : details::Opcode::Text;
                    conn->sendFrame(op, message);
                }
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

        details::ServerLogic m_logic;
        details::Acceptor<details::ServerLogic> m_acceptor;
    };

    Server::Server() {}
    Server::~Server() {}
    void Server::start(const std::string& ip, unsigned short port, std::ostream& log)
    {
        assert(!m_impl);

        auto&& callback = [this](Event event, ConnectionId connId, std::string message)
        {
            decltype(m_queue) tmpList;
            tmpList.emplace_back(event, connId, std::move(message));

            std::lock_guard<std::mutex> lock{m_mutex};
            m_queue.splice(end(m_queue), tmpList);
        };

        boost::asio::ip::tcp::endpoint endpoint{boost::asio::ip::address_v4::from_string(ip), port};
        m_impl = std::make_unique<Impl>(endpoint, log, callback);
    }
    void Server::stop() { m_impl->stop(); }
    void Server::sendText(ConnectionId connId, std::string message) { m_impl->send(connId, std::move(message), false); }
    void Server::sendBinary(ConnectionId connId, std::string message) { m_impl->send(connId, std::move(message), true); }
    void Server::drop(ConnectionId connId) { m_impl->drop(connId); }

    bool Server::poll(Event& event, ConnectionId& connId, std::string& message)
    {
        decltype(m_queue) tmpList;

        {
            std::lock_guard<std::mutex> lock{m_mutex};
            if (m_queue.empty())
                return false;

            tmpList.splice(begin(tmpList), m_queue, begin(m_queue));
        }
        
        std::tie(event, connId, message) = std::move(tmpList.front());
        return true;
    }
}