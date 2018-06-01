/*
 Name:		esp32_iot.ino
 Created:	5/30/2018 10:32:12 PM
 Author:	ahmet
*/
#include <WiFi.h>
#include <OneWire.h>

#define PIN_MOIST_SENS	36
#define PIN_HMDTY_SENS	39
#define PIN_MOTOR		26
#define PIN_TEMP_SENS	25

#define WIFI_SSID		"khorfo_net"
#define WIFI_PASS		"ahmet_ipek_12082004"
#define WIFI_CONNECTION_THRESHOLD 10000

OneWire ds(PIN_TEMP_SENS); 

double sens_val_moist = 0;
double sens_val_hmdty = 0;
double sens_val_temp = 0;
bool wifi_connected = false;

// the setup function runs once when you press reset or power the board
void setup() {
	Serial.begin(921600);

	connectToWiFi();

	pinMode(PIN_MOIST_SENS, INPUT);
	pinMode(PIN_HMDTY_SENS, INPUT);
	pinMode(PIN_MOTOR, OUTPUT);


	//digitalWrite(PIN_MOTOR, HIGH);

	xTaskCreatePinnedToCore(task_sensor_read, "sensor_read", 2048, NULL, 11, NULL, 1);
	xTaskCreatePinnedToCore(task_temp_read, "temp_read", 2048, NULL, 10, NULL, 1);
	xTaskCreatePinnedToCore(task_serial, "task_serial", 2048, NULL, 1, NULL, 0); 
	xTaskCreatePinnedToCore(task_IoT, "task_IoT", 2048, NULL, 5, NULL, 0);


}

// the loop function runs over and over again until power down or reset
void loop() {
	vTaskSuspend(NULL);
}

void task_sensor_read(void * parameter)
{
	int arr_size = 100;
	int moistArr[100];
	int hmdtyArr[100];
	for (int i = 0; i < arr_size; i++)
	{
		moistArr[i] = 4095;
		hmdtyArr[i] = 4095;
	}


	int arr_index = 0;
	double sens_moist = 4095;
	double sens_hmdty = 4095;
	double sens_moist_sum = 409500;
	double sens_hmdty_sum = 409500;

	while (true)
	{
		sens_moist_sum -= moistArr[arr_index];
		sens_hmdty_sum -= hmdtyArr[arr_index];
		moistArr[arr_index] = analogRead(PIN_MOIST_SENS);
		hmdtyArr[arr_index] = analogRead(PIN_HMDTY_SENS);
		sens_moist_sum += moistArr[arr_index];
		sens_hmdty_sum += hmdtyArr[arr_index];
		arr_index = (arr_index + 1) % arr_size;

		sens_hmdty = sens_hmdty_sum / arr_size;
		sens_moist = sens_moist_sum / arr_size;

		sens_val_hmdty = (4095 - sens_hmdty) / 4095 * 100;
		sens_val_moist = (4095 - sens_moist) / 4095 * 100;

		//Serial.print("index:");
		//Serial.print(arr_index);
		//Serial.print("  Moisture:");
		//Serial.print(sens_moist);
		//Serial.print("   Humudity:");
		//Serial.println(sens_hmdty);
		delay(50);   
	}
	vTaskDelete(NULL);
}

void task_temp_read(void * parameter)
{
	byte i;
	byte present = 0;
	byte type_s;
	byte data[12];
	byte addr[8];
	float celsius;


	if (!ds.search(addr))
	{
		ds.reset_search();
		delay(250);
	}

	if (OneWire::crc8(addr, 7) != addr[7])
	{
		Serial.println("CRC is not valid!");
	}

	switch (addr[0])
	{
	case 0x10:
		type_s = 1;
		break;
	case 0x28:
		type_s = 0;
		break;
	case 0x22:
		type_s = 0;
		break;
	default:
		Serial.println("Device is not a DS18x20 family device.");
	}


	while (true)
	{
		ds.reset();
		ds.select(addr);
		ds.write(0x44, 1); // start conversion, with parasite power on at the end
		delay(1000);
		present = ds.reset();
		ds.select(addr);
		ds.write(0xBE); // Read Scratchpad

		for (i = 0; i < 9; i++)
		{
			data[i] = ds.read();
		}

		// Convert the data to actual temperature
		int16_t raw = (data[1] << 8) | data[0];
		if (type_s) {
			raw = raw << 3; // 9 bit resolution default
			if (data[7] == 0x10)
			{
				raw = (raw & 0xFFF0) + 12 - data[6];
			}
		}
		else
		{
			byte cfg = (data[4] & 0x60);
			if (cfg == 0x00) raw = raw & ~7; // 9 bit resolution, 93.75 ms
			else if (cfg == 0x20) raw = raw & ~3; // 10 bit res, 187.5 ms
			else if (cfg == 0x40) raw = raw & ~1; // 11 bit res, 375 ms

		}
		celsius = (float)raw / 16.0;
		sens_val_temp = celsius;
	}
	vTaskDelete(NULL);
}


void task_serial(void * parameter)
{
	delay(2000);
	while (true)
	{
		Serial.print("Temp:");
		Serial.print(sens_val_temp);
		Serial.print("    Moist:");
		Serial.print(sens_val_moist);
		Serial.print("    Hmdty:");
		Serial.println(sens_val_hmdty);
		delay(1000);
	}
	vTaskDelete(NULL);
}


void task_IoT(void * parameter)
{
	WiFiClient clientIoT;

	// ThingSpeak Settings
	const int channelID = 509184;
	String writeAPIKey = "MV5DJEW7VDOC1F1B"; // write API key for your ThingSpeak Channel
	const char* serverIoT = "api.thingspeak.com";

	while (true)
	{
		if (wifi_connected && clientIoT.connect(serverIoT, 80)) {

			// Construct API request body
			String body = "&field1=";
			body += String(sens_val_temp);
			body += "&field2=";
			body += String(sens_val_hmdty);
			body += "&field3=";
			body += String(sens_val_moist);
			clientIoT.print("POST /update HTTP/1.1\n");
			clientIoT.print("Host: api.thingspeak.com\n");
			clientIoT.print("Connection: close\n");
			clientIoT.print("X-THINGSPEAKAPIKEY: " + writeAPIKey + "\n");
			clientIoT.print("Content-Type: application/x-www-form-urlencoded\n");
			clientIoT.print("Content-Length: ");
			clientIoT.print(body.length());
			clientIoT.print("\n\n");
			clientIoT.print(body);
			clientIoT.print("\n\n");

		}
		clientIoT.stop();
		delay(15100);
	}
	vTaskDelete(NULL);
}
void connectToWiFi()
{
	// delete old config
	WiFi.disconnect(true);
	//register event handler
	WiFi.onEvent(WiFiEvent);

	Serial.print("Connecting to ");
	Serial.println(WIFI_SSID);

	WiFi.begin(WIFI_SSID, WIFI_PASS);
	unsigned long startTimeWifi = millis();
	while (WiFi.status() != WL_CONNECTED) {
		delay(500);
		Serial.print(".");
		if (millis() - startTimeWifi > WIFI_CONNECTION_THRESHOLD)
		{
			Serial.println("Wifi connection timed out!");
			break;
		}
	}
}
void WiFiEvent(WiFiEvent_t event) {
	switch (event) {
	case SYSTEM_EVENT_STA_GOT_IP:
		//When connected set 
		wifi_connected = true;
		Serial.println("");
		Serial.println("WiFi connected");
		Serial.print("IP address: ");
		Serial.println(WiFi.localIP());
		break;
	case SYSTEM_EVENT_STA_DISCONNECTED:
		wifi_connected = false;
		Serial.println("Wifi lost connection!");
		break;
	}
}