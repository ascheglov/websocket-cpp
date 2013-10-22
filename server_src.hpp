// Websocket Server implementation
// Belongs to the public domain

#pragma once

#include "Server.hpp"

#include <mutex>
#include <tuple>

#include "ServerImpl.hpp"

namespace websocket
{
    Server::Server() {}
    Server::~Server() {}
    void Server::start(const std::string& ip, unsigned short port, std::ostream& log)
    {
        assert(!m_impl);

        auto&& callback = [this](Event event, ConnectionId connId, std::string message)
        {
            std::lock_guard<std::mutex> lock{m_mutex};
            m_queue.emplace_back(event, connId, std::move(message));
        };

        boost::asio::ip::tcp::endpoint endpoint{boost::asio::ip::address_v4::from_string(ip), port};
        m_impl = std::make_unique<ServerImpl>(endpoint, log, callback);
    }
    void Server::stop() { m_impl->stop(); }
    void Server::sendText(ConnectionId connId, std::string message) { m_impl->send(connId, std::move(message), false); }
    void Server::sendBinary(ConnectionId connId, std::string message) { m_impl->send(connId, std::move(message), true); }
    void Server::drop(ConnectionId connId) { m_impl->drop(connId); }

    bool Server::poll(Event& event, ConnectionId& connId, std::string& message)
    {
        std::lock_guard<std::mutex> lock{m_mutex};
        if (m_queue.empty())
            return false;

        std::tie(event, connId, message) = m_queue.front();
        m_queue.pop_front();
        return true;
    }
}