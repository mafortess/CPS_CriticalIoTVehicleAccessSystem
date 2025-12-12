#include <SPI.h>
//#include <mcp_can.h>
#include <mcp2515.h>
#include "Air_Quality_Sensor.h"

#define CAN_CS 5  // Chip Select pin
struct can_frame canTx;
struct MCP2515 mcp2515(5); // CS pin is GPIO 5

#define GAS_SENSOR_PIN 34  // Gas sensor pin
#define AIR_QUALITY_PIN 35  // Air Quality sensor pin
#define GAS_SENSOR_ID 0x601
#define AIR_QUALITY_SENSOR_ID 0x501

#define LOOP_DELAY 1000

AirQualitySensor aq_sensor(AIR_QUALITY_PIN);

void setup() {
  Serial.begin(115200);

  SPI.begin();

  mcp2515.reset();
  mcp2515.setBitrate(CAN_500KBPS, MCP_8MHZ);
  mcp2515.setNormalMode();

  
  // Initializing Air Quality Sensor
  Serial.println("Waiting for air quality sensor to init...");
  // delay(20000); // we'll comment that for now
  Serial.println("Waiting sensor to init...");
  delay(10000);

  if (aq_sensor.init()) {
      Serial.println("Sensor ready.");
  } else {
      Serial.println("Sensor ERROR!");
  }

  Serial.println("Air sensor ready.");

}

void loop() {
  int gasSensorValue;
  int gasSensorVolt;
  int verbose = 1;
  byte sndStat;


  // Gas sensor reading
  gasSensorValue = analogRead(GAS_SENSOR_PIN);
  gasSensorVolt = (int) (gasSensorValue/4096.0 * 5.0 * 100.0); 
  
  delay(250);

  // Air quality sensor reading
  int quality = aq_sensor.slope();
  int airQualitySensorValue = aq_sensor.getValue();
  if (verbose){
    Serial.print("Sensor value: ");
    Serial.println(airQualitySensorValue);
    if (quality == AirQualitySensor::FORCE_SIGNAL) {
        Serial.println("High pollution! Force signal active.");
    } else if (quality == AirQualitySensor::HIGH_POLLUTION) {
        Serial.println("High pollution!");
    } else if (quality == AirQualitySensor::LOW_POLLUTION) {
        Serial.println("Low pollution!");
    } else if (quality == AirQualitySensor::FRESH_AIR) {
        Serial.println("Fresh air.");
    }
  }

  delay(250);


  // Prepare CAN transmit message from gas sensor
  canTx.can_id  = GAS_SENSOR_ID;
  canTx.can_dlc = 2;
  canTx.data[0] = (byte) ((gasSensorVolt >> 8) & 0xFF); // MSB
  canTx.data[1] = (byte) (gasSensorVolt & 0xFF);        // LSB

  if (mcp2515.sendMessage(&canTx) == MCP2515::ERROR_OK) {
      Serial.print("[CAN] Sent gas sensor reading: ");
      Serial.println(gasSensorVolt);
  }else{
    Serial.print("[CAN] Error sending message in gas sensor ");
    Serial.println(GAS_SENSOR_ID);
  }

  delay(250);

  // Prepare CAN transmit message from air quality sensor
  canTx.can_id  = AIR_QUALITY_SENSOR_ID;
  canTx.can_dlc = 2;
  canTx.data[0] = (byte) ((airQualitySensorValue >> 8) & 0xFF); // MSB
  canTx.data[1] = (byte) (airQualitySensorValue & 0xFF);        // LSB

  if (mcp2515.sendMessage(&canTx) == MCP2515::ERROR_OK) {
      Serial.print("[CAN] Sent air quality: ");
      Serial.println(airQualitySensorValue);

  }else{
    Serial.print("[CAN] Error sending message in air quality sensor ");
    Serial.println(AIR_QUALITY_SENSOR_ID);
  }

  delay(LOOP_DELAY);
}
