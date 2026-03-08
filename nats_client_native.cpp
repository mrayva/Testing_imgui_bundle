#ifndef __EMSCRIPTEN__
#include "nats_client.h"
#include <nats/nats.h>
#include <iostream>
#include <thread>
#include <memory>

struct NativeData {
    natsConnection* conn = nullptr;
    std::vector<natsSubscription*> subs;
};

static void onMsg(natsConnection* nc, natsSubscription* sub, natsMsg* msg, void* closure) {
    NatsClient* client = (NatsClient*)closure;
    if (client) {
        std::string subject = natsMsg_GetSubject(msg);
        std::string data(natsMsg_GetData(msg), natsMsg_GetDataLength(msg));
        client->PushMessage(subject, data);
    }
    natsMsg_Destroy(msg);
}

NatsClient::NatsClient() {
    natsStatus s = nats_Open(-1); // Initialize nats library
    if (s != NATS_OK) {
        std::cerr << "Failed to initialize NATS library: " << natsStatus_GetText(s) << std::endl;
    }
}

NatsClient::~NatsClient() {
    Disconnect();
    nats_Close();
}

bool NatsClient::Connect(const std::string& url) {
    if (url.empty()) {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_status = "Failed";
        m_lastError = "NATS URL cannot be empty";
        m_connected.store(false, std::memory_order_release);
        return false;
    }

    // Ensure prior connection attempt is fully stopped and joined before starting a new one.
    Disconnect();

    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_status = "Connecting...";
        m_lastError = "";
    }
    m_stopRequested.store(false, std::memory_order_release);

    std::string urlCopy = url;
    m_connectThread = std::thread([this, urlCopy]() {
        natsOptions* opts = nullptr;
        natsOptions_Create(&opts);
        natsOptions_SetURL(opts, urlCopy.c_str());
        natsOptions_SetTimeout(opts, 5000); // 5 second timeout

        natsConnection* conn = nullptr;
        natsStatus s = natsConnection_Connect(&conn, opts);
        natsOptions_Destroy(opts);

        if (m_stopRequested.load(std::memory_order_acquire)) {
            if (conn) {
                natsConnection_Close(conn);
                natsConnection_Destroy(conn);
            }
            return;
        }

        if (s == NATS_OK) {
            auto nd = std::make_unique<NativeData>();
            nd->conn = conn;
            {
                std::lock_guard<std::mutex> lock(m_stateMutex);
                if (m_stopRequested.load(std::memory_order_acquire)) {
                    // Disconnect won during connect setup; drop this connection.
                    if (nd->conn) {
                        natsConnection_Close(nd->conn);
                        natsConnection_Destroy(nd->conn);
                    }
                } else {
                    m_nativeData = nd.release();
                    m_status = "Connected";
                    m_connected.store(true, std::memory_order_release);
                }
            }
        } else {
            {
                std::lock_guard<std::mutex> lock(m_stateMutex);
                if (!m_stopRequested.load(std::memory_order_acquire)) {
                    m_status = "Failed";
                    m_lastError = natsStatus_GetText(s);
                }
            }
            if (!m_stopRequested.load(std::memory_order_acquire)) {
                m_connected.store(false, std::memory_order_release);
                std::cerr << "NATS connection failed: " << natsStatus_GetText(s) << std::endl;
            }
        }
    });

    return true;
}

std::string NatsClient::GetConnectionStatus() const {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    return m_status;
}

std::string NatsClient::GetLastError() const {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    return m_lastError;
}

void NatsClient::UpdateStatus(const std::string& status) {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    m_status = status;
}

void NatsClient::UpdateError(const std::string& error) {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    m_lastError = error;
}

void NatsClient::Disconnect() {
    m_stopRequested.store(true, std::memory_order_release);
    if (m_connectThread.joinable()) {
        m_connectThread.join();
    }

    std::lock_guard<std::mutex> lock(m_stateMutex);
    if (m_nativeData) {
        NativeData* nd = (NativeData*)m_nativeData;
        for (auto sub : nd->subs) {
            natsSubscription_Destroy(sub);
        }
        if (nd->conn) {
            natsConnection_Close(nd->conn);
            natsConnection_Destroy(nd->conn);
        }
        delete nd;
        m_nativeData = nullptr;
    }
    m_status = "Disconnected";
    m_connected.store(false, std::memory_order_release);
}

bool NatsClient::IsConnected() const {
    return m_connected.load(std::memory_order_acquire);
}

void NatsClient::Subscribe(const std::string& subject) {
    if (!m_connected.load(std::memory_order_acquire)) return;
    std::lock_guard<std::mutex> lock(m_stateMutex);
    if (!m_nativeData) return;
    NativeData* nd = (NativeData*)m_nativeData;
    natsSubscription* sub = nullptr;
    natsStatus s = natsConnection_Subscribe(&sub, nd->conn, subject.c_str(), onMsg, (void*)this);
    if (s == NATS_OK) {
        nd->subs.push_back(sub);
    }
}

void NatsClient::Publish(const std::string& subject, const std::string& data) {
    if (!m_connected.load(std::memory_order_acquire)) return;
    std::lock_guard<std::mutex> lock(m_stateMutex);
    if (!m_nativeData) return;
    NativeData* nd = (NativeData*)m_nativeData;
    natsConnection_PublishString(nd->conn, subject.c_str(), data.c_str());
}

void NatsClient::PushMessage(const std::string& subject, const std::string& data) {
    std::lock_guard<std::mutex> lock(m_messageMutex);
    m_incomingMessages.push({subject, data});
}

std::vector<NatsMessage> NatsClient::PollMessages() {
    std::lock_guard<std::mutex> lock(m_messageMutex);
    std::vector<NatsMessage> msgs;
    while (!m_incomingMessages.empty()) {
        msgs.push_back(m_incomingMessages.front());
        m_incomingMessages.pop();
    }
    return msgs;
}

#endif
