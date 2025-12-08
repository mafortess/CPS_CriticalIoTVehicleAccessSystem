## Sensor and actuators
For each sensor measurement data sent over the CAN. We should construct a specification for its corresponding translation. For example, for temperature data:
- Temperature sensor IDs will be in the range 400-499
- 2 Bytes used
- Ranges from 0ºC to 99.99ºC
- Only 2 decimals for ºC
- Conversion factor will be as follows: (TEMP x 100)
- Then converted into hex to send over CAN using 2 bytes
- So, 85.31ºC -> 8531 -> 0x2152 -> MSB=21, LSB=52
- CAN message will be: 423#022152 for sensor with ID 423

### Air quality 
Needs no ACK, just sends measures.
- Air quality sensor IDs will be in the range 500-599
- 2 Bytes used
- Value ranges from 0 to 4096 (decimal)
- The converted into hex to send over CAN using 2 bytes
- So, for a value of 350 -> 0x15E -> MSB=0x01, LSB=0x53
- CAN message will be: 515#020153 for air quality sensor with ID 515

### Gas 
Needs no ACK, just sends measures.
- Air quality sensor IDs will be in the range 600-699
- 2 Bytes used
- Value ranges from 0 to 4096 (decimal)
- The converted into hex to send over CAN using 2 bytes
- So, for a value of 123 -> 0x007B -> MSB=0x00, LSB=0x7B
- CAN message will be: 633#02007B for gas sensor with ID 633

### Ultrasound
Sensor for detecting presence in the parking spot. Will have a led attached to indicate locally whether the spot is busy or not. Independently the data will be sent over CAN, Needs no ACK, just send measures.
- Air quality sensor IDs will be in the range 700-799


### Servo
Simulating barrier. Barrier will constantly send their state, which along with the identifier will provide a complete understanding of barrier(s) state.
The communication will require two IDs, an ID for the barrier to receive orders to open or close, and an ID for the barrier to communicate its current state.
- The orders will be received from IDs 800-899
- The servomotor state will be sent through IDs 900-999
- For any servomotor the output ID = (input ID + 100).
- E.g. a servomotor that receives orders from ID 850 will also send its state through ID 950.
