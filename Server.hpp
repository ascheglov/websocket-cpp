// Websocket Server implementation
// Belongs to the public domain

#pragma once

#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <tuple>

#include "server_fwd.hpp"

namespace websocket
{
    class Server
    {
    public:
        Server();
        ~Server();

        void start(const std::string& ip, unsigned short port, std::ostream& log);
        void stop();

        void sendText(ConnectionId connId, std::string message);
        void sendBinary(ConnectionId connId, std::string message);
        
        bool poll(Event& event, ConnectionId& connId, std::string& message);

        void drop(ConnectionId connId);

    private:
        class Impl;
        std::unique_ptr<Impl> m_impl;

        using tuple_t = std::tuple<Event, ConnectionId, std::string>;
        std::deque<tuple_t> m_queue;
        std::mutex m_mutex;
    };
}