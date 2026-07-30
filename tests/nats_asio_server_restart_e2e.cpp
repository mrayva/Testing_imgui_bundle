#include "database/nats_asio_flexbuffer_transport.h"

#include <nats_asio/nats_asio.hpp>

#include <asio/co_spawn.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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

std::vector<char> MakePayload(std::int64_t id, const char* symbol, double price) {
    flexbuffers::Builder builder;
    builder.Map([&]() {
        builder.String("source", "nats_asio_server_restart_e2e");
        builder.Vector("rows", [&]() {
            builder.Map([&]() {
                builder.Int("id", id);
                builder.String("symbol", symbol);
                builder.Double("price", price);
            });
        });
    });
    builder.Finish();
    return {builder.GetBuffer().begin(), builder.GetBuffer().end()};
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: nats_asio_server_restart_e2e READY_FILE RESUME_FILE\n";
        return 1;
    }

    const auto portText = std::getenv("NATS_ASIO_RESTART_PORT");
    const auto port = static_cast<std::uint16_t>(portText ? std::atoi(portText) : 14223);
    const std::string readyFile = argv[1];
    const std::string resumeFile = argv[2];
    constexpr const char* subject = "imgui.flexbuffer.server_restart.e2e";

    db::FlexbufferTableWidget<4096> widget;
    widget.AddColumn("id", "ID");
    widget.AddColumn("symbol", "Symbol");
    widget.AddColumn("price", "Price");
    db::NatsAsioFlexbufferTransport transport(widget);
    if (!transport.Connect("127.0.0.1", port, subject) ||
        !WaitFor([&] { return transport.GetStatus() == "Connected"; },
                 std::chrono::seconds(5))) {
        std::cerr << "subscriber did not connect: " << transport.GetLastError() << '\n';
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

    auto stopPublisher = [&] {
        publisher->stop();
        publisherIo.stop();
        if (publisherThread.joinable()) publisherThread.join();
    };
    if (!WaitFor([&] { return publisher->is_connected(); }, std::chrono::seconds(5))) {
        stopPublisher();
        transport.Disconnect();
        return 3;
    }

    auto publish = [&](std::vector<char> payload) {
        auto future = asio::co_spawn(
            publisherIo,
            [publisher, payload = std::move(payload)]() mutable
                -> asio::awaitable<nats_asio::status> {
                co_return co_await publisher->publish(subject, payload, std::nullopt);
            },
            asio::use_future);
        return !future.get().failed();
    };

    if (!publish(MakePayload(1, "AAPL", 181.25)) ||
        !WaitFor(
            [&] {
                widget.Sync();
                return widget.GetRowCount() == 1 && widget.GetCell(0, 0) == "1" &&
                       widget.GetCell(0, 1) == "AAPL" &&
                       widget.GetCell(0, 2) == "181.250000";
            },
            std::chrono::seconds(5))) {
        stopPublisher();
        transport.Disconnect();
        return 4;
    }

    std::ofstream(readyFile).put('\n');
    const bool observedDisconnect = WaitFor(
        [&] { return transport.GetStatus() != "Connected"; }, std::chrono::seconds(10));
    if (!observedDisconnect) {
        std::cerr << "subscriber never observed the server outage\n";
        stopPublisher();
        transport.Disconnect();
        return 5;
    }

    if (!WaitFor([&] { return std::filesystem::exists(resumeFile); },
                 std::chrono::seconds(15))) {
        std::cerr << "test controller did not restart the server\n";
        stopPublisher();
        transport.Disconnect();
        return 6;
    }

    if (!WaitFor([&] { return transport.GetStatus() == "Connected"; },
                 std::chrono::seconds(15)) ||
        !WaitFor([&] { return publisher->is_connected(); }, std::chrono::seconds(15)) ||
        !publish(MakePayload(2, "MSFT", 421.5)) ||
        !WaitFor(
            [&] {
                widget.Sync();
                return widget.GetRowCount() == 1 && widget.GetCell(0, 0) == "2" &&
                       widget.GetCell(0, 1) == "MSFT" &&
                       widget.GetCell(0, 2) == "421.500000" && widget.MissedCount() == 0;
            },
            std::chrono::seconds(5))) {
        std::cerr << "delivery after server restart failed: status=" << transport.GetStatus()
                  << " error=" << transport.GetLastError() << '\n';
        stopPublisher();
        transport.Disconnect();
        return 7;
    }

    stopPublisher();
    transport.Disconnect();
    return 0;
}
