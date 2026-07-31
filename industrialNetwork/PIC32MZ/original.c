/*******************************************************************************
  Main Source File

  Company:
    Microchip Technology Inc.

  File Name:
    main_pic32mzda.c

  Summary:
    This file contains the "main" function for a project.

  Description:
    This file contains the "main" function for a project, interrupt handler
    callback functions and Temperature conversion function. The "main"
    function calls the "SYS_Initialize" function to initialize all peripherals
    utilized in this project, calls the different interrupt handler callback
    initializations and the "main" function have the application code to
    toggles an LED on a timeout basis and to print the LED toggling rate
    (or current temperature value if a I/01 Xplained Pro Extension Kit board
    is connected) on the serial terminal.
*******************************************************************************/

/*******************************************************************************
* Copyright (C) 2019 Microchip Technology Inc. and its subsidiaries.
*
* Subject to your compliance with these terms, you may use Microchip software
* and any derivatives exclusively with Microchip products. It is your
* responsibility to comply with third party license terms applicable to your
* use of third party software (including open source software) that may
* accompany Microchip software.
*
* THIS SOFTWARE IS SUPPLIED BY MICROCHIP "AS IS". NO WARRANTIES, WHETHER
* EXPRESS, IMPLIED OR STATUTORY, APPLY TO THIS SOFTWARE, INCLUDING ANY IMPLIED
* WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY, AND FITNESS FOR A
* PARTICULAR PURPOSE.
*
* IN NO EVENT WILL MICROCHIP BE LIABLE FOR ANY INDIRECT, SPECIAL, PUNITIVE,
* INCIDENTAL OR CONSEQUENTIAL LOSS, DAMAGE, COST OR EXPENSE OF ANY KIND
* WHATSOEVER RELATED TO THE SOFTWARE, HOWEVER CAUSED, EVEN IF MICROCHIP HAS
* BEEN ADVISED OF THE POSSIBILITY OR THE DAMAGES ARE FORESEEABLE. TO THE
* FULLEST EXTENT ALLOWED BY LAW, MICROCHIP'S TOTAL LIABILITY ON ALL CLAIMS IN
* ANY WAY RELATED TO THIS SOFTWARE WILL NOT EXCEED THE AMOUNT OF FEES, IF ANY,
* THAT YOU HAVE PAID DIRECTLY TO MICROCHIP FOR THIS SOFTWARE.
*******************************************************************************/

// *****************************************************************************
// *****************************************************************************
// Section: Included Files
// *****************************************************************************
// *****************************************************************************

#include <stdio.h>
#include <stddef.h>                     // Defines NULL
#include <stdbool.h>                    // Defines true
#include <stdlib.h>                     // Defines EXIT_FAILURE
#include <string.h>
#include "definitions.h"                // SYS function prototypes
#include "device_cache.h"

#include "sensors.h"


#define TEMP_SENSOR_SLAVE_ADDR                  0x18
#define TEMP_SENSOR_REG_ADDR                    0x05

#define SWITCH_PRESSED_STATE                    0   // Active LOW switch

/* Timer Counter Time period match values for input clock of 4096 Hz */
#define PERIOD_500MS                            2048
#define PERIOD_1S                               4096
#define PERIOD_2S                               8192
#define PERIOD_4S                               16384

// *****************************************************************************
// *****************************************************************************
// Section: Globals
// *****************************************************************************
// *****************************************************************************
static volatile bool isTmr1Expired = false;
static volatile bool isTemperatureRead = false;

/* New high-level flags */
static volatile bool listenMode = false;             // toggled by SW1 - when true we print incoming CAN msgs
static volatile bool sendTemperatureRequest = false; // set by SW2 to request a single temp read + CAN send

static uint8_t temperatureVal;
static uint8_t i2cWrData = TEMP_SENSOR_REG_ADDR;
static uint8_t i2cRdData[2] = {0};
static uint8_t __attribute__ ((aligned (16))) uartTxBuffer[256] = {0};

typedef enum
{
    TEMP_SAMPLING_RATE_500MS = 0,
    TEMP_SAMPLING_RATE_1S = 1,
    TEMP_SAMPLING_RATE_2S = 2,
    TEMP_SAMPLING_RATE_4S = 3,
} TEMP_SAMPLING_RATE;
static TEMP_SAMPLING_RATE tempSampleRate = TEMP_SAMPLING_RATE_500MS;
static const char timeouts[4][20] = {"500 milliSeconds", "1 Second",  "2 Seconds",  "4 Seconds"};


uint8_t TxfifoQueue = 0;
uint8_t RxfifoQueue = 1;

uint8_t TxBufferLen = 8;
uint8_t TxBuffer[8];

uint8_t RxBuffer[8];
uint8_t RxBufferLen;
uint32_t RxMessageID = 0;
uint16_t RxTimestamp = 0;
CAN_MSG_RX_ATTRIBUTE RxAttr;

bool tx_status = false;
bool rx_status = false;

bool deviceResetRequested = false;

const IdRange idRanges[] = {
    { RANGE_TEMP_START, RANGE_TEMP_END },
    { RANGE_AIR_QUALITY_START, RANGE_AIR_QUALITY_END },
    { RANGE_GAS_START, RANGE_GAS_END },
    { RANGE_OCCUPANCY_START, RANGE_OCCUPANCY_END },
    { RANGE_BARRIER_STATE_START, RANGE_BARRIER_STATE_END },
    { RANGE_BARRIER_COMMAND_START, RANGE_BARRIER_COMMAND_END },
};
const size_t idRangesCount = sizeof idRanges / sizeof idRanges[0];

BaselinePattern baselines[100];  // Support 100 sensor types
uint8_t baseline_count = 0;



static void SW1_User_Handler(GPIO_PIN pin, uintptr_t context);
static void SW2_User_Handler(GPIO_PIN pin, uintptr_t context);
static void SW3_User_Handler(GPIO_PIN pin, uintptr_t context);
static void tmr1EventHandler (uint32_t intCause, uintptr_t context);
static void i2cEventHandler(uintptr_t contextHandle);
static void UARTDmaChannelHandler(DMAC_TRANSFER_EVENT event, uintptr_t contextHandle);
static void MCP9808TempSensorInit(void);
static uint8_t getTemperature(uint8_t* rawTempValue);

// *****************************************************************************
// *****************************************************************************
// Section: Handlers & helper functions
// *****************************************************************************
// *****************************************************************************

/* Keep ISR handlers short: only change flags and (optionally) quickly enqueue a UART message */
static void SW1_User_Handler(GPIO_PIN pin, uintptr_t context)
{
    if(SW1_Get() == SWITCH_PRESSED_STATE)
    {
        /* Toggle listen mode: main loop will poll CAN RX and print messages */
        listenMode = !listenMode;

        if (listenMode)
        {
            sprintf((char*)uartTxBuffer, "Listen mode ENABLED: printing incoming CAN messages\r\n");
        }
        else
        {
            sprintf((char*)uartTxBuffer, "Listen mode DISABLED\r\n");
        }
        DCACHE_CLEAN_BY_ADDR((uint32_t)uartTxBuffer, sizeof(uartTxBuffer));
        DMAC_ChannelTransfer(DMAC_CHANNEL_0, (const void *)uartTxBuffer,
                             strlen((const char*)uartTxBuffer),
                             (const void *)&U4TXREG, 1, 1);
    }
}

static void SW2_User_Handler(GPIO_PIN pin, uintptr_t context)
{
    if(SW2_Get() == SWITCH_PRESSED_STATE)
    {
        /* Request a single temperature read and CAN transmit ? handled in main loop */
        sendTemperatureRequest = true;
    }
}

static void SW3_User_Handler(GPIO_PIN pin, uintptr_t context)
{
    if (SW3_Get() == SWITCH_PRESSED_STATE)
    {
        deviceResetRequested = true;

        sprintf((char*)uartTxBuffer, "SW3 pressed, requesting soft reset\r\n");
        DCACHE_CLEAN_BY_ADDR((uint32_t)uartTxBuffer, sizeof(uartTxBuffer));
        DMAC_ChannelTransfer(DMAC_CHANNEL_0, (const void *)uartTxBuffer,
                             strlen((const char*)uartTxBuffer),
                             (const void *)&U4TXREG, 1, 1);
    }
}

static void tmr1EventHandler (uint32_t intCause, uintptr_t context)
{
    (void)intCause;
    (void)context;
    isTmr1Expired = true;
}

static void i2cEventHandler(uintptr_t contextHandle)
{
    (void)contextHandle;
    if (I2C1_ErrorGet() == I2C_ERROR_NONE)
    {
        isTemperatureRead = true;
    }
}

static void UARTDmaChannelHandler(DMAC_TRANSFER_EVENT event, uintptr_t contextHandle)
{
    (void)contextHandle;
    if (event == DMAC_TRANSFER_EVENT_COMPLETE)
    {
        /* We don't use this flag strictly here, but keep handler for completeness */
    }
}

/* MCP9808 initialization (same as earlier) */
static void MCP9808TempSensorInit(void)
{
    uint8_t config[3] = {0};
    config[0] = 0x01;
    config[1] = 0x00;
    config[2] = 0x00;

    I2C1_Write(TEMP_SENSOR_SLAVE_ADDR, config, 3);

    while (isTemperatureRead != true);
    isTemperatureRead = false;

    config[0] = 0x08;
    config[1] = 0x03;
    I2C1_Write(TEMP_SENSOR_SLAVE_ADDR, config, 2);

    while (isTemperatureRead != true);
    isTemperatureRead = false;
}

/* Converts MCP9808 raw 2-byte read to Fahrenheit truncated to uint8_t (same as earlier) */
static uint8_t getTemperature(uint8_t* rawTempValue)
{
    int temp = ((rawTempValue[0] & 0x1F) * 256 + rawTempValue[1]);
    if(temp > 4095)
    {
        temp -= 8192;
    }
    float cTemp = temp * 0.0625f;
    float fTemp = cTemp * 1.8f + 32.0f;
    return (uint8_t)fTemp;
}

/* Check if given identifier is within defined ranges acting as firewall    */
bool id_in_ranges(uint32_t ident)
{
    for (size_t i = 0; i < idRangesCount; i++) {
        if (ident >= idRanges[i].start && ident <= idRanges[i].end) {
            return true;
        }
    }
    return false;
}


// Initialize baseline from normal traffic (run during learning phase)
void learn_baseline(CANMessage *msg) {
    for (int i = 0; i < baseline_count; i++) {
        if (baselines[i].can_id == msg->can_id) {
            // Update existing baseline
            baselines[i].dlc = msg->dlc;
            return;
        }
    }
    // Add new baseline
    if (baseline_count < 100) {
        baselines[baseline_count].can_id = msg->can_id;
        baselines[baseline_count].dlc = msg->dlc;
        for (int j = 0; j < 8; j++) {
            baselines[baseline_count].expected_pattern[j] = msg->data[j];
        }
        baseline_count++;
    }
}


// Calculate Hamming distance between two byte arrays
uint8_t hamming_distance(uint8_t *data1, uint8_t *data2, uint8_t len) {
    uint8_t distance = 0;
    for (int i = 0; i < len; i++) {
        uint8_t xor = data1[i] ^ data2[i];
        // Count set bits
        while (xor) {
            distance += xor & 1;
            xor >>= 1;
        }
    }
    return distance;
}


// Detect anomalies in incoming message
bool detect_anomaly(CANMessage *msg) {
    // Check 1: CAN ID out of expected range for sensor network
    if (!id_in_ranges(msg->can_id)) {
        return true; // ANOMALY: Unexpected CAN ID
    }
    
    // Check 2: DLC mismatch
    for (int i = 0; i < baseline_count; i++) {
        if (baselines[i].can_id == msg->can_id) {
            if (msg->dlc != baselines[i].dlc) {
                return true; // ANOMALY: DLC changed
            }
            break;
        }
    }
    
    // Check 3: Payload pattern analysis
    // For sensor data, values should stay within expected ranges
    // Example: Temperature sensor (0x300-0x399) should be 0-100°C
    if (msg->can_id >= 0x300 && msg->can_id <= 0x399) {
        uint8_t temp_value = msg->data[0];
        // Reasonable temperature range in Celsius
        if (temp_value > 120) {  // Assuming 0-120°C valid
            return true; // ANOMALY: Out of range
        }
    }
    
    return false; // Normal message
}

// CAN interrupt handler - integrate into your CAN ISR
void CAN_MessageReceived(CANMessage *msg) {
    static MessageWindow window = {0};
    
    // Check for anomaly
    if (detect_anomaly(msg)) {
        // TRIGGER ALERT
        raise_intrusion_alert(msg, ALERT_ANOMALY_DETECTED);
        return;
    }
    
    // Add to window
    window.messages[window.index] = *msg;
    window.index = (window.index + 1) % WINDOW_SIZE;
    window.count++;
    
    // After collecting window of messages, check for traffic patterns
    if (window.count >= WINDOW_SIZE) {
        // Check for DoS pattern: High frequency of single CAN ID
        uint32_t id_counts[256] = {0};
        for (int i = 0; i < WINDOW_SIZE; i++) {
            uint16_t id_hash = window.messages[i].can_id & 0xFF;
            id_counts[id_hash]++;
        }
        
        for (int i = 0; i < 256; i++) {
            if (id_counts[i] > (WINDOW_SIZE * 0.7)) {
                // ANOMALY: Single message type dominates traffic
                raise_intrusion_alert(msg, ALERT_DOS_DETECTED);
                return;
            }
        }
    }
}

void log_alert(CANMessage* msg, uint8_t alert_type){
    sprintf((char*) uartTxBuffer, "ALERT of type %01X  \r\n",
                            (unsigned)alert_type);
                    /* send via DMA UART */
                    DCACHE_CLEAN_BY_ADDR((uint32_t)uartTxBuffer, sizeof(uartTxBuffer));
                    DMAC_ChannelTransfer(DMAC_CHANNEL_0, (const void *)uartTxBuffer,
                                         strlen((const char*)uartTxBuffer),
                                         (const void *)&U4TXREG, 1, 1);
}

// Alert mechanism - customize for your system
void raise_intrusion_alert(CANMessage *msg, uint8_t alert_type) {
    // Log to EEPROM or SD card
    log_alert(msg, alert_type);
    
    // Optionally: Take protective action
    // - Slow down affected sensor reading
    // - Increase monitoring frequency
    // - Send alert to network-based IDS
    // - Trigger external interrupt to master controller
    
    #ifdef DEBUG
    printf("INTRUSION ALERT Type: %d, CAN ID: 0x%03X\n", alert_type, msg->can_id);
    #endif
}
// *****************************************************************************
// *****************************************************************************
// Section: Main Entry Point
// *****************************************************************************
// *****************************************************************************

int main ( void )
{
    uint8_t uartLocalTxBuffer[128] = {0};

    /* Initialize all modules */
    SYS_Initialize ( NULL );
    ids_init();
    
    /* Register callbacks and initialize peripherals */
    I2C1_CallbackRegister(i2cEventHandler, 0);
    MCP9808TempSensorInit();

    DMAC_ChannelCallbackRegister(DMAC_CHANNEL_0, UARTDmaChannelHandler, 0);
    TMR1_CallbackRegister(tmr1EventHandler, 0);

    GPIO_PinInterruptCallbackRegister(SW1_PIN, SW1_User_Handler, 0);
    GPIO_PinInterruptEnable(SW1_PIN);
    GPIO_PinInterruptCallbackRegister(SW2_PIN, SW2_User_Handler, 0);
    GPIO_PinInterruptEnable(SW2_PIN);
    GPIO_PinInterruptCallbackRegister(SW3_PIN, SW3_User_Handler, 0);
    GPIO_PinInterruptEnable(SW3_PIN);

    CAN2_Initialize();

    /* initialize TxBuffer with something predictable */
    for(uint8_t j=0; j<8; j++) TxBuffer[j] = j;
    TxBuffer[0] = 0xAA;

    /* Start timer if you still need LED toggling behavior */
    TMR1_Start();

    while ( true )
    {
        if (deviceResetRequested)
        {
            deviceResetRequested = false;
            // Print once
            sprintf((char*)uartTxBuffer, "Device soft reset via SW3\r\n");
            DCACHE_CLEAN_BY_ADDR((uint32_t)uartTxBuffer, sizeof(uartTxBuffer));
            DMAC_ChannelTransfer(DMAC_CHANNEL_0, (const void *)uartTxBuffer,
                                 strlen((const char*)uartTxBuffer),
                                 (const void *)&U4TXREG, 1, 1);

            // Clear flags
            isTmr1Expired = false;
            isTemperatureRead = false;
            tempSampleRate = TEMP_SAMPLING_RATE_500MS;
            listenMode = false;
            sendTemperatureRequest = false;

            // Reset CAN payload to ?1?byte temp frame?
            TxBufferLen = 1;
            for (uint8_t j = 0; j < 8; j++)
                TxBuffer[j] = 0;

            CAN2_Initialize();

            TMR1_PeriodSet(PERIOD_500MS);
            TMR1_Start();
        }
        /* ----------------- LISTEN MODE: poll CAN and print received messages ----------------- */
        if (listenMode)
        {
            rx_status = CAN2_MessageReceive(&RxMessageID, &RxBufferLen, RxBuffer, &RxTimestamp, RxfifoQueue, &RxAttr);
            if (rx_status)
            {
                /* Wrap received message in CANMessage struct */
                CANMessage rxMsg = {
                    .can_id = RxMessageID,
                    .dlc = RxBufferLen,
                    .timestamp = RxTimestamp
                };
                for (uint8_t b = 0; b < RxBufferLen && b < 8; b++) {
                    rxMsg.data[b] = RxBuffer[b];
                }

                /* Process through IDS */
                if (ids_process_message(&rxMsg))
                {
                    sprintf((char*)uartTxBuffer,
                            "IDS ANOMALY DETECTED: ID=0x%03X Total anomalies=%lu\r\n",
                            (unsigned)RxMessageID, ids_get_anomaly_count());
                }
                else
                {
                    int n = sprintf((char*)uartTxBuffer,
                                    "CAN RX ID=0x%03X DLC=%d TS=%u data=",
                                    (unsigned)RxMessageID, (int)RxBufferLen, (unsigned)RxTimestamp);

                    /* append bytes */
                    for (uint8_t b = 0; b < RxBufferLen && b < 8; b++)
                    {
                        n += sprintf((char*)(uartTxBuffer + n), "%02X ", RxBuffer[b]);
                    }
                    n += sprintf((char*)(uartTxBuffer + n), "\r\n");

                    /* send via DMA UART */
                    DCACHE_CLEAN_BY_ADDR((uint32_t)uartTxBuffer, sizeof(uartTxBuffer));
                    DMAC_ChannelTransfer(DMAC_CHANNEL_0, (const void *)uartTxBuffer,
                                         strlen((const char*)uartTxBuffer),
                                         (const void *)&U4TXREG, 1, 1);
                }else{
                    sprintf((char*) uartTxBuffer, "Message with undefined ID 0x%03X recieved. Filtering \r\n",
                            (unsigned)RxMessageID);
                    /* send via DMA UART */
                    DCACHE_CLEAN_BY_ADDR((uint32_t)uartTxBuffer, sizeof(uartTxBuffer));
                    DMAC_ChannelTransfer(DMAC_CHANNEL_0, (const void *)uartTxBuffer,
                                         strlen((const char*)uartTxBuffer),
                                         (const void *)&U4TXREG, 1, 1);
                }
            }
        }

        /* ----------------- SEND TEMPERATURE ON DEMAND (SW2 pressed) ----------------- */
        if (sendTemperatureRequest)
        {
            /* clear request flag (one-shot behavior) */
            sendTemperatureRequest = false;

            /* start I2C read for temp register (write pointer then read 2 bytes) */
            isTemperatureRead = false;
            I2C1_WriteRead(TEMP_SENSOR_SLAVE_ADDR, &i2cWrData, 1, i2cRdData, 2);

            /* wait for i2c callback to set isTemperatureRead (simple busy wait) */
            uint32_t wait = 0;
            while ((!isTemperatureRead) && (wait++ < 1000000u))
            {
                /* Optionally add a small nop or sleep if platform supports it */
            }

            if (!isTemperatureRead)
            {
                /* I2C failed or timeout */
                sprintf((char*)uartTxBuffer, "I2C read TIMEOUT or ERROR\r\n");
                DCACHE_CLEAN_BY_ADDR((uint32_t)uartTxBuffer, sizeof(uartTxBuffer));
                DMAC_ChannelTransfer(DMAC_CHANNEL_0, (const void *)uartTxBuffer,
                                     strlen((const char*)uartTxBuffer),
                                     (const void *)&U4TXREG, 1, 1);
            }
            else
            {
                /* compute temperature */
                temperatureVal = getTemperature(i2cRdData);

                /* Prepare CAN payload (1 byte = temperature in F) */
                TxBufferLen = 1;
                TxBuffer[0] = temperatureVal;

                tx_status = false;
                if (!CAN2_TxFIFOIsFull(TxfifoQueue))
                {
                    tx_status = CAN2_MessageTransmit(0x321, TxBufferLen, TxBuffer,
                                                     TxfifoQueue, CAN_MSG_TX_DATA_FRAME);
                }

                /* print debug: TX result and temperature */
                sprintf((char*)uartTxBuffer,
                        "Sent Temp over CAN ID=0x%03X tx=%d temp=%02d F\r\n",
                        0x321, (int)tx_status, (int)temperatureVal);

                DCACHE_CLEAN_BY_ADDR((uint32_t)uartTxBuffer, sizeof(uartTxBuffer));
                DMAC_ChannelTransfer(DMAC_CHANNEL_0, (const void *)uartTxBuffer,
                                     strlen((const char*)uartTxBuffer),
                                     (const void *)&U4TXREG, 1, 1);
            }
        }

        /* ----------------- optional: maintain LED toggling on timer expiry (original behavior) ------------- */
        if (isTmr1Expired)
        {
            isTmr1Expired = false;
            LED1_Toggle();
            /* Optionally print LED toggle message if desired */
            /* sprintf((char*)uartLocalTxBuffer, "LED toggled (rate: %s)\r\n", &timeouts[(uint8_t)tempSampleRate][0]);
            DCACHE_CLEAN_BY_ADDR((uint32_t)uartLocalTxBuffer, sizeof(uartLocalTxBuffer));
            DMAC_ChannelTransfer(DMAC_CHANNEL_0, (const void *)uartLocalTxBuffer,
                                 strlen((const char*)uartLocalTxBuffer),
                                 (const void *)&U4TXREG, 1, 1);
             */
        }

        /* small idle/yield to reduce busy looping ? platform specific sleep could be used */
    }

    /* Execution should not come here during normal operation */
    return ( EXIT_FAILURE );
}

/*******************************************************************************
 End of File
*/
