/*
 Name:		esp32_iot.ino
 Created:	5/30/2018 10:32:12 PM
 Author:	ahmet
*/
#include <WiFi.h>
#include <OneWire.h>
#include <ArduinoJson.h>
#include "hidden_keys.h"

#define PIN_MOIST_SENS	33
#define PIN_SENS_EXC_P	27
#define PIN_SENS_EXC_N	12
#define PIN_MOTOR		22
#define PIN_MOTOR_RB	34
#define PIN_TEMP_SENS	25
#define PIN_BLINK		16

#define WIFI_SSID		_WIFI_SSID
#define WIFI_PASS		_WIFI_PASS
#define WIFI_CONNECTION_THRESHOLD 10000
#define WATERING_DURATION	35000
#define WATERING_PERIOD_IN_MIN   720


// ThingSpeak Settings
const int channelID = 509184;
String writeAPIKey = _writeAPIKey; // write API key for your ThingSpeak Channel
String readAPIKey = _readAPIKey; // read API key for your ThingSpeak Channel
String APIkey = "509184";
const char* serverIoT = "api.thingspeak.com";

const unsigned long HTTP_TIMEOUT = 8000;  // max respone time from server

OneWire ds(PIN_TEMP_SENS); 

double sens_val_moist = 0;
double sens_val_temp = 0;
bool wifi_connected = false;
long total_watering = 0;
long watering_limit = 1000000; 
long mc_min = 0;
String mc_min_rb_str = "-1";
String ts_datetime = "0";
int watering_time_next = 360;
bool watering_cmd = false;

int relay_status = LOW;
int relay_status_old = LOW;

long relay_high_st_time = 0;
long relay_high_time = 0;
int motor_active_duration = 0;
bool myKey = false;
bool myKeySent = false;

QueueHandle_t queueWT;

enum stateType
{
	STATE_WAIT_FOR_CMD = 0,
	STATE_MONITOR_LL = 1,
	STATE_WATERING = 2,
	STATE_WAIT_AFTER_WATERING =3,
	STATE_MONITOR_HL = 4,
	STATE_WATERING_LIMIT = 5,
};

stateType state = STATE_WAIT_FOR_CMD;
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
	pinMode(PIN_MOTOR_RB, INPUT);


	digitalWrite(PIN_SENS_EXC_P, LOW);
	digitalWrite(PIN_SENS_EXC_N, LOW);

	xTaskCreatePinnedToCore(task_sensor_read, "sensor_read", 2048, NULL, 11, NULL, 1);
	xTaskCreatePinnedToCore(task_temp_read, "temp_read", 2048, NULL, 10, NULL, 1);
	xTaskCreatePinnedToCore(task_serial, "task_serial", 1024, NULL, 1, NULL, 0); 
	xTaskCreatePinnedToCore(task_IoT, "task_IoT", 6000, NULL, 5, NULL, 0);
	xTaskCreatePinnedToCore(task_check_connection, "task_check_connection", 2048, NULL, 4, NULL, 0);
	xTaskCreatePinnedToCore(task_fsm, "task_fsm", 1024, NULL, 6, NULL, 0);
	xTaskCreatePinnedToCore(task_relay_monitor, "task_relay_monitor", 1024, NULL, 3, NULL, 1);
	xTaskCreatePinnedToCore(task_blink, "task_blink", 1024, NULL, 2, NULL, 1);

	queueWT = xQueueCreate(10, sizeof(int));

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
		analogRead(PIN_MOIST_SENS);    //dummy analog read 
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
		mc_min = millis() / 60000;
		//Serial.print("State: ");
		//Serial.print(state);
		//Serial.print("    Temp:");
		//Serial.print(sens_val_temp);
		//Serial.print("    Moist:");
		//Serial.print(sens_val_moist);
		//Serial.print("    mc_min:");
		//Serial.println(mc_min);


		Serial.print("RH ST:");
		Serial.print(relay_high_st_time);
		Serial.print("   RHT:");
		Serial.print(relay_high_time);
		Serial.print("   MAD");
		Serial.println(motor_active_duration);

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
	WiFiServer server(80);
	WiFiClient clientIoT;
	delay(20000);

	while (true)
	{
		if (wifi_connected && clientIoT.connect(serverIoT, 80)) {
			write_TS(clientIoT);
		}
		clientIoT.stop();
		delay(1000);
		clientIoT = server.available();
		if (wifi_connected && clientIoT.connect(serverIoT, 80)) {
			read_TS(clientIoT);
		}
		clientIoT.stop();

		//Serial.println(uxTaskGetStackHighWaterMark(NULL));
		delay(14000);
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
	const double moist_ll = 45;
	long monitor_ll_start = 0;
	long monitor_ll_thr_time = 30000;
	long watering_limit_start = 0;
	long watering_limit_wait_time = 20000;
	bool watering_cmd_old = false;
	while (true)
	{
		check_watering_cmd();

		switch (state)
		{
		case STATE_WAIT_FOR_CMD:
			if (watering_cmd && !watering_cmd_old)
			{
				state = STATE_WATERING;
				monitor_ll_start = millis();
			}
			break;
		case STATE_WATERING:
			if (total_watering < watering_limit)
			{
				digitalWrite(PIN_MOTOR, HIGH);
				delay(WATERING_DURATION);
				digitalWrite(PIN_MOTOR, LOW);
				delay(WATERING_DURATION);
				total_watering += 100000;
				state = STATE_WAIT_FOR_CMD;
			}
			else
			{
				state = STATE_WATERING_LIMIT;
				watering_limit_start = millis();
			}

			break;
		case STATE_WATERING_LIMIT:
			if ((millis() - watering_limit_start) > watering_limit_wait_time)
			{
				state = STATE_WAIT_FOR_CMD;
			}
			break;
		default:
			state = STATE_WAIT_FOR_CMD;
			break;
		}

		watering_cmd_old = watering_cmd;
		if (total_watering > 0)
		{
			total_watering--;
		}
		delay(100);
	}
	vTaskDelete(NULL);
}
void write_TS(WiFiClient & _clientIoT)
{
	// Construct API request body
	String body = "&field1=";
	body += String(sens_val_temp);
	body += "&field2=";
	body += String(mc_min);
	body += "&field3=";
	body += String(sens_val_moist);
	body += "&field4=";
	body += String(state);
	body += "&field5=";
	int val = 0;
	xQueueReceive(queueWT, &val, 10);
	body += String(val);

	_clientIoT.print("POST /update HTTP/1.1\n");
	_clientIoT.print("Host: api.thingspeak.com\n");
	_clientIoT.print("Connection: close\n");
	_clientIoT.print("X-THINGSPEAKAPIKEY: " + writeAPIKey + "\n");
	_clientIoT.print("Content-Type: application/x-www-form-urlencoded\n");
	_clientIoT.print("Content-Length: ");
	_clientIoT.print(body.length());
	_clientIoT.print("\n\n");
	_clientIoT.print(body);
	_clientIoT.print("\n\n");
}

void read_TS(WiFiClient & _clientIoT) {  // Receive data from Thingspeak
	static char responseBuffer[3 * 1024]; // Buffer for received data

	String url = "/channels/" + APIkey; // Start building API request string
	url += "/fields/2.json?results=1";  // 1 is the results request number, so 1 are returned, 1 woudl return the last result received
	_clientIoT.print(String("GET ") + url + " HTTP/1.1\r\n" + "Host: " + serverIoT + "\r\n" + "Connection: close\r\n\r\n");
	while (!skipResponseHeaders(_clientIoT));                      // Wait until there is some data and skip headers
	while (_clientIoT.available()) {                         // Now receive the data
		String line = _clientIoT.readStringUntil('\n');
		if (line.indexOf('{', 0) >= 0) {                  // Ignore data that is not likely to be JSON formatted, so must contain a '{'
			//Serial.println(line);                            // Show the text received
			line.toCharArray(responseBuffer, line.length()); // Convert to char array for the JSON decoder
			decodeJSON(responseBuffer);                      // Decode the JSON text
		}
	}
}

bool skipResponseHeaders(WiFiClient & _clientIoT) {
	char endOfHeaders[] = "\r\n\r\n"; // HTTP headers end with an empty line 
	_clientIoT.setTimeout(HTTP_TIMEOUT);
	bool ok = _clientIoT.find(endOfHeaders);
	if (!ok) { Serial.println("No response or invalid response!"); }
	return ok;
}

bool decodeJSON(char *json) {
	StaticJsonBuffer <3 * 1024> jsonBuffer;
	char *jsonstart = strchr(json, '{'); // Skip characters until first '{' found and ignore length, if present
	if (jsonstart == NULL) {
		Serial.println("JSON data missing");
		return false;
	}
	json = jsonstart;
	JsonObject& root = jsonBuffer.parseObject(json); // Parse JSON
	if (!root.success()) {
		Serial.println(F("jsonBuffer.parseObject() failed"));
		return false;
	}
	JsonObject& root_data = root["channel"]; // Begins and ends within first set of { }
	String id = root_data["id"];
	String name = root_data["name"];
	String datetime = root_data["updated_at"];
	ts_datetime = datetime;
	Serial.println("\n\n Channel id: " + id + " Name: " + name);
	Serial.println(" Readings last updated at: " + datetime);

	for (int result = 0; result < 1; result++) {
		JsonObject& channel = root["feeds"][result]; // Now we can read 'feeds' values and so-on
		String entry_id = channel["entry_id"];
		String field2value = channel["field2"];
		mc_min_rb_str = field2value;
		Serial.print(" Field2 entry number [" + entry_id + "] had a value of: "); Serial.println(field2value);
	}
}

void check_watering_cmd()
{
	long mc_min_rb = mc_min_rb_str.toInt();

	int time_hour = -1;
	int time_min = -1;

	if (abs(mc_min - mc_min_rb) < 3)  // If connection with thingspeak is OK
	{
		if (ts_datetime.length() == 20 && ts_datetime.charAt(10) == 'T' && ts_datetime.charAt(19) == 'Z')
		{
			time_hour = ts_datetime.substring(11, 13).toInt();
			time_min = ts_datetime.substring(14, 16).toInt();

			if ((time_hour == 15 && (time_min < 10)) || (time_hour == 3 && (time_min < 10)))
			{
				watering_cmd = true;
				watering_time_next = mc_min + WATERING_PERIOD_IN_MIN;
			}
			else
			{
				watering_cmd = false;
			}
		}
	}
	else
	{
		if (mc_min == watering_time_next)
		{
			watering_cmd = true;
		}
		else if (mc_min > watering_time_next)
		{
			watering_time_next += WATERING_PERIOD_IN_MIN;
			watering_cmd = false;
		}
		else
		{
			watering_cmd = false;
		}

	}




	
}

void task_relay_monitor(void * parameter)
{
	delay(2000);
	while (true)
	{
		relay_status = digitalRead(PIN_MOTOR_RB);

		if (relay_status == HIGH && relay_status_old == LOW)
		{
			relay_high_st_time = millis();
		}
		else if (relay_status == HIGH && relay_status_old == HIGH)
		{
			relay_high_time = millis() - relay_high_st_time;
		}
		else if(relay_status == LOW && relay_status_old == HIGH)
		{
			motor_active_duration = (millis() - relay_high_st_time) / 1000;	
			xQueueSend(queueWT, &motor_active_duration, 10);
		}
		else
		{
			relay_high_time = 0;
		}
		relay_status_old = relay_status;


		if (relay_high_time > WATERING_DURATION + 1000)
		{
			digitalWrite(PIN_MOTOR, LOW);
		}

		delay(100);
	}
	vTaskDelete(NULL);
}