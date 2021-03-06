// Config & common included files
#include "sys_includes.h"

#include <SoftwareSerial.h>

#include "service.h"
#include "system.h"

#include "uart_bus.h"
#include "winsen.h"
#include "mh_zxx.h"

/*****************************************************************************************************************************
*
*   Calculate CRC of Winsen MH-Zxx CO2 sensor data packet
*
*   Returns: 
*     - CRC
*
*****************************************************************************************************************************/
static uint8_t crcMHZxx(uint8_t* _data) {
    uint8_t crc = 0;
    for(uint8_t i = 1; i < (MH_ZXX_PACKET_SIZE-1); i++) { crc += _data[i]; }
    crc = 0xFF - crc;
    crc += 1;  
    return crc;
}

/*****************************************************************************************************************************
*
*   Read specified metric's value of the Winsen MH-Zxx CO2 sensor via UART, put it to output buffer on success. 
*
*   Returns: 
*     - RESULT_IS_BUFFERED on success
*     - DEVICE_ERROR_TIMEOUT if device stop talking
*
*****************************************************************************************************************************/
int8_t getMHZxxMetricUART(const uint8_t _rxPin, const uint8_t _txPin, int32_t* _value) {
  uint8_t len, rc = DEVICE_ERROR_TIMEOUT;
  uint8_t data[MH_ZXX_PACKET_SIZE] = {0};
  SoftwareSerial swSerial(_rxPin, _txPin);

  // Send query only if sensor heated
  if (millis() > MH_ZXX_PREHEAT_TIMEOUT) {
  
     swSerial.begin(MH_ZXX_UART_SPEED);

     data[MH_ZXX_STARTING_BYTE] = 0xFF;                           // Starting byte
     data[MH_ZXX_SENSOR_NUMBER] = 0x01;                           // Sensor No.
     data[MH_ZXX_CMD] = MH_ZXX_CMD_GAS_CONCENTRATION;             // Command
     //data[3] = data[4] = data[5] = data[6] = data[7] = 0x00;      // Stub bytes
     data[MH_ZXX_CRC] = 0x79;                                     // Check value

     // Flush all device's transmitted data to avoid get excess data in recieve buffer
     //serialRXFlush(&swSerial, !UART_SLOW_MODE);
     flushStreamRXBuffer(&swSerial, MH_ZXX_DEFAULT_READ_TIMEOUT, !UART_SLOW_MODE);


     // The serial stream can get out of sync. The response starts with 0xff, try to resync : https://github.com/jehy/arduino-esp8266-mh-z19-serial/blob/master/arduino-esp8266-mhz-19-serial.ino
     //  Send command to MH-Zxx
     serialSend(&swSerial, data, MH_ZXX_PACKET_SIZE, !UART_SLOW_MODE);

     
     //  Recieve from MH-Zxx
     //  It actually do not use '\r', '\n', '\0' to terminate string
     len = serialRecive(&swSerial, data, MH_ZXX_PACKET_SIZE, MH_ZXX_DEFAULT_READ_TIMEOUT, !UART_STOP_ON_CHAR, '\r', !UART_SLOW_MODE);
     
     // Connection timeout occurs
     if (len < MH_ZXX_PACKET_SIZE) { rc = DEVICE_ERROR_TIMEOUT; goto finish; }
     
     // Wrong answer. buffer[0] must contain 0xFF
     if (0xFF != data[MH_ZXX_STARTING_BYTE]) { rc = DEVICE_ERROR_WRONG_ANSWER; goto finish; }
     
     // Bad CRC
     // CRC calculate for bytes #1..#9 (byte #0 excluded)
     if (data[MH_ZXX_CRC] != crcMHZxx(data)) { rc = DEVICE_ERROR_CHECKSUM; goto finish; }
     
     *_value = 256 * data[MH_ZXX_GAS_CONCENTRATION_HIGH_BYTE];
     *_value += data[MH_ZXX_GAS_CONCENTRATION_LOW_BYTE];
  } else {  // if (millis() > MH_ZXX_PREHEAT_TIMEOUT)
     // Return 'good concentracion' while sensor heated
     *_value = MH_ZXX_PREHEAT_GAS_CONCENTRATION;
  } // if (millis() > MH_ZXX_PREHEAT_TIMEOUT)


  rc = RESULT_IS_UNSIGNED_VALUE;

  finish:
  gatherSystemMetrics(); // Measure memory consumption
  swSerial.~SoftwareSerial(); 
  return rc;

}


/*****************************************************************************************************************************
*
*  Read specified metric's value of the Winsen MH-Zxx CO2 sensor via PWM, put it to output buffer on success. 
*
*  Returns: 
*    - RESULT_IS_BUFFERED on success
*    - DEVICE_ERROR_ACK_L
*    - DEVICE_ERROR_ACK_H
*    - DEVICE_ERROR_TIMEOUT if sensor stops answer to the request
*
*****************************************************************************************************************************/
int8_t getMHZxxMetricPWM(uint8_t _pin, uint16_t _range, int32_t* _value) {

//  volatile uint8_t *PIR;
  uint8_t stage = MH_ZXX_STAGE_WAIT_FOR_LOW, 
          pinState, // pinBit, pinPort,
          rc = DEVICE_ERROR_ACK_L;
          
  uint32_t startTime, 
           nowTime,
           highTime = 0x00,
           lowTime = 0x00;

  
  /* 
  pinBit = digitalPinToBitMask(_pin);
  pinPort = digitalPinToPort(_pin);
  *PIR = portInputRegister(pinPort);
*/
  pinMode(_pin, INPUT_PULLUP);

  // Send query only if sensor heated
  if (millis() > MH_ZXX_PREHEAT_TIMEOUT) {

     stopTimerOne(); 
     startTime = millis();
  
     do {
        nowTime = millis();
        pinState = digitalRead(_pin);
//        pinState = *PIR & pinBit;

        switch (stage) {
          case MH_ZXX_STAGE_WAIT_FOR_LOW:  
            if (LOW == pinState) { 
                rc = DEVICE_ERROR_ACK_H;
                stage = MH_ZXX_STAGE_WAIT_FOR_HIGH; 
            }
            break;
     
          case MH_ZXX_STAGE_WAIT_FOR_HIGH:  
            if (HIGH == pinState) { 
               rc = DEVICE_ERROR_TIMEOUT;
               stage = MH_ZXX_STAGE_COUNT_FOR_HIGH; 
               highTime = nowTime;
            }
            break;
     
          case MH_ZXX_STAGE_COUNT_FOR_HIGH:
            if (LOW == pinState) { 
               highTime = nowTime - highTime;
               lowTime = nowTime;
               stage = MH_ZXX_STAGE_COUNT_FOR_LOW; 
            }
            break;
     
          case MH_ZXX_STAGE_COUNT_FOR_LOW:  
            if (HIGH == pinState) { 
               lowTime = nowTime - lowTime;
               stage = MH_ZXX_STAGE_CYCLE_FINISHED; 
               goto finish; 
            }
            break;       
        
        }   
     } while ( nowTime - startTime < MH_ZXX_CYCLE_TIME);
     
     finish:
     *_value = 0;
     if (MH_ZXX_STAGE_CYCLE_FINISHED == stage) {
/*
        DTSH( DEBUG_PORT.println(F("High level time: "));  DEBUG_PORT.print(highTime);
              DEBUG_PORT.println(F(", low level time: ")); DEBUG_PORT.println(highTime);
        )
*/ 
        *_value = _range * (highTime - 2) / (highTime + lowTime - 4);
     } 
     startTimerOne(); 
     gatherSystemMetrics(); 

  } else {  // if (millis() > MH_ZXX_PREHEAT_TIMEOUT)

     // Return 'good concentracion' while sensor heated
     *_value = MH_ZXX_PREHEAT_GAS_CONCENTRATION;
  }  // if (millis() > MH_ZXX_PREHEAT_TIMEOUT)

  rc = RESULT_IS_UNSIGNED_VALUE;

  return rc;   


}
