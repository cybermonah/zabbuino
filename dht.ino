#define DHT11 11
#define DHT22 22
#define DHT21 21
#define AM2301 21
#define AM2302 22


/* ****************************************************************************************************************************
*
*   Функция считывания температуры / влажности с цифрового датчика DHT11/DHT21/DHT22/AM2301/AM2302
*
**************************************************************************************************************************** */
int16_t DHTRead(uint8_t _pin, uint8_t _sensor_model, uint8_t _metric, char* _outBuffer)
{
  uint8_t data[6];
  uint8_t laststate = HIGH;
  uint8_t counter = 0;
  uint8_t j, i;
  int16_t result;

  data[0] = data[1] = data[2] = data[3] = data[4] = 0;
  j = 0;
  // pull the pin high and wait 250 milliseconds
  digitalWrite(_pin, HIGH);
  delay(250);

  // now pull it low for ~20 milliseconds
  pinMode(_pin, OUTPUT);
  digitalWrite(_pin, LOW);
  delay(20);
  noInterrupts();
  digitalWrite(_pin, HIGH);
  delayMicroseconds(40);
  pinMode(_pin, INPUT);

  // read in timings
  //  for ( i = 0; i < MAXTIMINGS; i++) {
  for ( i = 0; i < 85; i++) {
    counter = 0;
    while (digitalRead(_pin) == laststate) {
      counter++;
      delayMicroseconds(1);
      if (counter == 255) {
        break;
      }
    }
    laststate = digitalRead(_pin);

    if (counter == 255) break;

    // ignore first 3 transitions
    if ((i >= 4) && (i % 2 == 0)) {
      // shove each bit into the storage bytes
      data[j / 8] <<= 1;
      //      if (counter > _count)
      if (counter > 6)
        data[j / 8] |= 1;
      j++;
    }

  }

  interrupts();

  // check we read 40 bits and that the checksum matches
  if ((j >= 40) &&
      (data[4] != ((data[0] + data[1] + data[2] + data[3]) & 0xFF)) ) {
    return DEVICE_DISCONNECTED_C;
  }

  if (SENS_READ_HUMD == _metric)
  {
    // calc humidity
    switch (_sensor_model) {
      case DHT11:
        result = data[0];
      case DHT22:
      case DHT21:
      default:
        result = data[0];
        result = result << 8;
        result += data[1];
    }
  } else {
    // calc temp
    switch (_sensor_model) {
      case DHT11:
        result = data[2];
      case DHT22:
      case DHT21:
      default:
        result = data[2] & 0x7F;
        result = result << 8;
        result += data[3];
        if (data[2] & 0x80) result = -result;
    }
  }

  ltoaf(result, _outBuffer, 1);
  return RESULT_IN_BUFFER;
}

