#pragma once

#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <atomic>
#include <queue>

struct NatsMessage {
    std::string subject;
    std::string data;
};

class NatsClient {
public:
    NatsClient();
    ~NatsClient();

    bool Connect(const std::string& url);
    void Disconnect();
    bool IsConnected() const;

    void Subscribe(const std::string& subject);
    void Publish(const std::string& subject, const std::string& data);

    // Poll for new messages (call this in your Gui loop)
    std::vector<NatsMessage> PollMessages();
    void PushMessage(const std::string& subject, const std::string& data);

    std::string GetConnectionStatus() const;
    std::string GetLastError() const;

    void UpdateStatus(const std::string& status);
    void UpdateError(const std::string& error);

private:
    std::atomic<bool> m_connected{false};

    mutable std::mutex m_stateMutex;
    std::string m_lastError;
    std::string m_status = "Disconnected";
    void* m_nativeData = nullptr;

    std::mutex m_messageMutex;
    std::queue<NatsMessage> m_incomingMessages;
};
