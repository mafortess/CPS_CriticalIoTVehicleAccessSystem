#include <SPI.h>
//#include <mcp_can.h>
#include <mcp2515.h>
#include "Air_Quality_Sensor.h"
#include <ESP32Servo.h>


#define CAN_CS 5  // Chip Select pin
struct MCP2515 mcp2515(5); // CS pin is GPIO 5
#define SERVO_PIN 26 // ESP32 pin GPIO26 connected to servo motor

Servo servoMotor;

#define SERVO_ID_IN 0x201 // Will always receive 0 or 1
#define SERVO_ID_IN_BUTTON 0x321 // Will always receive 0 or 1
struct can_frame canMsgReceived;

#define SERVO_ID_OUT 0x301 // Will send barrier degrees
struct can_frame canMsgSent;

#define BARRIER_TIMEOUT_MS 5000

#define LOOP_DELAY 500

int lastOrderTimestamp = 0;
int verbose = 1;

int servoAngle = 0;
int servoOrder = 0;


void setup() {
  Serial.begin(115200);

  SPI.begin();

  mcp2515.reset();
  mcp2515.setBitrate(CAN_500KBPS, MCP_8MHZ);
  mcp2515.setNormalMode();

  servoMotor.attach(SERVO_PIN);  // attaches the servo on ESP32 pin
}

void loop() {
  

  if (millis() - lastOrderTimestamp > BARRIER_TIMEOUT_MS) servoOrder = 0;

  if (mcp2515.readMessage(&canMsgReceived) == MCP2515::ERROR_OK)
  {
    if (canMsgReceived.can_id == SERVO_ID_IN || canMsgReceived.can_id == SERVO_ID_IN_BUTTON)  // Check if the message is from the sender
    {
      
      if (canMsgReceived.data[0] == 0) {
        servoOrder = 0;
      }
      else {
        servoOrder = 1;
        lastOrderTimestamp = millis();
      }

      if (verbose) {
        Serial.print("Message received: ");
        Serial.println(canMsgReceived.data[0]);
        Serial.print("Order received: ");
        Serial.print(servoOrder ? "OPEN" : "CLOSE");
        Serial.println(" BARRIER");
      } 
 
    }
  }

  servoAngle = servoOrder ? 90 : 0;
  servoMotor.write(servoAngle);
  if (verbose) {
    Serial.print("Data sent to motor: servoAngle = ");
    Serial.print(servoAngle);
    Serial.println(" degrees.");
  }

  // Prepare CAN transmit message
  canMsgSent.can_id  = SERVO_ID_OUT;
  canMsgSent.can_dlc = 1;
  canMsgSent.data[0] = (byte) (servoOrder & 0xFF);        // LSB

  if (mcp2515.sendMessage(&canMsgSent) == MCP2515::ERROR_OK) {
      Serial.print("[CAN] Sent degrees: ");
      Serial.print(servoOrder, 1);
      Serial.println(" degrees.");

  }else{
    Serial.print("[CAN] Error sending message in sensor ");
    Serial.println(SERVO_ID_OUT);
  }
  delay(LOOP_DELAY);
}
