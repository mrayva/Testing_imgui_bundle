#ifdef __EMSCRIPTEN__
#include "nats_client.h"
#include <emscripten.h>
#include <iostream>

// Global pointer to the current client instance for the callback
static NatsClient* g_instance = nullptr;

extern "C" {
EMSCRIPTEN_KEEPALIVE
void OnNatsMessageJS(const char* subject, const char* data) {
    if (g_instance) {
        g_instance->PushMessage(subject, data);
    }
}

EMSCRIPTEN_KEEPALIVE
void OnNatsStatusJS(const char* status) {
    if (g_instance) {
        g_instance->UpdateStatus(status);
    }
}

EMSCRIPTEN_KEEPALIVE
void OnNatsErrorJS(const char* error) {
    if (g_instance) {
        g_instance->UpdateError(error);
    }
}
}

EM_JS(void, nats_connect_js, (const char* url_ptr), {
    const url = UTF8ToString(url_ptr);

    (async () => {
        try {
            console.log("NATS: Importing nats.ws from jsdelivr...");
            const natsModule = await import('https://cdn.jsdelivr.net/npm/nats.ws/esm/nats.js');
            const {connect, StringCodec} = natsModule;

            if (!connect) throw new Error("connect not found");

            // Close previous connection if present.
            if (window.nats_conn) {
                try {
                    await window.nats_conn.close();
                } catch (closeErr) {
                    console.warn("NATS: Failed closing existing connection", closeErr);
                }
            }

            console.log("NATS: Connecting to", url);
            const nc = await connect({servers: [url]});
            window.nats_conn = nc;
            window.nats_sc = StringCodec();
            if (!window.nats_subs) {
                window.nats_subs = new Map();
            }
            console.log("NATS: Connected successfully");

            const statusStr = "Connected";
            const statusLen = lengthBytesUTF8(statusStr) + 1;
            const statusPtr = _malloc(statusLen);
            stringToUTF8(statusStr, statusPtr, statusLen);
            _OnNatsStatusJS(statusPtr);
            _free(statusPtr);
        } catch (err) {
            console.error("NATS Connection Error:", err);
            if (err && err.stack) {
                console.error("Error stack:", err.stack);
            }
            let errMsg = "Connection failed: ";
            if (err && err.message) {
                errMsg += err.message;
            } else if (typeof err === 'string') {
                errMsg += err;
            } else {
                errMsg += (err && err.toString) ? err.toString() : JSON.stringify(err);
            }

            const errLen = lengthBytesUTF8(errMsg) + 1;
            const errPtr = _malloc(errLen);
            stringToUTF8(errMsg, errPtr, errLen);

            const failStr = "Failed";
            const failLen = lengthBytesUTF8(failStr) + 1;
            const failPtr = _malloc(failLen);
            stringToUTF8(failStr, failPtr, failLen);

            _OnNatsErrorJS(errPtr);
            _OnNatsStatusJS(failPtr);

            _free(errPtr);
            _free(failPtr);
        }
    })();
});

EM_JS(void, nats_publish_js, (const char* subj_ptr, const char* data_ptr), {
    const subj = UTF8ToString(subj_ptr);
    const data = UTF8ToString(data_ptr);
    if (window.nats_conn && window.nats_sc) {
        window.nats_conn.publish(subj, window.nats_sc.encode(data));
    }
});

EM_JS(void, nats_subscribe_js, (const char* subj_ptr), {
    const subj = UTF8ToString(subj_ptr);
    if (window.nats_conn && window.nats_sc) {
        (async () => {
            const sub = window.nats_conn.subscribe(subj);
            if (!window.nats_subs) {
                window.nats_subs = new Map();
            }
            window.nats_subs.set(subj, sub);
            console.log("NATS: Subscribed to:", subj);
            try {
                for await (const m of sub) {
                    if (!window.nats_conn) {
                        break;
                    }
                    const msgData = window.nats_sc.decode(m.data);
                    const subjStr = m.subject;

                    // Call back into C++
                    const subjLen = lengthBytesUTF8(subjStr) + 1;
                    const dataLen = lengthBytesUTF8(msgData) + 1;
                    const subjPtr = _malloc(subjLen);
                    const dataPtr = _malloc(dataLen);
                    stringToUTF8(subjStr, subjPtr, subjLen);
                    stringToUTF8(msgData, dataPtr, dataLen);

                    _OnNatsMessageJS(subjPtr, dataPtr);

                    _free(subjPtr);
                    _free(dataPtr);
                }
            } catch (err) {
                const errMsg = "Subscribe error: " + (err && err.message ? err.message : String(err));
                const errLen = lengthBytesUTF8(errMsg) + 1;
                const errPtr = _malloc(errLen);
                stringToUTF8(errMsg, errPtr, errLen);
                _OnNatsErrorJS(errPtr);
                _free(errPtr);
            } finally {
                if (window.nats_subs) {
                    window.nats_subs.delete(subj);
                }
            }
        })();
    }
});

EM_JS(void, nats_disconnect_js, (), {
    (async () => {
        try {
            if (window.nats_subs) {
                for (const sub of window.nats_subs.values()) {
                    try {
                        sub.unsubscribe();
                    } catch (subErr) {
                        console.warn("NATS: unsubscribe failed", subErr);
                    }
                }
                window.nats_subs.clear();
            }
            if (window.nats_conn) {
                try {
                    await window.nats_conn.drain();
                } catch (drainErr) {
                    console.warn("NATS: drain failed, closing", drainErr);
                    try {
                        await window.nats_conn.close();
                    } catch (closeErr) {
                        console.warn("NATS: close failed", closeErr);
                    }
                }
            }
        } finally {
            window.nats_conn = undefined;
            window.nats_sc = undefined;
            const statusStr = "Disconnected";
            const statusLen = lengthBytesUTF8(statusStr) + 1;
            const statusPtr = _malloc(statusLen);
            stringToUTF8(statusStr, statusPtr, statusLen);
            _OnNatsStatusJS(statusPtr);
            _free(statusPtr);
        }
    })();
});

NatsClient::NatsClient() {
    g_instance = this;
}

NatsClient::~NatsClient() {
    if (g_instance == this) g_instance = nullptr;
}

bool NatsClient::Connect(const std::string& url) {
    if (url.empty()) {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_status = "Failed";
        m_lastError = "NATS URL cannot be empty";
        m_connected.store(false, std::memory_order_release);
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_status = "Connecting...";
        m_lastError = "";
    }
    m_connected.store(false, std::memory_order_release);
    nats_connect_js(url.c_str());
    return true;
}

void NatsClient::Disconnect() {
    nats_disconnect_js();
    m_connected.store(false, std::memory_order_release);
    std::lock_guard<std::mutex> lock(m_stateMutex);
    m_status = "Disconnected";
}

bool NatsClient::IsConnected() const {
    return m_connected.load(std::memory_order_acquire);
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
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_status = status;
    }
    m_connected.store(status == "Connected", std::memory_order_release);
}

void NatsClient::UpdateError(const std::string& error) {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    m_lastError = error;
}

void NatsClient::Subscribe(const std::string& subject) {
    nats_subscribe_js(subject.c_str());
}

void NatsClient::Publish(const std::string& subject, const std::string& data) {
    nats_publish_js(subject.c_str(), data.c_str());
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
