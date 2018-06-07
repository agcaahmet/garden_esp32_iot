/*
 Name:		esp32_iot.ino
 Created:	5/30/2018 10:32:12 PM
 Author:	ahmet
*/
#include <WiFi.h>
#include <OneWire.h>

#define PIN_MOIST_SENS	33
#define PIN_SENS_EXC_P	27
#define PIN_SENS_EXC_N	12
#define PIN_MOTOR		22
#define PIN_TEMP_SENS	25
#define PIN_BLINK		16

#define WIFI_SSID		"khorfo_net"
#define WIFI_PASS		"ahmet_ipek_12082004"
#define WIFI_CONNECTION_THRESHOLD 10000

OneWire ds(PIN_TEMP_SENS); 

double sens_val_moist = 0;
double sens_val_temp = 0;
bool wifi_connected = false;
long total_watering = 0;
long watering_limit = 50000; 
long mc_min = 0;

enum stateType
{
	STATE_WAIT_FOR_LL = 0,
	STATE_MONITOR_LL = 1,
	STATE_WATERING = 2,
	STATE_WAIT_AFTER_WATERING =3,
	STATE_MONITOR_HL = 4,
	STATE_WATERING_LIMIT = 5,
};

stateType state = STATE_WAIT_FOR_LL;
// the setup function runs once when you press reset or power the board
void setup() {
	Serial.begin(921600);

	connectToWiFi();

	pinMode(PIN_BLINK, OUTPUT);
	pinMode(PIN_MOIST_SENS, INPUT);
	pinMode(PIN_MOTOR, OUTPUT);
	pinMode(PIN_SENS_EXC_P, OUTPUT);
	pinMode(PIN_SENS_EXC_N, OUTPUT);
	pinMode(PIN_MOTOR, OUTPUT);


	digitalWrite(PIN_SENS_EXC_P, LOW);
	digitalWrite(PIN_SENS_EXC_N, LOW);

	xTaskCreatePinnedToCore(task_sensor_read, "sensor_read", 2048, NULL, 11, NULL, 1);
	xTaskCreatePinnedToCore(task_temp_read, "temp_read", 2048, NULL, 10, NULL, 1);
	xTaskCreatePinnedToCore(task_serial, "task_serial", 2048, NULL, 1, NULL, 0); 
	xTaskCreatePinnedToCore(task_IoT, "task_IoT", 2048, NULL, 5, NULL, 0);
	xTaskCreatePinnedToCore(task_check_connection, "task_check_connection", 2048, NULL, 4, NULL, 0);
	xTaskCreatePinnedToCore(task_fsm, "task_fsm", 1024, NULL, 6, NULL, 0);
	xTaskCreatePinnedToCore(task_blink, "task_blink", 1024, NULL, 2, NULL, 0);


}

// the loop function runs over and over again until power down or reset
void loop() {
	vTaskSuspend(NULL);
}

void task_sensor_read(void * parameter)
{
	int arr_size = 100;
	int moistArr[100];

	for (int i = 0; i < arr_size; i++)
	{
		moistArr[i] = 4095;
	}


	int arr_index = 0;
	double sens_moist = 4095;
	double sens_moist_sum = 409500;


	while (true)
	{
		sens_moist_sum -= moistArr[arr_index];


		digitalWrite(PIN_SENS_EXC_P, HIGH);
		digitalWrite(PIN_SENS_EXC_N, LOW);
		delay(5);
		moistArr[arr_index] = analogRead(PIN_MOIST_SENS);
		digitalWrite(PIN_SENS_EXC_P, LOW);
		digitalWrite(PIN_SENS_EXC_N, HIGH);
		delay(5);
		digitalWrite(PIN_SENS_EXC_P, LOW);
		digitalWrite(PIN_SENS_EXC_N, LOW);

		sens_moist_sum += moistArr[arr_index];

		arr_index = (arr_index + 1) % arr_size;

		sens_moist = sens_moist_sum / arr_size;

		sens_val_moist = sens_moist / 4095 * 100;

		//Serial.print("index:");
		//Serial.print(arr_index);
		//Serial.print("  Moisture:");
		//Serial.println(sens_moist);

		delay(90);   
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
		if (total_watering > 0)
		{
			total_watering--;
		}


		mc_min = millis() / 60000;
		Serial.print("State: ");
		Serial.print(state);
		Serial.print("    Temp:");
		Serial.print(sens_val_temp);
		Serial.print("    Moist:");
		Serial.print(sens_val_moist);
		Serial.print("    mc_min:");
		Serial.println(mc_min);
		delay(1000);
	}
	vTaskDelete(NULL);
}

void task_blink(void * parameter)
{

	while (true)
	{
		digitalWrite(PIN_BLINK, HIGH);
		delay(750);
		digitalWrite(PIN_BLINK, LOW);
		delay(750);
	}
	vTaskDelete(NULL);
}

void task_IoT(void * parameter)
{
	WiFiClient clientIoT;

	// ThingSpeak Settings
	const int channelID = 509184;
	String writeAPIKey = "NCEVIYDHZVS7W1QD"; // write API key for your ThingSpeak Channel
	const char* serverIoT = "api.thingspeak.com";

	while (true)
	{
		if (wifi_connected && clientIoT.connect(serverIoT, 80)) {

			// Construct API request body
			String body = "&field1=";
			body += String(sens_val_temp);
			body += "&field2=";
			body += String(mc_min);
			body += "&field3=";
			body += String(sens_val_moist);
			body += "&field4=";
			body += String(state);
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
void task_check_connection(void * parameter)
{
	while (true)
	{
		delay(100000);
		if (!wifi_connected)
		{
			connectToWiFi();
		}
	}
	vTaskDelete(NULL);
}
void task_fsm(void * parameter)
{
	const double moist_ll = 40;
	const double moist_hl = 45;
	long monitor_ll_start = 0;
	long monitor_ll_thr_time = 30000;
	long monitor_hl_start = 0;
	long monitor_hl_thr_time = 30000;
	while (true)
	{
		switch (state)
		{
		case STATE_WAIT_FOR_LL:
			if (sens_val_moist < moist_ll)
			{
				state = STATE_MONITOR_LL;
				monitor_ll_start = millis();
			}
			break;
		case STATE_MONITOR_LL:

			if ((millis() - monitor_ll_start) > monitor_ll_thr_time)
			{
				state = STATE_WATERING;
			}
			else if (sens_val_moist > (moist_ll + 1))
			{
				state = STATE_WAIT_FOR_LL;
			}

			break;
		case STATE_WATERING:
			if (total_watering < watering_limit)
			{
				digitalWrite(PIN_MOTOR, HIGH);
				delay(10000);
				digitalWrite(PIN_MOTOR, LOW);
				delay(10000);
				total_watering += 10000;
				state = STATE_WAIT_AFTER_WATERING;
			}
			else
			{
				state = STATE_WATERING_LIMIT;
			}

			break;
		case STATE_WAIT_AFTER_WATERING:
			delay(60000);
			state = STATE_MONITOR_HL;
			monitor_hl_start = millis();
			break;
		case STATE_MONITOR_HL:
			if ((millis() - monitor_hl_start) > monitor_hl_thr_time)
			{
				state = STATE_WAIT_FOR_LL;
			}
			else if (sens_val_moist < moist_hl)
			{
				state = STATE_WATERING;
			}
			break;
		case STATE_WATERING_LIMIT:
			delay(20000);
			state = STATE_WAIT_FOR_LL;
			break;
		default:
			state = STATE_WAIT_FOR_LL;
			break;
		}
		delay(100);
	}
	vTaskDelete(NULL);
}