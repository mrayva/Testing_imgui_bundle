#include "nats_asio_flexbuffer_transport.h"

#include <nats_asio/nats_asio.hpp>

#include <atomic>
#include <mutex>
#include <thread>

namespace db {

struct NatsAsioFlexbufferTransport::Impl {
    explicit Impl(FlexbufferTableWidget<4096>& table) : widget(table) {}

    FlexbufferTableWidget<4096>& widget;
    nats_asio::aio io;
    std::shared_ptr<nats_asio::iconnection> connection;
    nats_asio::isubscription_sptr subscription;
    std::thread worker;
    mutable std::mutex mutex;
    std::string status = "Disconnected";
    std::string lastError;
    std::string subject;
    std::atomic<std::uint64_t> epoch{0};
};

NatsAsioFlexbufferTransport::NatsAsioFlexbufferTransport(
    FlexbufferTableWidget<4096>& widget)
    : m_impl(std::make_unique<Impl>(widget)) {}

NatsAsioFlexbufferTransport::~NatsAsioFlexbufferTransport() {
    Disconnect();
}

bool NatsAsioFlexbufferTransport::Connect(std::string address, std::uint16_t port,
                                          std::string subject) {
    if (address.empty() || subject.empty()) {
        std::lock_guard lock(m_impl->mutex);
        m_impl->status = "Failed";
        m_impl->lastError = "NATS address and subject are required";
        return false;
    }

    Disconnect();
    const auto connectionEpoch = m_impl->epoch.fetch_add(1, std::memory_order_acq_rel) + 1;
    m_impl->io.restart();
    m_impl->subject = std::move(subject);
    {
        std::lock_guard lock(m_impl->mutex);
        m_impl->status = "Connecting...";
        m_impl->lastError.clear();
    }

    auto isCurrent = [this, connectionEpoch]() {
        return m_impl->epoch.load(std::memory_order_acquire) == connectionEpoch;
    };

    auto connected = [this, connectionEpoch](nats_asio::iconnection& connection)
        -> asio::awaitable<void> {
        if (m_impl->epoch.load(std::memory_order_acquire) != connectionEpoch) {
            co_return;
        }

        auto callback = [this, connectionEpoch](const nats_asio::message_view& message)
            -> asio::awaitable<void> {
            if (m_impl->epoch.load(std::memory_order_acquire) == connectionEpoch) {
                m_impl->widget.Publish(
                    reinterpret_cast<const std::uint8_t*>(message.payload.data()),
                    message.payload.size());
            }
            co_return;
        };
        auto [subscription, status] = co_await connection.subscribe(m_impl->subject, callback);
        if (status.failed()) {
            std::lock_guard lock(m_impl->mutex);
            m_impl->lastError = status.error();
            m_impl->status = "Failed";
            co_return;
        }

        if (m_impl->epoch.load(std::memory_order_acquire) != connectionEpoch) {
            subscription->cancel();
            co_return;
        }

        {
            std::lock_guard lock(m_impl->mutex);
            m_impl->subscription = std::move(subscription);
        }
        std::lock_guard lock(m_impl->mutex);
        m_impl->status = "Connected";
        co_return;
    };

    auto disconnected = [this, connectionEpoch](nats_asio::iconnection&)
        -> asio::awaitable<void> {
        if (m_impl->epoch.load(std::memory_order_acquire) == connectionEpoch) {
            std::lock_guard lock(m_impl->mutex);
            m_impl->status = "Disconnected";
        }
        co_return;
    };

    auto error = [this, connectionEpoch](nats_asio::iconnection&, std::string_view message)
        -> asio::awaitable<void> {
        if (m_impl->epoch.load(std::memory_order_acquire) == connectionEpoch) {
            std::lock_guard lock(m_impl->mutex);
            m_impl->lastError = std::string(message);
            m_impl->status = "Failed";
        }
        co_return;
    };

    auto connection = nats_asio::create_connection(m_impl->io, connected, disconnected, error,
                                                    std::nullopt);
    {
        std::lock_guard lock(m_impl->mutex);
        m_impl->connection = connection;
    }
    nats_asio::connect_config config;
    config.address = std::move(address);
    config.port = port;
    connection->start(config);
    m_impl->worker = std::thread([this, isCurrent]() {
        m_impl->io.run();
        if (isCurrent()) {
            std::lock_guard lock(m_impl->mutex);
            if (m_impl->status == "Connecting...") {
                m_impl->status = "Disconnected";
            }
        }
    });
    return true;
}

void NatsAsioFlexbufferTransport::Disconnect() {
    m_impl->epoch.fetch_add(1, std::memory_order_acq_rel);
    nats_asio::isubscription_sptr subscription;
    std::shared_ptr<nats_asio::iconnection> connection;
    {
        std::lock_guard lock(m_impl->mutex);
        subscription = std::move(m_impl->subscription);
        connection = std::move(m_impl->connection);
    }
    if (subscription) {
        subscription->cancel();
    }
    if (connection) {
        connection->stop();
    }
    m_impl->io.stop();
    if (m_impl->worker.joinable()) {
        m_impl->worker.join();
    }
    std::lock_guard lock(m_impl->mutex);
    m_impl->status = "Disconnected";
}

bool NatsAsioFlexbufferTransport::IsConnected() const {
    std::shared_ptr<nats_asio::iconnection> connection;
    {
        std::lock_guard lock(m_impl->mutex);
        connection = m_impl->connection;
    }
    return connection && connection->is_connected();
}

std::string NatsAsioFlexbufferTransport::GetStatus() const {
    std::lock_guard lock(m_impl->mutex);
    return m_impl->status;
}

std::string NatsAsioFlexbufferTransport::GetLastError() const {
    std::lock_guard lock(m_impl->mutex);
    return m_impl->lastError;
}

} // namespace db
