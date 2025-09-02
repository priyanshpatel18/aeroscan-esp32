#include <WiFi.h>
#include <WebSocketsClient.h>
#include <HTTPClient.h>
#include "DHT.h"

// Network Credentials
const char *ssid = "<YOUR_WIFI_SSID>";
const char *password = "<YOUR_WIFI_PASSWORD>";

// WebSocket and HTTP server
const char *ws_host = "websocket.aeroscan.site";
const int ws_port = 443; // SSL for wss
const char *ws_path = "/";
const char *http_url = "https://www.aeroscan.site/api/sensor-data";
const char *auth_token = "<AUTH_TOKEN>";

// DHT sensor
#define DHTPIN 4
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

WebSocketsClient webSocket;

// Forward declaration
void sendFallbackHTTP(float temp, float hum);

void webSocketEvent(WStype_t type, uint8_t *payload, size_t length)
{
    switch (type)
    {
    case WStype_DISCONNECTED:
        Serial.println("[WSc] Disconnected from WebSocket server");
        break;
    case WStype_CONNECTED:
        Serial.printf("[WSc] Connected to WebSocket server\n");
        break;
    case WStype_TEXT:
        Serial.printf("[WSc] Received: %s\n", payload);
        break;
    case WStype_ERROR:
        Serial.printf("[WSc] WebSocket Error\n");
        break;
    default:
        break;
    }
}

void setup()
{
    Serial.begin(115200);
    dht.begin();

    // Connect to Wi-Fi
    WiFi.begin(ssid, password);
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }
    Serial.println(" Connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    // Small delay to ensure WiFi is stable
    delay(2000);

    // Setup WebSocket with SSL (wss://)
    Serial.println("Setting up WebSocket connection...");
    webSocket.beginSSL(ws_host, ws_port, String(ws_path) + "?token=" + String(auth_token));
    webSocket.onEvent(webSocketEvent);
    webSocket.setReconnectInterval(5000);

    // Enable heartbeat to keep connection alive
    webSocket.enableHeartbeat(15000, 3000, 2);

    Serial.println("WebSocket setup complete. Connecting...");
}

void loop()
{
    webSocket.loop();

    float temperature = dht.readTemperature();
    float humidity = dht.readHumidity();

    if (isnan(temperature) || isnan(humidity))
    {
        Serial.println("Failed to read from DHT sensor!");
        delay(2000);
        return;
    }

    // JSON payload matches server expectations (Next.js API)
    String payload = "{\"temperature\":" + String(temperature, 2) +
                     ",\"humidity\":" + String(humidity, 2) +
                     ",\"pm25\":null,\"pm10\":null}";

    // Send via WebSocket if connected
    if (webSocket.isConnected())
    {
        webSocket.sendTXT("{\"type\":\"UPDATE_DATA\",\"payload\":" + payload + "}");
        Serial.println("Sent via WebSocket: " + payload);
    }
    else
    {
        Serial.println("WebSocket not connected, using HTTP fallback");
        sendFallbackHTTP(temperature, humidity);
    }

    delay(10000); // Send data every 10 seconds
}

void sendFallbackHTTP(float temp, float hum)
{
    if (WiFi.status() == WL_CONNECTED)
    {
        HTTPClient http;

        http.begin(http_url);
        http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

        http.addHeader("Content-Type", "application/json");
        http.addHeader("Authorization", "Bearer " + String(auth_token));
        http.addHeader("User-Agent", "ESP32-HTTPClient/1.0");

        String httpPayload = "{\"temperature\":" + String(temp, 2) +
                             ",\"humidity\":" + String(hum, 2) +
                             ",\"pm25\":null,\"pm10\":null}";

        Serial.println("Sending HTTP request to: " + String(http_url));
        Serial.println("Payload: " + httpPayload);

        int httpResponseCode = http.POST(httpPayload);

        if (httpResponseCode > 0)
        {
            String response = http.getString();
            Serial.printf("HTTP POST response: %d\n", httpResponseCode);
            Serial.println("Response: " + response);

            if (httpResponseCode == 200)
            {
                Serial.println("✓ HTTP request successful!");
            }
        }
        else
        {
            Serial.printf("HTTP POST failed: %s\n", http.errorToString(httpResponseCode).c_str());
        }

        http.end();
    }
    else
    {
        Serial.println("WiFi not connected, cannot send HTTP fallback");
    }
}
