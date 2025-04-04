/*
*complete this description:
*This example demonstrates how to use the MQTT library with the SIM800 module to publish and subscribe to messages on an MQTT broker.
*The example initializes the SIM800 module and establishes a GPRS connection for MQTT communication.
*It then connects to an MQTT broker, subscribes to a topic, and publishes a test message.
*The example also periodically publishes a message and checks for incoming messages from the broker.
*key Updates:
*readSerialChunks() Function:
*
*Reads incoming data in smaller chunks to avoid overflowing the inputBuffer.
*Appends chunks to inputBuffer if space allows.
*Integration in loop():
*
*Continuously reads incoming data using readSerialChunks() and processes complete MQTT messages using processMQTTMessages().
*Buffer Management:
*
*The inputBuffer is reset after processing to ensure no old data is retained.
*This updated example handles large incoming MQTT messages efficiently and prevents buffer overflows. 
*/

#include <mqtt_v5.h>
#include <SoftwareSerial.h>

#define SIM_TX 10
#define SIM_RX 11

SoftwareSerial SIM800(SIM_TX, SIM_RX);

#define MAX_MESSAGE_SIZE 128  // Maximum expected MQTT message size
char inputBuffer[MAX_MESSAGE_SIZE]; // Buffer to store incoming data
volatile uint16_t bufferIndex = 0; // Index to track position in the buffer

#define CHUNK_SIZE 32  // Read in chunks to avoid buffer overflow

void readSerialChunks() {
  while (SIM800.available()) {
    char chunk[CHUNK_SIZE + 1] = {0};  // Temporary buffer
    int len = SIM800.readBytes(chunk, CHUNK_SIZE);
    chunk[len] = '\0'; // Null-terminate

    // Append to the main buffer
    if (bufferIndex + len < MAX_MESSAGE_SIZE) {
      strcat(inputBuffer, chunk);
      bufferIndex += len;
    } else {
      Serial.println("Error: Buffer Overflow in readSerialChunks");
    }
  }
}

extern uint8_t preallocated_mqtt_buffer[];

// Configuration for GPRS and MQTT
const char *apn = "your-apn";  // Replace with your APN
const char *mqtt_broker = "mqtt.example.com";  // Replace with your MQTT broker
const int mqtt_port = 1883;
const char *client_id = "arduino_client";
const char *topic_publish = "test/publish";
const char *topic_subscribe = "test/subscribe";

// Payload to publish
const char *payload = "{\"message\": \"Hello, MQTT V5!\"}";

// Flags
bool gprsReady = false;

void setup() {
  Serial.begin(9600);   // Debugging interface
  SIM800.begin(9600);   // SIM800 communication

  Serial.println("Initializing SIM800...");

  // Ensure SIM800 communication
  sendATCommand("AT");
  sendATCommand("AT+CSQ");  // Check signal strength

  // Configure GPRS
  gprsReady = configureGPRS();
  if (!gprsReady) {
    Serial.println("GPRS configuration failed. Restarting...");
    while (1);
  }

  // Connect to the MQTT broker
  connectToMQTTBroker();

  // Subscribe to a topic
  subscribeToTopic(topic_subscribe);

  // Publish a test message
  publishMessage(topic_publish, payload);
}

void loop() {
  // Publish periodically
  static unsigned long lastPublishTime = 0;
  if (millis() - lastPublishTime > 5000) {
    publishMessage(topic_publish, "{\"message\": \"Periodic Hello, MQTT V5!\"}");
    lastPublishTime = millis();
  }

  // Read incoming serial chunks
  readSerialChunks();

  // Process buffered MQTT messages
  processMQTTMessages();
}

void sendATCommand(const char *command, const char *expectedResponse = "OK", unsigned long timeout = 2000) {
  Serial.print("Sending: ");
  Serial.println(command);

  SIM800.println(command);
  unsigned long start = millis();
  while (millis() - start < timeout) {
    if (SIM800.available()) {
      String response = SIM800.readString();
      Serial.println("Response: " + response);
      if (response.indexOf(expectedResponse) != -1) {
        return;  // Command executed successfully
      }
    }
  }
  Serial.println("Command timeout or failed.");
}

bool configureGPRS() {
  Serial.println("Configuring GPRS...");

  // Set APN
  String command = String("AT+CGDCONT=1,\"IP\",\"") + apn + "\"";
  sendATCommand(command.c_str());

  // Activate PDP context
  sendATCommand("AT+CGACT=1,1");

  // Check GPRS registration
  sendATCommand("AT+CGREG?");
  return true;
}

void connectToMQTTBroker() {
  Serial.println("Connecting to MQTT broker...");

  // Setup MQTT configuration
  mqtt_property connect_properties[1];
  uint8_t session_expiry[4] = {0, 0, 0, 60};  // Session expiry: 60 seconds
  connect_properties[0].property_id = 0x11;
  connect_properties[0].length = 4;
  connect_properties[0].value = session_expiry;

  int result = mqtt_v5_connect_message(preallocated_mqtt_buffer, client_id, connect_properties, 1);
  if (result != 0) {
    Serial.println("Error constructing CONNECT message.");
    return;
  }

  sendMQTTMessage(preallocated_mqtt_buffer, strlen((char *)preallocated_mqtt_buffer));
  Serial.println("CONNECT message sent.");
}

void subscribeToTopic(const char *topic) {
  Serial.print("Subscribing to topic: ");
  Serial.println(topic);

  mqtt_property subscribe_properties[1];
  uint8_t subscription_identifier[2] = {0x00, 0x01};  // Subscription identifier
  subscribe_properties[0].property_id = 0x0B;
  subscribe_properties[0].length = 2;
  subscribe_properties[0].value = subscription_identifier;

  int result = mqtt_v5_subscribe_message(preallocated_mqtt_buffer, topic, 1, subscribe_properties, 1);
  if (result != 0) {
    Serial.println("Error constructing SUBSCRIBE message.");
    return;
  }

  sendMQTTMessage(preallocated_mqtt_buffer, strlen((char *)preallocated_mqtt_buffer));
  Serial.println("SUBSCRIBE message sent.");
}

void publishMessage(const char *topic, const char *message) {
  Serial.print("Publishing message to topic: ");
  Serial.println(topic);

  mqtt_property publish_properties[1];
  uint8_t content_type[] = "application/json";  // Content type property
  publish_properties[0].property_id = 0x03;
  publish_properties[0].length = strlen((char *)content_type);
  publish_properties[0].value = content_type;

  int result = mqtt_v5_publish_message(preallocated_mqtt_buffer, topic, message, publish_properties, 1);
  if (result != 0) {
    Serial.println("Error constructing PUBLISH message.");
    return;
  }

  sendMQTTMessage(preallocated_mqtt_buffer, strlen((char *)preallocated_mqtt_buffer));
  Serial.println("PUBLISH message sent.");
}

void sendMQTTMessage(const uint8_t *message, uint16_t length) {
  SIM800.println("AT+CIPSEND");
  delay(500);
  SIM800.write(message, length);
  SIM800.write((char)26);  // End of message (Ctrl+Z)
}

void processMQTTMessages() {
  if (bufferIndex > 0) {
    Serial.print("Processing MQTT Message: ");
    Serial.println(inputBuffer);

    // Reset the buffer after processing
    memset(inputBuffer, 0, MAX_MESSAGE_SIZE);
    bufferIndex = 0;
  }
}
