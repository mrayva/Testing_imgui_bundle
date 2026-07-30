#include "database/nats_asio_flexbuffer_transport.h"

#include <nats_asio/nats_asio.hpp>

#include <asio/co_spawn.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <future>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

template <typename Predicate>
bool WaitFor(Predicate&& predicate, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return predicate();
}

} // namespace

int main() {
    const auto portText = std::getenv("NATS_ASIO_E2E_PORT");
    const auto port = static_cast<std::uint16_t>(portText ? std::atoi(portText) : 4222);
    db::FlexbufferTableWidget<4096> widget;
    widget.AddColumn("id", "ID");
    widget.AddColumn("symbol", "Symbol");
    widget.AddColumn("price", "Price");

    db::NatsAsioFlexbufferTransport transport(widget);
    if (!transport.Connect("127.0.0.1", port, "imgui.flexbuffer.e2e")) return 1;
    if (!WaitFor([&] { return transport.GetStatus() == "Connected"; },
                 std::chrono::seconds(3))) {
        std::cerr << "transport did not connect: " << transport.GetLastError() << '\n';
        return 2;
    }

    nats_asio::aio publisherIo;
    auto publisher = nats_asio::create_connection(
        publisherIo,
        [](nats_asio::iconnection&) -> asio::awaitable<void> { co_return; },
        [](nats_asio::iconnection&) -> asio::awaitable<void> { co_return; },
        [](nats_asio::iconnection&, std::string_view) -> asio::awaitable<void> { co_return; },
        std::nullopt);
    nats_asio::connect_config config;
    config.address = "127.0.0.1";
    config.port = port;
    publisher->start(config);
    std::thread publisherThread([&] { publisherIo.run(); });

    if (!WaitFor([&] { return publisher->is_connected(); }, std::chrono::seconds(3))) {
        publisher->stop();
        publisherIo.stop();
        publisherThread.join();
        return 3;
    }

    auto publish = [&](std::int64_t id, std::string symbol,
                       double price) {
        flexbuffers::Builder builder;
        builder.Map([&]() {
            builder.String("source", "nats_asio_e2e");
            builder.Vector("rows", [&]() {
                builder.Map([&]() {
                    builder.Int("id", id);
                    builder.String("symbol", symbol);
                    builder.Double("price", price);
                });
            });
        });
        builder.Finish();

        auto payload = std::vector<char>(builder.GetBuffer().begin(), builder.GetBuffer().end());
        auto publishFuture = asio::co_spawn(
            publisherIo,
            [publisher, payload = std::move(payload)]() mutable
                -> asio::awaitable<nats_asio::status> {
                co_return co_await publisher->publish(
                    "imgui.flexbuffer.e2e", payload, std::nullopt);
            },
            asio::use_future);
        return !publishFuture.get().failed();
    };

    if (!publish(42, "AAPL", 181.25)) {
        publisher->stop();
        publisherIo.stop();
        publisherThread.join();
        return 4;
    }

    const bool firstReceived = WaitFor(
        [&] {
            widget.Sync();
            return widget.GetRowCount() == 1 && widget.GetCell(0, 0) == "42" &&
                   widget.GetCell(0, 1) == "AAPL" &&
                   widget.GetCell(0, 2) == "181.250000";
        },
        std::chrono::seconds(3));

    if (!firstReceived) {
        std::cerr << "initial delivery failed: transport status=" << transport.GetStatus()
                  << " error=" << transport.GetLastError() << " rows=" << widget.GetRowCount()
                  << '\n';
        publisher->stop();
        publisherIo.stop();
        publisherThread.join();
        transport.Disconnect();
        return 5;
    }

    transport.Disconnect();
    if (!WaitFor([&] { return transport.GetStatus() == "Disconnected"; },
                 std::chrono::seconds(3))) {
        std::cerr << "transport did not disconnect cleanly\n";
        publisher->stop();
        publisherIo.stop();
        publisherThread.join();
        return 6;
    }

    if (!transport.Connect("127.0.0.1", port, "imgui.flexbuffer.e2e") ||
        !WaitFor([&] { return transport.GetStatus() == "Connected"; },
                 std::chrono::seconds(3))) {
        std::cerr << "transport did not reconnect: " << transport.GetLastError() << '\n';
        publisher->stop();
        publisherIo.stop();
        publisherThread.join();
        transport.Disconnect();
        return 7;
    }

    if (!publish(43, "MSFT", 421.5)) {
        publisher->stop();
        publisherIo.stop();
        publisherThread.join();
        transport.Disconnect();
        return 8;
    }

    const bool reconnectedDelivery = WaitFor(
        [&] {
            widget.Sync();
            return widget.GetRowCount() == 1 && widget.GetCell(0, 0) == "43" &&
                   widget.GetCell(0, 1) == "MSFT" &&
                   widget.GetCell(0, 2) == "421.500000" && widget.MissedCount() == 0;
        },
        std::chrono::seconds(3));

    publisher->stop();
    publisherIo.stop();
    publisherThread.join();
    if (!reconnectedDelivery) {
        std::cerr << "transport status=" << transport.GetStatus()
                  << " error=" << transport.GetLastError()
                  << " rows=" << widget.GetRowCount()
                  << " cells=" << widget.GetCell(0, 0) << ','
                  << widget.GetCell(0, 1) << ',' << widget.GetCell(0, 2) << '\n';
    }
    transport.Disconnect();
    return reconnectedDelivery ? 0 : 9;
}
