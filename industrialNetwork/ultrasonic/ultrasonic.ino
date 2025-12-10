#include "Ultrasonic.h"
#include <SPI.h>
#include <mcp2515.h>
#include <math.h>

struct can_frame canTx;
struct can_frame canRx;
struct MCP2515 mcp2515(10); // CS pin is GPIO 5

#define MAX_RETRIES 3
#define CAN_TX_ID  0x701

Ultrasonic ultrasonic(7);

// Simulation parameters
const unsigned long TEMP_PERIOD_MS = 60000UL; // period for sine wave (60s)
unsigned long lastSendMillis = 0;
const unsigned long SEND_INTERVAL_MS = 1000UL; // send every 5 seconds

void setup() {
  Serial.begin(115200);
  delay(10);

  SPI.begin();
  mcp2515.reset();
  mcp2515.setBitrate(CAN_500KBPS, MCP_8MHZ);
  mcp2515.setNormalMode();
}

void loop() {
  unsigned long now = millis();

  if (now - lastSendMillis < SEND_INTERVAL_MS) {
    // nothing to do yet
    return;
  }
  lastSendMillis = now;


  unsigned int waitTime = 100;

  long RangeInInches;
  long RangeInCentimeters;

  int busy = 0;

  // Prepare CAN transmit message
  canTx.can_id  = CAN_TX_ID;
  canTx.can_dlc = 1;


  Serial.println("The distance to obstacles in front is: ");

  RangeInCentimeters = ultrasonic.MeasureInCentimeters(); // two measurements should keep an interval
  Serial.print(RangeInCentimeters);//0~400cm
  Serial.println(" cm");

  busy =  RangeInCentimeters < 100 ? 1:0;
  canTx.data[0] = (byte)busy;
  delay(250);


  if (mcp2515.sendMessage(&canTx) == MCP2515::ERROR_OK) {
      Serial.print("[CAN] Sent occupation: ");
      Serial.println(busy ? "Ocupado" : "Libre");

  }else{
    Serial.print("[CAN] Error sending message in sensor ");
    Serial.println(CAN_TX_ID);
  }

}
