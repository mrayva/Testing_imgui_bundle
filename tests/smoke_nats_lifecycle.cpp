#include "nats_client.h"

#include <chrono>
#include <thread>

int main() {
    NatsClient client;

    if (client.IsConnected()) {
        return 1;
    }

    if (client.Connect("")) {
        return 2;
    }

    // Start a connect attempt and immediately tear it down to exercise lifecycle safety.
    if (!client.Connect("nats://127.0.0.1:1")) {
        return 3;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    client.Disconnect();
    client.Disconnect(); // Idempotency smoke check.

    if (client.IsConnected()) {
        return 4;
    }

    return 0;
}
