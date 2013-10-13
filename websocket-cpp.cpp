// Websocket Server implementation
// Belongs to the public domain

#include "websocket-cpp.hpp"

#include <cassert>
#include <atomic>
#include <algorithm>
#include <array>
#include <deque>
#include <mutex>
#include <tuple>
#include <thread>
#include <ostream>
#include <sstream>
#include <unordered_map>
#include <asio.hpp>

#include "details/Acceptor.hpp"
#include "details/handshake.hpp"
#include "details/FrameReceiver.hpp"

namespace websocket // implementation
{
    class EventQueue
    {
        using tuple_t = std::tuple<Event, ConnectionId, std::string>;

    public:
        void post(Event event, ConnectionId connId, std::string message)
        {
            std::lock_guard<std::mutex> lock{m_mutex};
            m_queue.emplace_back(event, connId, std::move(message));
        }

        bool poll(Event& event, ConnectionId& connId, std::string& message)
        {
            std::lock_guard<std::mutex> lock{m_mutex};
            if (m_queue.empty())
                return false;

            std::tie(event, connId, message) = m_queue.front();
            m_queue.pop_front();
            return true;
        }

    private:
        std::deque<tuple_t> m_queue;
        std::mutex m_mutex;
    };

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

    struct Server::Impl
    {
        Impl(const std::string& ip, unsigned short port, std::ostream& log)
            : m_log(log)
        {
            m_workerThread.reset(new std::thread{[this]{ workerThread(); }});
            m_acceptor = std::make_unique<Acceptor>(
                m_ioService, ip, port, log,
                [this](asio::ip::tcp::socket&& s){ onAccept(std::move(s)); });
        }

        ~Impl()
        {
            if (!m_isStopped)
                stop();
        }

        void stop()
        {
            m_isStopped = true;

            m_acceptor->stop();

            for (auto&& conn : m_connections)
                conn.second->close();

            enqueue([]{}); // JIC, wake up queue
            m_workerThread->join();
        }

        void send(ConnectionId connId, std::string message, bool isBinary)
        {
            enqueue([=]
            {
                if (auto conn = find(connId))
                    send(*conn, message, isBinary);
            });
        }

        bool poll(Event& event, ConnectionId& connId, std::string& message)
        {
            return m_eventQueue.poll(event, connId, message);
        }

        void drop(ConnectionId connId)
        {
            enqueue([=]{ dropImpl(connId); });
        }

    private:
        void workerThread()
        {
            while (!m_isStopped)
            {
                try
                {
                    m_ioService.run_one();
                }
                catch (std::exception& e)
                {
                    m_log << "ERROR: " << e.what() << std::endl;
                }
            }
        }

        template<typename F>
        void enqueue(F&& f)
        {
            m_ioService.post(std::forward<F>(f));
        }

        void onAccept(asio::ip::tcp::socket socket)
        {
            if (!performHandshake(socket))
                return;

            assert(!m_newConnection);
            ++m_lastConnId;
            m_newConnection = std::make_unique<Connection>(m_lastConnId, std::move(socket));

            enqueue([this]{ onHandshakeComplete(); });
        }

        void onHandshakeComplete()
        {
            auto id = m_newConnection->m_id;
            m_connections[id] = std::move(m_newConnection);
            beginRecvFrame(*m_connections[id]);
            m_eventQueue.post(Event::NewConnection, id, "");
        }

        bool performHandshake(asio::ip::tcp::socket& socket)
        {
            asio::streambuf buf;
            asio::read_until(socket, buf, "\r\n\r\n");
            std::istream requestStream(&buf);

            std::ostringstream replyStream;
            auto status = handshake(requestStream, replyStream);

            asio::error_code ec;
            asio::write(socket, asio::buffer(replyStream.str()), ec);

            if (status != http::Status::OK)
            {
                m_log << "Handshake: error " << (int)status << "\n";
                return false;
            }

            if (ec)
            {
                m_log << "Handshake: write error: " << ec << "\n";
                return false;
            }

            return true;
        }

        void send(Connection& conn, const std::string& message, bool isBinary)
        {
            std::string frame;
            frame.reserve(2 + message.size());
            frame.push_back(isBinary ? 0x82 : 0x81);

            auto n = message.size();
            if (message.size() <= 125)
            {
                frame.push_back((char)n);
            }
            else if (message.size() <= 0xFFFF)
            {
                frame.push_back(126);
                frame.push_back(n >> 8);
                frame.push_back(n & 0xFF);
            }
            else if (message.size() <= 0xFFffFFff)
            {
                frame.push_back(127);

                frame.push_back(0);
                frame.push_back(0);
                frame.push_back(0);
                frame.push_back(0);
                frame.push_back((n >> 8 * 3) & 0xFF);
                frame.push_back((n >> 8 * 2) & 0xFF);
                frame.push_back((n >> 8 * 1) & 0xFF);
                frame.push_back(n & 0xFF);
            }
            else
            {
                throw std::length_error("websocket message is too long");
            }

            for (auto c : message)
                frame.push_back(c);

            sendFrame(conn, std::move(frame));
        }

        void sendFrame(Connection& conn, std::string frame)
        {
            assert(std::this_thread::get_id() == m_workerThread->get_id());
            conn.m_sendQueue.push_back(std::move(frame));
            if (conn.m_sendQueue.size() == 1)
                sendNext(conn);
        }

        void sendNext(Connection& conn)
        {
            auto id = conn.m_id;
            conn.m_isSending = true;
            asio::async_write(conn.m_socket, asio::buffer(conn.m_sendQueue.front()),
                [this, id](const asio::error_code& ec, std::size_t)
                {
                    if (auto conn = find(id))
                        onSendComplete(*conn, ec);
                });
        }

        void onSendComplete(Connection& conn, const asio::error_code& ec)
        {
            conn.m_isSending = false;
            if (ec)
            {
                log("#", conn.m_id, ": send error: ", ec);
            }
            else if (!conn.m_isClosed)
            {
                assert(std::this_thread::get_id() == m_workerThread->get_id());
                conn.m_sendQueue.pop_front();
                if (!conn.m_sendQueue.empty())
                    sendNext(conn);

                return;
            }
                
            dropImpl(conn.m_id);
        }

        void beginRecvFrame(Connection& conn)
        {
            auto id = conn.m_id;
            conn.beginRecvFrame(
                [this, id](const asio::error_code& ec, std::size_t bytesTransferred)
                {
                    if (auto connPtr = find(id))
                        onRecvComplete(*connPtr, ec, bytesTransferred);
                });
        }

        void onRecvComplete(Connection& conn, const asio::error_code& ec, std::size_t bytesTransferred)
        {
            conn.m_isReading = false;
            if (ec)
            {
                if (ec.value() != asio::error::eof)
                    log("#", conn.m_id, ": recv error: ", ec);
            }
            else if (!conn.m_isClosed)
            {
                conn.m_receiver.addBytes(bytesTransferred);
                if (conn.m_receiver.isValidFrame())
                {
                    if (conn.m_receiver.opcode() == Opcode::Close)
                    {
                        sendFrame(conn, {"\x88\x00", 2});
                    }
                    else
                    {
                        processMessage(conn.m_id, conn.m_receiver);
                        conn.m_receiver.shiftBuffer();
                        beginRecvFrame(conn);
                        return;
                    }
                }
                else
                {
                    log("#", conn.m_id, ": invalid frame");
                }
            }

            dropImpl(conn.m_id);
        }

        void processMessage(ConnectionId id, FrameReceiver& receiver)
        {
            auto opcode = receiver.opcode();

            if (opcode == Opcode::Text || opcode == Opcode::Binary)
            {
                receiver.unmask();
                m_eventQueue.post(Event::Message, id, receiver.message());
            }
            else
            {
                log("#", id, ": WARNING: unknown opcode ", (int)opcode);
            }
        }

        void dropImpl(ConnectionId connId)
        {
            auto iter = m_connections.find(connId);
            if (iter == m_connections.end())
                return;

            auto& conn = *iter->second;
            if (!conn.m_isClosed)
            {
                conn.close();
                m_eventQueue.post(Event::Disconnect, connId, "");
            }
            
            if (!conn.m_isReading && !conn.m_isSending)
                m_connections.erase(iter);
        }

        Connection* find(ConnectionId connId)
        {
            auto iter = m_connections.find(connId);
            return iter == m_connections.end() ? nullptr : iter->second.get();
        }

        template<typename... Ts>
        void log(Ts&&... t)
        {
            int h[]{(m_log << t, 0)...};
            (void)h;
            m_log << std::endl;
        }

        std::atomic<bool> m_isStopped{false};
        std::ostream& m_log;

        asio::io_service m_ioService;

        std::unique_ptr<Acceptor> m_acceptor;
        
        std::unique_ptr<std::thread> m_workerThread;

        ConnectionId m_lastConnId{0};
        std::unordered_map<ConnectionId, std::unique_ptr<Connection>> m_connections;

        EventQueue m_eventQueue;

        std::unique_ptr<Connection> m_newConnection;
    };
}

namespace websocket // API
{
    Server::Server() {}
    Server::~Server() {}
    void Server::start(const std::string& ip, unsigned short port, std::ostream& log)
    {
        assert(!m_impl);
        m_impl.reset(new Impl{ip, port, log});
    }
    void Server::stop() { m_impl->stop(); }
    void Server::sendText(ConnectionId connId, std::string message) { m_impl->send(connId, std::move(message), false); }
    void Server::sendBinary(ConnectionId connId, std::string message) { m_impl->send(connId, std::move(message), true); }
    bool Server::poll(Event& event, ConnectionId& connId, std::string& message) { return m_impl->poll(event, connId, message); }
    void Server::drop(ConnectionId connId) { m_impl->drop(connId); }
}