#include "nats_client.h"

#include <string>

extern "C" void OnNatsStatusJS(const char* status);
extern "C" void OnNatsErrorJS(const char* error);

int main() {
    NatsClient client;

    if (client.IsConnected()) {
        return 1;
    }

    OnNatsStatusJS("Connected");
    if (!client.IsConnected()) {
        return 2;
    }
    if (client.GetConnectionStatus() != "Connected") {
        return 3;
    }

    OnNatsErrorJS("simulated error");
    if (client.GetLastError() != "simulated error") {
        return 4;
    }

    OnNatsStatusJS("Disconnected");
    if (client.IsConnected()) {
        return 5;
    }
    if (client.GetConnectionStatus() != "Disconnected") {
        return 6;
    }

    return 0;
}
