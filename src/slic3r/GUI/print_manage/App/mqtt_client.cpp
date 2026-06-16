#include "mqtt_client.h"

// ActionCallback implementation
MQTTClient::ActionCallback::ActionCallback(MQTTClient& client) 
    : client_(client) {}

void MQTTClient::ActionCallback::connection_lost(const std::string& cause) {
    std::cerr << "Connection lost: " << (cause.empty() ? "unknown reason" : cause) << std::endl;
    if (client_.connection_callback_) {
        client_.connection_callback_(false);
    }
}

void MQTTClient::ActionCallback::message_arrived(mqtt::const_message_ptr msg) {
    std::string topic = msg->get_topic();
    // Preserve the raw bytes from the payload; some messages are not UTF-8 encoded.
    const auto& payload_ref = msg->get_payload_ref();
    std::string payload;
    if (!payload_ref.empty())
        payload.assign(payload_ref.data(), payload_ref.size());
    
    // Look up the corresponding callback function
    auto it = client_.topic_callbacks_.find(topic);
    if (it != client_.topic_callbacks_.end() && it->second) {
        it->second(topic, payload);
    } else {
        // If no callback for the specific topic was found, check for wildcard matches
        for (const auto& pair : client_.topic_callbacks_) {
            // Simple wildcard matching (real-world use may need more complex matching logic)
            if (pair.first.find('#') != std::string::npos ||
                pair.first.find('+') != std::string::npos) {
                // Full MQTT wildcard matching logic should be implemented here
                // Simplified handling: if the topic starts with the part before the wildcard, invoke the callback
                size_t wildcard_pos = pair.first.find_first_of("#+");
                if (wildcard_pos != std::string::npos) {
                    std::string prefix = pair.first.substr(0, wildcard_pos);
                    if (topic.find(prefix) == 0 && pair.second) {
                        pair.second(topic, payload);
                    }
                }
            }
        }
    }
}

void MQTTClient::ActionCallback::delivery_complete(mqtt::delivery_token_ptr token) {
    // Optional: implement message-delivery-complete handling
}
MQTTClient::ActionCallback::~ActionCallback()
{
    std::cout << "~ActionCallback";
}
// MQTTClient implementation
MQTTClient::MQTTClient(const std::string& server_address, const std::string& client_id)
    : server_address_(server_address),
      client_id_(client_id),
      connection_callback_(nullptr) {
    
    try {
        client_ = std::make_unique<mqtt::async_client>(server_address_, client_id_);
        callback_ = std::make_unique<ActionCallback>(*this);
        client_->set_callback(*callback_);
    } catch (const mqtt::exception& exc) {
        std::cerr << "Error creating MQTT client: " << exc.what() << std::endl;
        throw;
    }
}

MQTTClient::~MQTTClient() {
    try {
        if (isConnected()) {
            disconnect();
        }
    } catch (...) {
        // Do not throw exceptions in the destructor
    }
}

bool MQTTClient::connect(const std::string& username, 
                        const std::string& password, 
                        bool clean_session,
                        int keep_alive) {
    try {
        if (isConnected()) {
            return true;
        }
        
        // Set connection options
        conn_opts_ = mqtt::connect_options();
        conn_opts_.set_clean_session(clean_session);
        conn_opts_.set_keep_alive_interval(keep_alive);
        conn_opts_.set_automatic_reconnect(true);
        conn_opts_.set_max_inflight(20);                               // increase concurrency
        conn_opts_.set_mqtt_version(MQTTVERSION_3_1_1);
        
        
        if (!username.empty()) {
            conn_opts_.set_user_name(username);
        }
        if (!password.empty()) {
            conn_opts_.set_password(password);
        }
        
        // Connect to the server
        client_->connect(conn_opts_)->wait();
        
        if (connection_callback_) {
            connection_callback_(true);
        }
        
        return true;
    } catch (const mqtt::exception& exc) {
        std::cerr << "Error connecting to MQTT server: " << exc.what() << std::endl;
        return false;
    }
}

void MQTTClient::disconnect() {
    try {
        if (isConnected()) {
            client_->disconnect()->wait();
        }
    } catch (const mqtt::exception& exc) {
        std::cerr << "Error disconnecting from MQTT server: " << exc.what() << std::endl;
    }
}

bool MQTTClient::publish(const std::string& topic, 
                        const std::string& payload, 
                        int qos, 
                        bool retained) {
    try {
        if (!isConnected()) {
            std::cerr << "Not connected to MQTT server" << std::endl;
            return false;
        }
        
        auto msg = mqtt::make_message(topic, payload, qos, retained);
        client_->publish(msg)->wait();
        return true;
    } catch (const mqtt::exception& exc) {
        std::cerr << "Error publishing message: " << exc.what() << std::endl;
        return false;
    }
}

bool MQTTClient::subscribe(const std::string& topic, 
                          int qos, 
                          MessageCallback callback) {
    try {
        if (!isConnected()) {
            std::cerr << "Not connected to MQTT server" << std::endl;
            return false;
        }
        
        client_->subscribe(topic, qos)->wait();
        
        // Store the callback function
        if (callback) {
            topic_callbacks_[topic] = callback;
        }
        
        return true;
    } catch (const mqtt::exception& exc) {
        std::cerr << "Error subscribing to topic: " << exc.what() << std::endl;
        return false;
    }
}

bool MQTTClient::unsubscribe(const std::string& topic) {
    try {
        if (!isConnected()) {
            std::cerr << "Not connected to MQTT server" << std::endl;
            return false;
        }
        
        client_->unsubscribe(topic)->wait();
        
        // Remove the callback function
        topic_callbacks_.erase(topic);
        
        return true;
    } catch (const mqtt::exception& exc) {
        std::cerr << "Error unsubscribing from topic: " << exc.what() << std::endl;
        return false;
    }
}

void MQTTClient::setConnectionCallback(ConnectionCallback callback) {
    connection_callback_ = callback;
}

bool MQTTClient::isConnected() const {
    return client_ && client_->is_connected();
}
