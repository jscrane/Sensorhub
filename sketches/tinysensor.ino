#include <RF24Network.h>
#include <RF24.h>
#include <SPI.h>
#include <DHT.h>

#include "tinysensor.h"
#include "wdt.h"

const uint8_t CE_PIN = 8;
const uint8_t CS_PIN = 7;
const uint8_t DHT_PIN = 2;

RF24 radio(CE_PIN, CS_PIN);
RF24Network network(radio);
DHT dht;

const uint8_t channel = 90;
const uint16_t master_node = 0;
const uint16_t this_node = NODE_ID;
const uint8_t retry_count = 5;		// 250+5*15*250 = 19mS
const uint8_t retry_delay = 1;		// 1*250uS

void setup(void)
{
	pinMode(A1, INPUT_PULLUP);

	dht.setup(DHT_PIN);

	SPI.begin();
	radio.begin();

	radio.enableDynamicPayloads();
	radio.setRetries(retry_delay, retry_count);

	network.begin(channel, this_node);
}

void loop(void)
{
	radio.powerUp();
	network.update();

	unsigned lsens = analogRead(A1);
	uint8_t light = 255 - lsens / 4;
	uint16_t battery = analogRead(A0);

	// millis() only counts time when the sketch is not sleeping
	sensor_payload_t payload = { millis(), light, dht.getStatus(), dht.getHumidity(), dht.getTemperature(), battery };
	RF24NetworkHeader header(master_node, sensor_type_id);
	network.write(header, &payload, sizeof(payload));

	radio.powerDown();

	unsigned secs = lsens / 8 + 1;
	wdt_sleep(secs);
}
