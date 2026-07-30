#pragma once

#include "flexbuffer_table_widget.h"

#include <cstdint>
#include <memory>
#include <string>

namespace db {

/**
 * Native-only NATS transport for FlexBuffer snapshots.
 *
 * The nats_asio callback payload is valid only during the callback. Publish()
 * copies it into the widget mailbox before returning, preserving that lifetime
 * boundary for the UI thread.
 */
class NatsAsioFlexbufferTransport {
public:
    explicit NatsAsioFlexbufferTransport(FlexbufferTableWidget<4096>& widget);
    ~NatsAsioFlexbufferTransport();

    NatsAsioFlexbufferTransport(const NatsAsioFlexbufferTransport&) = delete;
    NatsAsioFlexbufferTransport& operator=(const NatsAsioFlexbufferTransport&) = delete;

    bool Connect(std::string address, std::uint16_t port, std::string subject);
    void Disconnect();

    bool IsConnected() const;
    std::string GetStatus() const;
    std::string GetLastError() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace db
