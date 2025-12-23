#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include <EloquentSurveillance.h>
#include "TelegramChat.h"
#include <base64.h>
#include <WiFiClientSecure.h>

// WiFi Settings
#define WIFI_SSID "ESP32_AP"
#define WIFI_PASS "mah-iot2"

// Telegram Settings
#define BOT_TOKEN <YOUR_BOT_TOKEN>
#define CHAT_ID <YOUR_CHAT_ID>

// Flash LED PIN
#define FLASH_PIN 4

// Webserver object
WebServer server(80);

WiFiClientSecure secureClient;
PubSubClient mqtt_client(secureClient);

// Motion detection
EloquentSurveillance::Motion motion;

// Telegram chat initialization
EloquentSurveillance::TelegramChat chat(BOT_TOKEN, CHAT_ID);

// MQTT Client
//#define MQTT_SERVER "192.168.50.1"
// #define MQTT_SERVER "192.168.1.133"
#define MQTT_SERVER "192.168.70.1"
#define MQTT_USER "mqtt_cps_esp32"
#define MQTT_PASS "mah_cps_esp32"

String DEVICE_ID;

// WiFiClient wClient;
// PubSubClient mqtt_client(wClient);

String TOPIC_STATUS;
String TOPIC_SEND_PICTURE;
String TOPIC_SERVER_RESPONSE;
String TOPIC_NOTIFY_PICTURE = "picture/notify";
String TOPIC_NOTIFY_MOTION_DETECTED = "motion/notify";

const char* root_ca PROGMEM = R"EOF(
  -----BEGIN CERTIFICATE-----
MIIFmzCCA4OgAwIBAgIUWk1VEYivDH+W7bPRsWQ7F7oEeQowDQYJKoZIhvcNAQEL
BQAwXTELMAkGA1UEBhMCRVMxDzANBgNVBAgMBk1hbGFnYTEMMAoGA1UEBwwDVU1B
MRAwDgYDVQQKDAdJb1QtTGFiMQswCQYDVQQLDAJDQTEQMA4GA1UEAwwHaW90Mi1j
YTAeFw0yNTEyMTUwMDQxMDRaFw0yNjEyMTUwMDQxMDRaMF0xCzAJBgNVBAYTAkVT
MQ8wDQYDVQQIDAZNYWxhZ2ExDDAKBgNVBAcMA1VNQTEQMA4GA1UECgwHSW9ULUxh
YjELMAkGA1UECwwCQ0ExEDAOBgNVBAMMB2lvdDItY2EwggIiMA0GCSqGSIb3DQEB
AQUAA4ICDwAwggIKAoICAQC23v5ajTcTIa3RKZlLV00z54X5lw8qu+9vo2f84UB1
YHxNMY73iO54bwbP38D4ye3bpeD4aBcAfyZ5Ju+njabNRn8SdmjONwJnSsbSHO+q
5RXt6W6y+j7dOi+G0n4z4kqTmtQx3KQiaTpjjkYC0lZ+UX5SD2ITdv10Os6s7bDK
wKn7jm2PYf1UWv3g2p7O/Xb2SCiajgOOScMKCHXC7PWT9AZ8CN1j/BGs1MFFj0rU
HGotrRWqlle9P3NpgkxMIAvku3j/DEQBxHSdaTn5U45DejBf/sRYnF0HgA6kq5Su
F/gOUKwSJx6JSHnWtsj9d6WhglfTdN9svsJZdmavtP+fdSjkmICGSfoUobB4C3ra
tdWgIrl7OkRU/kagRF5ww8ppzn9XI7eCg/Mn8gENu+j2teg3AlxhszYJHDpFmxmA
A2hKngih2wmtrceC9DoVdwll7MZ9VjTErkgwG5B91TGlAHNhmoOI0w9P0D18H207
eOFjqAXu6iqEIWCtfEgGHZrmhcc6WX96c4/NZq6H1aicnI7yDfAZylcGGynfMGgH
McFfVCeI6ewMeDcqP7W5X8mbVKQwhukZqqKl28SBYTadMjkmaYmFA0V9A3DvzN/A
1dU3Ljp9hj2zw1S5N2T7h3qQ7UE1f24heMbt0SRGWzHF2P2sT6FGO4Sns9yWLW8V
BwIDAQABo1MwUTAdBgNVHQ4EFgQULKli11dONEz6SCIlcYPklzzAcqAwHwYDVR0j
BBgwFoAULKli11dONEz6SCIlcYPklzzAcqAwDwYDVR0TAQH/BAUwAwEB/zANBgkq
hkiG9w0BAQsFAAOCAgEAgLiwInIDWMf78mE6CfzrRyfy4CNAfyDdmhpBvVKo7OS8
CsjOEGEPUy9rr/cFl0aEyZvOYqhrNDVr4vE+e6vOWHRkEHR73fDkmDS/LznlDVF7
Re/AI9kbgPMzcc9VIetciQQ+Id41CTwnowasFM9xgRVJNZMdyalB4tB1mgVc9Hwl
2f+VqmXH8SpvSrPfxKw6JARjqFthb1SR2dfeI41XANrEG4sKodO5Z7xnl6MH4yOH
5C5nyOVMM6bIxOnOcbnUiPIv9sWBiMwsDisIaVG91cEN/eHIViUiqbKFD1w1uHPD
CDbiYvPZC9Xo8ZHhmXBBFiXMaAgIzBmQLhbeFFXjztb5MueVC+ad+0a1bGXHIHNr
0Oh8Aa26zAELJU0vKoX14Rd/xdgjGPbOfW6aCsxvOousg8pyIYw56zbrsx01g0FZ
BXY3A7OoBtlgcSkwE9jL5fiJJPq7V+1RWvdMijTxag54mv05DuuHE3ItO/LxrUbb
QcWXhSit+M2E7FxAlaxvO9Oip50dQ2GfaXAB1wMCbSmWqCSUCTBKFfAQvlIJbvsl
+KxcvxrpAxlPfAMlv//czizUm3WJ44tJxFd5B2uY5vFmITytM+36rC5uNYzFpM8b
YwB0Tjb4nUVZc/qypQgOkZqYcQThn9ILUAfcoBlGSiWI+ebULj3PwETaScA+wYk=
-----END CERTIFICATE-----
)EOF";

void debug(String level, String message)
{
  Serial.print("[" + level + "] ");
  Serial.println(message);
}

void start_wifi_connection()
{
  debug("INFO", "Connecting to WiFi " + String(WIFI_SSID));

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  WiFi.setSleep(false);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(200);
    Serial.print(".");
  }

  Serial.println("");
  debug("SUCCESS", "WiFi connected!");
  Serial.print("Dirección IP: ");
  Serial.println(WiFi.localIP());
}

void start_mqtt_connection()
{
  while (!mqtt_client.connected())
  {
    debug("INFO", "Attempting MQTT connection...");

    // Last Will
    JsonDocument lwtDoc;
    lwtDoc["status"] = "offline";
    String lwtPayload;
    serializeJson(lwtDoc, lwtPayload);

    if (mqtt_client.connect(DEVICE_ID.c_str(), MQTT_USER, MQTT_PASS, TOPIC_STATUS.c_str(), 1, true, lwtPayload.c_str()))
    {
      debug("INFO", "Connected to broker " + String(MQTT_SERVER));

      // Publishing device status
      JsonDocument doc;
      doc["status"] = "online";
      String jsonPayload;
      serializeJson(doc, jsonPayload);

      mqtt_client.publish(TOPIC_STATUS.c_str(), jsonPayload.c_str(), false);
      debug("INFO", "Published status: " + jsonPayload);

      // Suscribing to topic
      mqtt_client.subscribe(TOPIC_SERVER_RESPONSE.c_str());
    }
    else
    {
      debug("ERROR", String(mqtt_client.state()) + " reintento en 5s");
      delay(5000);
    }
  }
}

void mqtt_handle_message(char *topic, byte *payload, unsigned int length)
{
  String msg = "";
  for (int i = 0; i < length; i++) msg += (char)payload[i];
  debug("Received message", msg);

  JsonDocument json;

  if (String(topic) == TOPIC_SERVER_RESPONSE) {
    DeserializationError error = deserializeJson(json, msg);
    
    if (error) 
      debug("ERROR", String(error.c_str()));
    else if (json.containsKey("access_granted") && json.containsKey("plate_number")) {
      bool open_gate = bool(json["access_granted"]);
      String plate_number = String(json["plate_number"]);

      if (open_gate) {
        debug("INFO", "Access granted to " + plate_number); 
        digitalWrite(FLASH_PIN, HIGH);
        delay(500);
        digitalWrite(FLASH_PIN, LOW);
      } else {
        debug("INFO", "Access denied");
      }
    }
  }
}

void sendPictureOverMQTT()
{
  if (!camera.capture())
  {
    debug("ERROR", camera.getErrorMessage());
    return;
  }

  // Obtiene el buffer de la imagen
  uint8_t *buffer = camera.getBuffer();
  size_t bufferSize = camera.getFileSize();

  if (!buffer || bufferSize == 0)
  {
    debug("ERROR", "No se pudo obtener buffer de imagen");
    return;
  }

  debug("INFO", "Imagen capturada correctamente, codificando en Base64...");

  // Codifica la imagen en Base64
  String encodedImage = base64::encode(buffer, bufferSize);

  // Elimina el recorte para enviar toda la imagen
  size_t maxSize = 48000;
  if (encodedImage.length() > maxSize)
  {
    debug("WARN", "Imagen demasiado grande, recortando para enviar por MQTT");
    encodedImage = encodedImage.substring(0, maxSize);
  }

  // Prepara el JSON con ArduinoJson
  JsonDocument doc; // Ajusta el tamaño según la longitud de tu imagen Base64
  doc["image"] = encodedImage;

  String payloadJson;
  serializeJson(doc, payloadJson);

  // Publica la notificación de imagen tomada
  mqtt_client.publish(TOPIC_NOTIFY_PICTURE.c_str(), "Image taken!");
  mqtt_client.publish(TOPIC_NOTIFY_MOTION_DETECTED.c_str(), "Motion detected!", false);

  // Envía el payload JSON con la imagen en Base64
  debug("INFO", "Sending image through MQTT " + String(TOPIC_SEND_PICTURE));
  debug("INFO", payloadJson.c_str());
  mqtt_client.publish(TOPIC_SEND_PICTURE.c_str(), payloadJson.c_str(), false);

  debug("INFO", "Imagen enviada por MQTT en formato JSON correctamente.");
}

void startWebServer()
{
  server.on("/", HTTP_GET, []()
            { server.send(200, "text/html",
                          "<html><body><h1>ESP32-CAM</h1>"
                          "<p>Usa <a href='/capture'>/capture</a> para capturar manualmente una imagen.</p>"
                          "</body></html>"); });

  server.on("/capture", HTTP_GET, []()
            {
    debug("INFO", "Petición manual de captura recibida");
    captureAndSendImage(); });

  server.begin();
  debug("SUCCESS", "Servidor web iniciado en puerto 80");
}

void captureAndSendImage()
{
  digitalWrite(FLASH_PIN, HIGH);
  delay(100);

  if (!camera.capture())
  {
    debug("ERROR", camera.getErrorMessage());
    digitalWrite(FLASH_PIN, LOW);
    server.send(500, "text/plain", "Error al capturar la imagen");
    return;
  }

  digitalWrite(FLASH_PIN, LOW);

  uint8_t *buffer = camera.getBuffer();
  size_t bufferSize = camera.getFileSize();

  if (!buffer || bufferSize == 0)
  {
    debug("ERROR", "No se pudo obtener buffer de imagen");
    server.send(500, "text/plain", "Error al obtener el buffer");
    return;
  }

  server.sendHeader("Content-Disposition", "attachment; filename=\"captura.jpg\"");
  server.send_P(200, "image/jpeg", (const char *)buffer, bufferSize);

  debug("INFO", "Imagen enviada correctamente");
}

void scanNetworks()
{
  debug("INFO", "Escaneando redes WiFi disponibles...");

  int n = WiFi.scanNetworks();
  if (n == 0)
  {
    debug("INFO", "No se encontraron redes WiFi");
  }
  else
  {
    debug("INFO", String(n) + " redes encontradas:");
    for (int i = 0; i < n; ++i)
    {
      String ssid = WiFi.SSID(i);
      int rssi = WiFi.RSSI(i);
      String encryptionType;
      switch (WiFi.encryptionType(i))
      {
      case WIFI_AUTH_OPEN:
        encryptionType = "Open";
        break;
      case WIFI_AUTH_WEP:
        encryptionType = "WEP";
        break;
      case WIFI_AUTH_WPA_PSK:
        encryptionType = "WPA/PSK";
        break;
      case WIFI_AUTH_WPA2_PSK:
        encryptionType = "WPA2/PSK";
        break;
      case WIFI_AUTH_WPA_WPA2_PSK:
        encryptionType = "WPA/WPA2/PSK";
        break;
      default:
        encryptionType = "Unknown";
        break;
      }

      debug("INFO", String(i + 1) + ": " + ssid + " (" + rssi + "dBm) [" + encryptionType + "]");
      delay(10);
    }
  }
}

void build_topics(String device_mac) {
  TOPIC_STATUS = "gate/" + device_mac + "/status";
  TOPIC_SEND_PICTURE = "gate/" + device_mac + "/access";
  TOPIC_SERVER_RESPONSE = "server/response/" + device_mac;
}

void setup()
{
  Serial.begin(115200);
  delay(3000);

  debug("INFO", "Starting setup...");

  // Configura el pin del flash
  pinMode(FLASH_PIN, OUTPUT);
  digitalWrite(FLASH_PIN, LOW);

  // Camera Settings
  camera.aithinker();
  camera.qvga();        // VGA Resolution
  camera.lowQuality(); // Best Quality JPEG
  camera.setBrightness(1);
  camera.setSaturation(0);

  // Camera initilization
  while (!camera.begin())
  {
    debug("ERROR", camera.getErrorMessage());
    delay(1000);
  }

  // Motion detection settings
  motion.setMinChanges(0.1); // Sensibility
  motion.setMinPixelDiff(10);
  motion.setMinSizeDiff(0.05);

  // scanNetworks();

  start_wifi_connection();
  
  DEVICE_ID = String(WiFi.getHostname());
  String device_mac = String(WiFi.macAddress());
  build_topics(device_mac);
  
  // TLS
  secureClient.setCACert(root_ca);

  mqtt_client.setServer(MQTT_SERVER, 8883);
  mqtt_client.setBufferSize(51200);
  mqtt_client.setCallback(mqtt_handle_message);
  start_mqtt_connection();

  startWebServer();
}

void loop() {
  if (!mqtt_client.connected()) {
    start_mqtt_connection();
  }
  mqtt_client.loop();
  server.handleClient();

  yield(); // evita que el watchdog resetee

  if (!camera.capture()) {
    debug("ERROR", camera.getErrorMessage());
    delay(100);
    return;
  }

  if (!motion.update()) {
    delay(10); // reduce carga
    return;
  }

  if (motion.detect()) {
    debug("INFO", "Motion detected!");

    sendPictureOverMQTT();

    delay(100); 

    if (camera.capture())
    {
      debug("INFO", "Image taken successfully.");

      // bool messageResponse = chat.sendMessage("Motion Detected!");
      // debug("TELEGRAM MSG", messageResponse ? "OK" : "ERR");

      // bool photoResponse = chat.sendPhoto();
      // debug("TELEGRAM PHOTO", photoResponse ? "OK" : "ERR");
    }
  } else if (!motion.isOk()) {
    debug("ERROR", motion.getErrorMessage());
  }

  delay(10); // para que el watchdog no se dispare
}
