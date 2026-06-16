#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#include <mqtt/async_client.h>
#include <mqtt/callback.h>
#include <functional>
#include <string>
#include <map>
#include <vector>
#include <iostream>
#include <memory>

/**
 * @brief MQTT client wrapper class
 *
 * Wraps the Paho MQTT C++ library, providing a simple publish and subscribe interface
 */
class MQTTClient {
public:
    // Message-arrived callback function type
    using MessageCallback = std::function<void(const std::string& topic, 
                                              const std::string& payload)>;
    
    // Connection-status callback function type
    using ConnectionCallback = std::function<void(bool connected)>;

    /**
     * @brief Constructor
     * @param server_address MQTT server address, format: tcp://host:port
     * @param client_id Client ID; if empty, one is generated automatically
     */
    MQTTClient(const std::string& server_address, 
               const std::string& client_id = "");
    
    /**
     * @brief Destructor
     */
    ~MQTTClient();
    
    // Delete the copy constructor and assignment operator
    MQTTClient(const MQTTClient&) = delete;
    MQTTClient& operator=(const MQTTClient&) = delete;
    
    /**
     * @brief Connect to the MQTT server
     * @param username Username (optional)
     * @param password Password (optional)
     * @param clean_session Whether to clear the session
     * @param keep_alive Keep-alive interval (seconds)
     * @return Whether the connection succeeded
     */
    bool connect(const std::string& username = "", 
                 const std::string& password = "", 
                 bool clean_session = true,
                 int keep_alive = 60);
    
    /**
     * @brief Disconnect
     */
    void disconnect();
    
    /**
     * @brief Publish a message
     * @param topic Topic
     * @param payload Message content
     * @param qos Quality of service level (0, 1, 2)
     * @param retained Whether to retain the message
     * @return Whether the publish succeeded
     */
    bool publish(const std::string& topic, 
                 const std::string& payload, 
                 int qos = 1, 
                 bool retained = false);
    
    /**
     * @brief Subscribe to a topic
     * @param topic Topic; wildcards are supported
     * @param qos Quality of service level
     * @param callback Callback invoked when a message arrives
     * @return Whether the subscription succeeded
     */
    bool subscribe(const std::string& topic, 
                   int qos = 1, 
                   MessageCallback callback = nullptr);
    
    /**
     * @brief Unsubscribe
     * @param topic Topic
     * @return Whether the unsubscribe succeeded
     */
    bool unsubscribe(const std::string& topic);
    
    /**
     * @brief Set the connection-status callback
     * @param callback Callback function
     */
    void setConnectionCallback(ConnectionCallback callback);
    
    /**
     * @brief Check whether already connected
     * @return Connection status
     */
    bool isConnected() const;

private:
    // Internal callback handler class
    class ActionCallback : public mqtt::callback {
    public:
        ActionCallback(MQTTClient& client);
        ~ActionCallback();
        // Connection-lost callback
        void connection_lost(const std::string& cause) override;
        
        // Message-arrived callback
        void message_arrived(mqtt::const_message_ptr msg) override;
        
        // Message-delivery-complete callback (currently unused)
        void delivery_complete(mqtt::delivery_token_ptr token) override;
        
    private:
        MQTTClient& client_;
    };
    
    std::string server_address_;
    std::string client_id_;
    std::unique_ptr<mqtt::async_client> client_;
    std::unique_ptr<ActionCallback> callback_;
    
    // Subscribed topics and their corresponding callback functions
    std::map<std::string, MessageCallback> topic_callbacks_;
    
    // Connection-status callback
    ConnectionCallback connection_callback_;

    // Connection options
    mqtt::connect_options conn_opts_;
};

#endif // MQTT_CLIENT_H