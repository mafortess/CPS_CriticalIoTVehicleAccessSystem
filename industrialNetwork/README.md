### Servo
Simulating barrier. Barrier will constantly send their state, which along with the identifier will provide a complete understanding of barrier(s) state.
The communication will require two IDs, an ID for the barrier to receive orders to open or close, and an ID for the barrier to communicate its current state.
- The orders will be received from IDs 0x200-0x299
- The servomotor state will be sent through IDs 0x300-0x399
- For any servomotor the output ID = (input ID + 0x100).
- E.g. a servomotor that receives orders from ID 0x221 will also send its state through ID 0x321.

## Sensor and actuators
For each sensor measurement data sent over the CAN. We should construct a specification for its corresponding translation. For example, for temperature data:
- Temperature sensor IDs will be in the range 0x400-0x499
- 2 Bytes used
- Ranges from 0ºC to 99.99ºC
- Only 2 decimals for ºC
- Conversion factor will be as follows: (TEMP x 100)
- Then converted into hex to send over CAN using 2 bytes
- So, 85.31ºC -> 8531 -> 0x2152 -> MSB=21, LSB=52
- CAN message will be: 0x423#022152 for sensor with ID 0x423

### Air quality 
Needs no ACK, just sends measures.
- Air quality sensor IDs will be in the range 0x500-0x599
- 2 Bytes used
- Value ranges from 0 to 4096 (decimal)
- The converted into hex to send over CAN using 2 bytes
- So, for a value of 350 -> 0x15E -> MSB=0x01, LSB=0x53
- CAN message will be: 0x515#020153 for air quality sensor with ID 0x515

### Gas 
Needs no ACK, just sends measures.
- Gas sensor IDs will be in the range 0x600-0x699
- 2 Bytes used
- Ranges from 0V to 5.0V
- Only 2 decimals for Volts
- Conversion factor will be as follows: (VOLTS x 100)
- Then converted into hex to send over CAN using 2 bytes
- So, for a value of 3.45 -> 345 -> 0x159 -> MSB=0x01, LSB=0x59
- CAN message will be: 0x633#020159 for gas sensor with ID 0x633

### Ultrasound
Sensor for detecting presence in the parking spot. Will have a led attached to indicate locally whether the spot is busy or not. Independently the data will be sent over CAN, Needs no ACK, just send measures.
- Ultrasound sensor IDs will be in the range 0x700-0x799


