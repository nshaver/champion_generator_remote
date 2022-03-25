// Uncomment the following line to enable serial debug output
#define ENABLE_DEBUG 1

#ifdef ENABLE_DEBUG
  #define DEBUG_ESP_PORT Serial
  #define NODEBUG_WEBSOCKETS
  #define NDEBUG
	#define _SRP_LOGLEVEL_ 0
#endif

#include "Arduino.h"
#include <Wire.h>
#include "heltec.h"
#include <ESP32Encoder.h>
#define PIN_RFTX 2
#define PIN_ENC0 18
#define PIN_ENC1 5
#define PIN_ENCSW 17
#define PIN_BATTERY_MONITOR 37
#define PIN_DHT_SDA 19
#define PIN_DHT_SCL 23

int encoderMode=0;
enum encoderModes {
	ENC_NULL,
	ENC_SET_TEMP,
	ENC_SETTINGS,
	ENC_COUNT
};

int screenHighlight=0;
enum screenHighlights {
	SH_NULL,
	SH_SET_TEMP,
	SH_SETTINGS,
	SH_COUNT
};
int manualStart=0;
String manualStartMessage1="settings";
String manualStartMessage2="menu";
unsigned long lastStartStop=0;
String lastStartStopMessage="";
unsigned long nextAutoScreenUpdate=0;
unsigned long nextThermostat=0;
bool autoMode=false;
bool displayNetworkStatus=false;
unsigned long tempReadMs=15000;
unsigned long thermostatCheckMs=60000;
unsigned long batteryReadMs=10000;
unsigned long autoScreenUpdateMs=5000;
unsigned long autoSimpleDisplayMs=5000;
unsigned long nextSimpleDisplay=0;
bool simpleDisplay=false;
float lastSimpleDisplayTemp=0;
bool firstLoop=true;
bool sinricConnected=false;
bool sinricCredsFound=false;

// oled
bool updateScreen=false;

// lipo battery monitor
float BATTERY_MULTIPLIER= 0.002177;
// at .002177, reads 3.67, multimeter=3.57
// at .002177, reads 3.59, multimeter=3.48
// at .002177, reads 3.45, multimeter=3.35
float battery_v=0;
unsigned long nextBatteryRead=0;

// wifi
#define ESP32_WIFI_WRAPPER_DEBUG 1	// uncomment to send debug messages to Serial
#define ESP32_WIFI_WRAPPER_ADDL_MENUS 1 		// uncomment to look for function named esp32_wifi_wrapper_addl_menus()
// this function should be declared before including esp32_wifi_wrapper.h
// this function, along with defining ESP32_WIFI_WRAPPER_ADDL_MENUS, will allow the wifi wrapper to
// add additional menu items that relate to the custom device.
String esp32_wifi_wrapper_addl_menus(){
	String o = "";
	o += "<a href='gettemp'>Get Current Temp</a><br>";
	o += "<a href='sinric'>Enter Sinric Pro credentials</a><br>";
	return o;
}
String esp32_wifi_wrapper_apssid = "generatorremote";
String esp32_wifi_wrapper_appassword = "12345678";
String esp32_wifi_wrapper_aphostname = "generatorremote";
String esp32_wifi_wrapper_preferences_app = "generatorremote";
#include "esp32_wifi_wrapper.h"

// sinric pro
#include "SinricPro.h"
#include "SinricProThermostat.h"
// sinric pro for alexa/google home
String APP_KEY;
String APP_SECRET;
String THERMOSTAT_ID;
#define BAUD_RATE         115200


// dht12 temp sensor
#include <DHT12.h>
TwoWire I2Ctwo = TwoWire(1);
// for Heltec Wifi Kit 32, the i2c ports are weird.
// the DH12 i2c sensor works fine when you use alternate ports for i2c. They are specified here:
// I'm using SDA=19 and SCL=23
DHT12 dht(&I2Ctwo, PIN_DHT_SDA, PIN_DHT_SCL);
unsigned long nextDht12ms=0;
float TempF=0;
bool globalPowerState;
float lastHumidity=0;
float lastHeatIndex=0;
float setTemp=72;
float displaySetTemp=72;
unsigned long lastTempReadMs=0;
unsigned long tempErrors=0;

// encoder
ESP32Encoder encoder;
long encoderValue=0;
long lastEncoderValue=0;
long lastEncoderCount=0;
unsigned long nextEncSwRead=0;
bool EncSwitchState=false;
bool LastEncSwitchState=false;

// 433mhz transmitter
#include <RCSwitch.h>
RCSwitch mySwitch = RCSwitch();

// mode=0 = receive
// mode=1 = transmit
int mode=1;
unsigned long nextTransmitms=0;

bool onPowerState(const String &deviceId, bool &state) {
  Serial.printf("Thermostat %s turned %s\r\n", deviceId.c_str(), state?"on":"off");
  globalPowerState = state;
	if (globalPowerState){
		// turn on
		updateScreen=true;
		switch_send(0, 0);
		lastStartStop=millis();
		lastStartStopMessage="alexa start";
	} else {
		// turn off
		updateScreen=true;
		autoMode=false;
		nextThermostat=millis();
		switch_send(0, 1);
		lastStartStop=millis();
		lastStartStopMessage="alexa stop";
	}
  return true; // request handled properly
}

bool onTargetTemperature(const String &deviceId, float &temperature) {
  Serial.printf("Thermostat %s set temperature to %f\r\n", deviceId.c_str(), temperature);
	autoMode=true;
	lastStartStop=millis();
	lastStartStopMessage="alexa on";
  setTemp = temperature;
	nextThermostat=millis();
	updateScreen=true;
  return true;
}

bool onAdjustTargetTemperature(const String & deviceId, float &temperatureDelta) {
  setTemp += temperatureDelta;  // calculate absolut temperature
  Serial.printf("Thermostat %s changed temperature about %f to %f", deviceId.c_str(), temperatureDelta, setTemp);
  temperatureDelta = setTemp; // return absolut temperature
	autoMode=true;
	lastStartStop=millis();
	lastStartStopMessage="alexa on";
	nextThermostat=millis();
	updateScreen=true;
  return true;
}

bool onThermostatMode(const String &deviceId, String &mode) {
  Serial.printf("Thermostat %s set to mode %s\r\n", deviceId.c_str(), mode.c_str());
	if (mode=="COOL"){
  	Serial.println((String)"autoMode=" + autoMode);
		autoMode=true;
		nextThermostat=millis();
		updateScreen=true;
		lastStartStop=millis();
		lastStartStopMessage="alexa on";
	} else if (mode=="OFF"){
  	Serial.println((String)"autoMode=" + autoMode);
		autoMode=false;
		nextThermostat=millis();
		updateScreen=false;
		lastStartStop=millis();
		lastStartStopMessage="alexa off";
	}
  return true;
}

void esp32_wifi_wrapper_handleSinricForm(){
	String this_user="";
	String this_password="";
	String this_APP_KEY="";
	String this_APP_SECRET="";
	String this_THERMOSTAT_ID="";
	String o="";
	o += "<h1>Enter Senric Creditials</h1>";
	if (server.method()==HTTP_POST){
		for (int i=0; i<server.args(); i++){
			if (server.argName(i)=="user"){
				this_user=server.arg(i);
			}
			if (server.argName(i)=="password"){
				this_password=server.arg(i);
			}
			if (server.argName(i)=="key"){
				this_APP_KEY=server.arg(i);
			}
			if (server.argName(i)=="secret"){
				this_APP_SECRET=server.arg(i);
			}
			if (server.argName(i)=="thermostatid"){
				this_THERMOSTAT_ID=server.arg(i);
			}
		}
	}

	if (this_user=="admin" && this_password=="admin"){
		// store creditials to preferences
		preferences.begin(esp32_wifi_wrapper_preferences_app.c_str(), false);
		preferences.putString("APP_KEY", this_APP_KEY);
		preferences.putString("APP_SECRET", this_APP_SECRET);
		preferences.putString("THERMOSTAT_ID", this_THERMOSTAT_ID);
		preferences.end();

		o += "sinric pro credentials stored to device<br>device will reboot now";
		delay(1000);
		ESP.restart();
	} else {
		o += "invalid username/password";
	}
	esp32_wifi_wrapper_sendHtml(200, o);
}

void esp32_wifi_wrapper_handleSinric(){
	String o="";
	// present password collection form
	o += "<h2>You Must Login To Enter Sinric Credentials:</h2>";
	o += "<form action='sinricform' name='loginForm' method=POST>";
	o += "Username:";
	o += "<input type='text' size=25 name='user'><br>";
	o += "Password:";
	o += "<input type='Password' size=25 name='password'><br><br>";
	o += "Sinric Pro APP_KEY:";
	o += "<input type='text' size=25 name='key'><br><br>";
	o += "Sinric Pro APP_SECRET:";
	o += "<input type='text' size=25 name='secret'><br><br>";
	o += "Sinric Pro THERMOSTAT_ID:";
	o += "<input type='text' size=25 name='thermostatid'><br><br>";
	o += "<input type='submit' value='Login'><br>";
	o += "</form>";
	esp32_wifi_wrapper_sendHtml(200, o);
}

void esp32_wifi_wrapper_handleGetTemp(){
	unsigned long lastReadMs=millis() - lastTempReadMs;
	String o=(String)"last temp read was " + lastReadMs + "ms ago<br>last temp = " + int(setTemp) + "F";
	esp32_wifi_wrapper_sendHtml(200, o);
}

void setupSinricPro() {
	// get sinric pro credentials from preferences
	preferences.begin(esp32_wifi_wrapper_preferences_app.c_str(), true);
	APP_KEY=preferences.getString("APP_KEY", "");
	APP_SECRET=preferences.getString("APP_SECRET", "");
	THERMOSTAT_ID=preferences.getString("THERMOSTAT_ID", "");
	preferences.end();

	if (APP_KEY!="" && APP_SECRET!="" && THERMOSTAT_ID!=""){
		Serial.println("Sinric Pro credentials found:");
		Serial.println((String)"  Key=" + APP_KEY + ", THERMOSTAT_ID=" + THERMOSTAT_ID);
		sinricCredsFound=true;
		SinricProThermostat &myThermostat = SinricPro[THERMOSTAT_ID];
		myThermostat.onPowerState(onPowerState);
		myThermostat.onTargetTemperature(onTargetTemperature);
		myThermostat.onAdjustTargetTemperature(onAdjustTargetTemperature);
		myThermostat.onThermostatMode(onThermostatMode);

		// setup SinricPro
		SinricPro.onConnected([](){ 
			Serial.println("Connected to SinricPro"); 
			sinricConnected=true;
		});
		SinricPro.onDisconnected([](){ 
			Serial.println("Disconnected from SinricPro"); 
		});
		SinricPro.begin(APP_KEY, APP_SECRET);
	} else {
		Serial.println("Sinric Pro credentials not found in preferences. Use webapp to enter credentials");
	}
}

void setup(){
	Heltec.begin(true, false, true);
  Heltec.display->init();
  Heltec.display->flipScreenVertically();
	Serial.println("setup display");

	// setup battery monitor
	Serial.println("setup battery monitor");
	adcAttachPin(PIN_BATTERY_MONITOR);
	analogSetClockDiv(255); // 1338mS

	// setup wifi
	Serial.println("setup battery wifi");
	esp32_wifi_wrapper_setup();
	// add more webserver handlers here
	server.on("/gettemp", esp32_wifi_wrapper_handleGetTemp);
	server.on("/sinric", esp32_wifi_wrapper_handleSinric);
	server.on("/sinricform", esp32_wifi_wrapper_handleSinricForm);
	esp32_wifi_wrapper_server_begin();

	// transmit
	// Transmitter is connected to Arduino Pin #10  
	mySwitch.enableTransmit(PIN_RFTX);

	// Optional set pulse length.
	//mySwitch.setPulseLength(382);
	
	// Optional set protocol (default is 1, will work for most outlets)
	mySwitch.setProtocol(1);
	
	// Optional set number of transmission repetitions.
	mySwitch.setRepeatTransmit(12);

	// encoder
	Serial.println("setup encoder");
	pinMode(PIN_ENCSW, INPUT_PULLUP);
	// Enable the weak pull up resistors
	ESP32Encoder::useInternalWeakPullResistors=UP;
	encoder.attachFullQuad(PIN_ENC0, PIN_ENC1);
	// clear the encoder's raw count and set the tracked count to zero
	encoder.clearCount();
	encoderValue=0;
	lastEncoderValue=0;
	lastEncoderCount=0;

	// temp sensor
	Serial.println("setup dht temp sensor");
	dht.begin();

	// sinric pro
	Serial.println("setup sinric pro");
	setupSinricPro();

	nextSimpleDisplay=millis()+autoSimpleDisplayMs;
	Serial.println("setup complete");
}

int i=0;
unsigned long loopctr=0;

void switch_send(int rf_key, int rf_onoff) {
	// original remote on
	//mySwitch.setPulseLength(382);
  //mySwitch.send("011000010100110111010001");

	// replacement remote on
	//mySwitch.setPulseLength(370);
  //mySwitch.send("100000011101111110100001");

	if (rf_key==0){
		// invented replacement remote
		if (rf_onoff==0){
			// on
			mySwitch.setPulseLength(370);
			mySwitch.send("100000011101111110110001");
		} else if (rf_onoff==1){
			// off
			mySwitch.setPulseLength(370);
			mySwitch.send("100000011101111110110010");
		}
	}

	// original remote off
	//mySwitch.setPulseLength(382);
  //mySwitch.send("011000010100110111010010");

	// replacement remote off
	//mySwitch.setPulseLength(370);
  //mySwitch.send("100000011101111110100010");

}

static const char* bin2tristate(const char* bin);
static char * dec2binWzerofill(unsigned long Dec, unsigned int bitLength);

void output(unsigned long decimal, unsigned int length, unsigned int delay, unsigned int* raw, unsigned int protocol) {

  const char* b = dec2binWzerofill(decimal, length);
  Serial.print("Decimal: ");
  Serial.print(decimal);
  Serial.print(" (");
  Serial.print( length );
  Serial.print("Bit) Binary: ");
  Serial.print( b );
  Serial.print(" Tri-State: ");
  Serial.print( bin2tristate( b) );
  Serial.print(" PulseLength: ");
  Serial.print(delay);
  Serial.print(" microseconds");
  Serial.print(" Protocol: ");
  Serial.println(protocol);

  Serial.print("Raw data: ");
  for (unsigned int i=0; i<= length*2; i++) {
    Serial.print(raw[i]);
    Serial.print(",");
  }
  Serial.println();
  Serial.println();
}

static const char* bin2tristate(const char* bin) {
  static char returnValue[50];
  int pos = 0;
  int pos2 = 0;
  while (bin[pos]!='\0' && bin[pos+1]!='\0') {
    if (bin[pos]=='0' && bin[pos+1]=='0') {
      returnValue[pos2] = '0';
    } else if (bin[pos]=='1' && bin[pos+1]=='1') {
      returnValue[pos2] = '1';
    } else if (bin[pos]=='0' && bin[pos+1]=='1') {
      returnValue[pos2] = 'F';
    } else {
      return "not applicable";
    }
    pos = pos+2;
    pos2++;
  }
  returnValue[pos2] = '\0';
  return returnValue;
}

static char * dec2binWzerofill(unsigned long Dec, unsigned int bitLength) {
  static char bin[64];
  unsigned int i=0;

  while (Dec > 0) {
    bin[32+i++] = ((Dec & 1) > 0) ? '1' : '0';
    Dec = Dec >> 1;
  }

  for (unsigned int j = 0; j< bitLength; j++) {
    if (j >= bitLength - i) {
      bin[j] = bin[ 31 + i - (j - (bitLength - i)) ];
    } else {
      bin[j] = '0';
    }
  }
  bin[bitLength] = '\0';

  return bin;
}

void process_encoder(){
	// encoder
	long thisEncoder=encoder.getCount();
	if (lastEncoderCount!=thisEncoder){
		encoderValue=thisEncoder/4;

		if (lastEncoderValue!=encoderValue){
			long encoderDiff=encoderValue-lastEncoderValue;
			// encoder increment/decrement
			if (encoderMode==ENC_NULL){
				// not setting any values, highlight next section
				screenHighlight=screenHighlight+encoderDiff;
				if (screenHighlight>=SH_COUNT) screenHighlight=0;
				else if (screenHighlight<0) screenHighlight=SH_COUNT-1;
				updateScreen=true;
			} else if (encoderMode==ENC_SET_TEMP){
				displaySetTemp=displaySetTemp+encoderDiff;
				updateScreen=true;
			} else if (encoderMode==ENC_SETTINGS){
				displayNetworkStatus=false;
				if (encoderDiff>0) manualStart++;
				else manualStart--;
				if (manualStart>7) manualStart=0;
				if (manualStart<0) manualStart=7;
				if (manualStart==0){
					manualStartMessage1="exit";
					manualStartMessage2="menu";
				} else if (manualStart==1){
					manualStartMessage1="manual";
					manualStartMessage2="start";
				} else if (manualStart==2){
					manualStartMessage1="manual";
					manualStartMessage2="stop";
				} else if (manualStart==3){
					manualStartMessage1="auto";
					manualStartMessage2="enable";
				} else if (manualStart==4){
					manualStartMessage1="auto";
					manualStartMessage2="disable";
				} else if (manualStart==5){
					manualStartMessage1="network";
					manualStartMessage2="status";
				} else if (manualStart==6){
					manualStartMessage1="factory";
					manualStartMessage2="reset";
				} else if (manualStart==7){
					manualStartMessage1="reboot";
					manualStartMessage2="device";
				}
				updateScreen=true;
			}
			lastEncoderValue=encoderValue;
			Serial.println((String)"Encoder value = " + encoderValue);
		}
		lastEncoderCount=thisEncoder;
	}
	if (millis()>=nextEncSwRead){
		EncSwitchState=digitalRead(PIN_ENCSW);
		if (EncSwitchState!=LastEncSwitchState){
			if (EncSwitchState){
				Serial.println((String)"Button Released");
			} else {
				Serial.println((String)"Button Pressed");
				if (encoderMode==ENC_NULL){
					if (screenHighlight==SH_SET_TEMP){
						screenHighlight=SH_NULL;
						encoderMode=ENC_SET_TEMP;
						displaySetTemp=setTemp;
						updateScreen=true;
					} else if (screenHighlight==SH_SETTINGS){
						screenHighlight=SH_NULL;
						encoderMode=ENC_SETTINGS;
						if (autoMode){
							manualStart=4;
							manualStartMessage1="auto";
							manualStartMessage2="disable";
						} else {
							manualStart=1;
							manualStartMessage1="manual";
							manualStartMessage2="start";
						}
						updateScreen=true;
					}
				} else if (encoderMode==ENC_SET_TEMP){
					// exit set temp
					if (setTemp!=displaySetTemp){
						setTemp=displaySetTemp;
						if (sinricCredsFound){
							SinricProThermostat &myThermostat = SinricPro[THERMOSTAT_ID];
							if (myThermostat.sendTargetTemperatureEvent(setTemp, "PHYSICAL_INTERACTION")){
								Serial.println((String)"sent sendTargetTemperatureEvent(" + setTemp + ") sent to sinric");
							} else {
								Serial.println((String)"error sending to sinric: sendTargetTemperatureEvent(" + setTemp + ")");
							}
						}
					}

					// whenever changing temp, need to quickly recheck if generator needs turned on or off
					nextThermostat=millis();

					encoderMode=ENC_NULL;
					screenHighlight=SH_NULL;
					updateScreen=true;
				} else if (encoderMode==ENC_SETTINGS){
					if (manualStart==0){
						// leaving manual start mode
						encoderMode=ENC_NULL;
						screenHighlight=SH_NULL;
						manualStartMessage1="settings";
						manualStartMessage2="menu";
						updateScreen=true;
					} else if (manualStart==1){
						// start generator
						manualStartMessage1="manual";
						manualStartMessage2="starting";
						updateScreen=true;
						switch_send(0, 0);
						lastStartStop=millis();
						lastStartStopMessage="man started";
					} else if (manualStart==2){
						// stop generator
						manualStartMessage1="manual";
						manualStartMessage2="stopping";
						updateScreen=true;
						switch_send(0, 1);
						lastStartStop=millis();
						lastStartStopMessage="man stopped";
					} else if (manualStart==3){
						// turn on thermostat control
						autoMode=true;

						// notify Sinric
						if (sinricCredsFound){
							SinricProThermostat &myThermostat = SinricPro[THERMOSTAT_ID];
							if (myThermostat.sendThermostatModeEvent("COOL", "PHYSICAL_INTERACTION")){
								Serial.println((String)"sent sendThermostatModeEvent(COOL) sent to sinric");
							} else {
								Serial.println("error sending to sinric: sendThermostatModeEvent(COOL)");
							}
						}

						encoderMode=ENC_NULL;
						screenHighlight=SH_NULL;
						
						// immediately return to main screen
						manualStartMessage1="settings";
						manualStartMessage2="menu";
						updateScreen=true;
					} else if (manualStart==4){
						// turn off thermostat control
						autoMode=false;

						// notify Sinric
						if (sinricCredsFound){
							SinricProThermostat &myThermostat = SinricPro[THERMOSTAT_ID];
							if (myThermostat.sendThermostatModeEvent("OFF", "PHYSICAL_INTERACTION")){
								Serial.println((String)"sent sendThermostatModeEvent(OFF) sent to sinric");
							} else {
								Serial.println("error sending to sinric: sendThermostatModeEvent(OFF)");
							}
						}

						encoderMode=ENC_NULL;
						screenHighlight=SH_NULL;
						manualStartMessage1="settings";
						manualStartMessage2="menu";
						updateScreen=true;
					} else if (manualStart==5){
						displayNetworkStatus=!displayNetworkStatus;
						updateScreen=true;
					} else if (manualStart==6){
						esp32_wifi_wrapper_clear_preferences();
						ESP.restart();
					} else if (manualStart==7){
						//reboot
						ESP.restart();
					}
				}
			}
			LastEncSwitchState=EncSwitchState;
		}
		nextEncSwRead=millis()+50;
	}
}

void process_Dht12(){
	if(millis()>nextDht12ms){
		// Read temperature as Fahrenheit (isFahrenheit = true)
		float c12 = dht.readTemperature();
		float f12 = 32+(c12*9/5);
		// Sensor readings may also be up to 2 seconds 'old' (its a very slow sensor)
		float h12 = dht.readHumidity();
		float hif12 = 0;

		bool updateHeatIndex=false;
		if (isnan(h12) || isnan(f12)) {
			tempErrors+=1;
		  Serial.println("Failed to read from DHT12 sensor!");
		} else {
			f12=int(f12+0.5);
			h12=int(h12+0.5);
			if (f12!=TempF){
				updateScreen=true;
				TempF=f12;
				Serial.println((String)"Temperature=" + int(TempF));
				lastTempReadMs=millis();
				// Compute heat index in Fahrenheit (the default)
				lastHeatIndex=hif12;
				updateHeatIndex=true;
				if (sinricCredsFound){
					SinricProThermostat &myThermostat = SinricPro[THERMOSTAT_ID];
					if (myThermostat.sendTemperatureEvent(TempF, lastHumidity, "PERIODIC_POLL")){
						Serial.println((String)"sent myThermostat.sendTemperatureEvent(" + TempF + ", " + lastHumidity + ") sent to sinric");
					} else {
						Serial.println((String)"error sending to sinric: myThermostat.sendTemperatureEvent(" + TempF + ", " + lastHumidity + ")");
					}
				}
			}
			if (h12!=lastHumidity){
				updateScreen=true;
				lastHumidity=h12;
				Serial.println((String)"lastHumidity=" + lastHumidity);
				updateHeatIndex=true;
			}
			if (updateHeatIndex){
				hif12 = dht.computeHeatIndex(f12, h12);
			}
			Serial.print("DHT12=> Temperature: ");
			Serial.print(f12);
			Serial.print(" *F \t");
			Serial.print("Humidity: ");
			Serial.print(h12);
			Serial.print(" %\t");
			Serial.print(" Heat index: ");
			Serial.print(lastHeatIndex);
			Serial.println(" *F");
		}
		nextDht12ms=millis()+tempReadMs;
	}
}

void process_thermostat(){
	if (autoMode){
		if (millis()>nextThermostat){
			if (TempF>setTemp){
				// turn on

				// don't check again for at least a minute
				nextThermostat=millis()+thermostatCheckMs;
				updateScreen=true;
				lastStartStop=millis();
				lastStartStopMessage="auto start";
				switch_send(0, 0);
			} else if (TempF<setTemp-1){
				// turn off
				// don't check again for at least a minute
				nextThermostat=millis()+thermostatCheckMs;
				updateScreen=true;
				lastStartStop=millis();
				lastStartStopMessage="auto stop";
				switch_send(0, 1);
			} else {
				// not turning on or off, set next time to check temp
				nextThermostat=millis()+tempReadMs;
			}
		}
	}
}

void process_battery_monitor(){
	if (millis()>nextBatteryRead){
		battery_v  =  analogRead(PIN_BATTERY_MONITOR)*BATTERY_MULTIPLIER;
		//Serial.println((String)"battery voltage: " + battery_v + "v");
		nextBatteryRead=millis()+batteryReadMs;
	}
}

void process_screen_update(){
	int this_window_left=0;
	int this_window_width=0;
	int this_window_top=0;
	int this_window_height=0;

	if (updateScreen){
		simpleDisplay=false;
		nextSimpleDisplay=millis()+autoSimpleDisplayMs;

		if (encoderMode==ENC_SETTINGS){
			if (displayNetworkStatus){
				Heltec.display->setFont(ArialMT_Plain_10);
				this_window_left=0;
				this_window_width=128;
				this_window_top=0;
				this_window_height=64;
				Heltec.display->setColor(WHITE);
				Heltec.display->fillRect(this_window_left, this_window_top, this_window_width, this_window_height);
				Heltec.display->setColor(BLACK);
				Heltec.display->drawRect(this_window_left, this_window_top, this_window_width, this_window_height);
				Heltec.display->setTextAlignment(TEXT_ALIGN_LEFT);
				Heltec.display->drawString(this_window_left+1, this_window_top+1, (String)"local ssid=" + esp32_wifi_wrapper_lanssid);
				Heltec.display->drawString(this_window_left+1, this_window_top+12, (String)"local IP=192.168.4.1");
				Heltec.display->drawString(this_window_left+1, this_window_top+23, (String)"WLAN ssid=" + esp32_wifi_wrapper_lanssid);
				Heltec.display->drawString(this_window_left+1, this_window_top+34, (String)"WLAN IP=" + esp32_wifi_wrapper_lanIP);
				Heltec.display->drawString(this_window_left+1, this_window_top+45, (String)"hostname=" + esp32_wifi_wrapper_lanhostname);
			} else {
				// settings menu
				Heltec.display->setFont(ArialMT_Plain_24);
				this_window_left=0;
				this_window_width=128;
				this_window_top=0;
				this_window_height=64;
				Heltec.display->setColor(WHITE);
				Heltec.display->fillRect(this_window_left, this_window_top, this_window_width, this_window_height);
				Heltec.display->setColor(BLACK);
				Heltec.display->drawRect(this_window_left, this_window_top, this_window_width, this_window_height);
				Heltec.display->setTextAlignment(TEXT_ALIGN_CENTER);
				Heltec.display->drawString(this_window_left+(this_window_width/2), this_window_top+1, (String)manualStartMessage1);
				Heltec.display->drawString(this_window_left+(this_window_width/2), this_window_top+30, (String)manualStartMessage2);
			}
		} else if (encoderMode==ENC_SET_TEMP){
			// settin temp
			Heltec.display->setFont(ArialMT_Plain_24);
			this_window_left=0;
			this_window_width=128;
			this_window_top=0;
			this_window_height=64;
			Heltec.display->setColor(BLACK);
			Heltec.display->fillRect(this_window_left, this_window_top, this_window_width, this_window_height);
			Heltec.display->setColor(WHITE);
			Heltec.display->drawRect(this_window_left, this_window_top, this_window_width, this_window_height);
			Heltec.display->setTextAlignment(TEXT_ALIGN_CENTER);
			if (autoMode){
				Heltec.display->drawString(this_window_left+(this_window_width/2), this_window_top, (String)"active");
			} else {
				Heltec.display->drawString(this_window_left+(this_window_width/2), this_window_top, (String)"inactive");
			}
			Heltec.display->drawString(this_window_left+(this_window_width/2), this_window_top+30, (String)int(displaySetTemp) + "°");
		} else {
			// current temp
			this_window_left=0;
			this_window_width=64;
			this_window_top=0;
			this_window_height=32;
			Heltec.display->setColor(BLACK);
			Heltec.display->fillRect(this_window_left, this_window_top, this_window_width, this_window_height);
			Heltec.display->setColor(WHITE);
			Heltec.display->drawRect(this_window_left, this_window_top, this_window_width, this_window_height);
			Heltec.display->setTextAlignment(TEXT_ALIGN_CENTER);
			Heltec.display->setFont(ArialMT_Plain_24);
			Heltec.display->drawString(this_window_left+(this_window_width/2), this_window_top, (String)int(TempF) + "°");

			// battery
			Heltec.display->setFont(ArialMT_Plain_10);
			Heltec.display->setTextAlignment(TEXT_ALIGN_RIGHT);
			Heltec.display->drawString(this_window_left+this_window_width-2, this_window_top+19, (String)battery_v + "v");

			// set temp
			Heltec.display->setFont(ArialMT_Plain_16);
			this_window_left=64;
			this_window_width=64;
			this_window_top=0;
			this_window_height=32;
			if (encoderMode==ENC_SET_TEMP){
				// setting temp, reverse colors
				Heltec.display->setColor(WHITE);
				Heltec.display->fillRect(this_window_left, this_window_top, this_window_width, this_window_height);
				Heltec.display->setColor(BLACK);
				Heltec.display->drawRect(this_window_left, this_window_top, this_window_width, this_window_height);
			} else {
				Heltec.display->setColor(BLACK);
				Heltec.display->fillRect(this_window_left, this_window_top, this_window_width, this_window_height);
				Heltec.display->setColor(WHITE);
				Heltec.display->drawRect(this_window_left, this_window_top, this_window_width, this_window_height);
			}
			Heltec.display->setTextAlignment(TEXT_ALIGN_CENTER);
			if (autoMode){
				Heltec.display->drawString(this_window_left+(this_window_width/2), this_window_top, (String)"active");
			} else {
				Heltec.display->drawString(this_window_left+(this_window_width/2), this_window_top, (String)"inactive");
			}
			Heltec.display->drawString(this_window_left+(this_window_width/2), this_window_top+14, (String)int(setTemp) + "°");

			if (encoderMode==ENC_NULL && screenHighlight==SH_SET_TEMP){
				// scrolled to set temp window
				Heltec.display->setColor(WHITE);
				Heltec.display->drawRect(this_window_left+1, this_window_top+1, this_window_width-2, this_window_top+this_window_height-2);
				Heltec.display->drawRect(this_window_left+2, this_window_top+1, this_window_width-4, this_window_top+this_window_height-2);
			}

			// settings menu
			Heltec.display->setFont(ArialMT_Plain_10);
			this_window_left=64;
			this_window_width=64;
			this_window_top=32;
			this_window_height=32;
			if (encoderMode==ENC_NULL){
				Heltec.display->setColor(BLACK);
				Heltec.display->fillRect(this_window_left, this_window_top, this_window_width, this_window_height);
				Heltec.display->setColor(WHITE);
				Heltec.display->drawRect(this_window_left, this_window_top, this_window_width, this_window_height);
			} else if (encoderMode==ENC_SETTINGS){
				// setting temp, reverse colors
				Heltec.display->setColor(WHITE);
				Heltec.display->fillRect(this_window_left, this_window_top, this_window_width, this_window_height);
				Heltec.display->setColor(BLACK);
				Heltec.display->drawRect(this_window_left, this_window_top, this_window_width, this_window_height);
			}
			Heltec.display->setTextAlignment(TEXT_ALIGN_CENTER);
			Heltec.display->drawString(this_window_left+(this_window_width/2), this_window_top+1, (String)manualStartMessage1);
			Heltec.display->drawString(this_window_left+(this_window_width/2), this_window_top+15, (String)manualStartMessage2);

			if (encoderMode==ENC_NULL && screenHighlight==SH_SETTINGS){
				// scrolled to window
				Heltec.display->setColor(WHITE);
				Heltec.display->drawRect(this_window_left+1, this_window_top+1, this_window_width-2, this_window_height-2);
				Heltec.display->drawRect(this_window_left+2, this_window_top+1, this_window_width-4, this_window_height-2);
			}

			// messages window
			Heltec.display->setFont(ArialMT_Plain_10);
			this_window_left=0;
			this_window_width=64;
			this_window_top=32;
			this_window_height=32;
			Heltec.display->setColor(BLACK);
			Heltec.display->fillRect(this_window_left, this_window_top, this_window_width, this_window_height);
			Heltec.display->setColor(WHITE);
			Heltec.display->drawRect(this_window_left, this_window_top, this_window_width, this_window_height);
			Heltec.display->setTextAlignment(TEXT_ALIGN_CENTER);
			if (lastStartStop>0){
				Heltec.display->drawString(this_window_left+(this_window_width/2), this_window_top+1, (String)lastStartStopMessage);
				long time_ago=(millis()-lastStartStop)/1000;
				String time_ago_period=" sec ago";
				if (time_ago>60){
					time_ago=time_ago/60;
					time_ago_period=" min ago";
				}
				Heltec.display->drawString(this_window_left+(this_window_width/2), this_window_top+15, (String)time_ago + time_ago_period);
			}
		}

		Heltec.display->display();
		updateScreen=false;
	} else if (millis()>nextSimpleDisplay){
		simpleDisplay=true;
	}
	if (simpleDisplay){
		if (lastSimpleDisplayTemp!=TempF){
			// update simple display of big temp
			this_window_left=0;
			this_window_width=128;
			this_window_top=0;
			this_window_height=64;
			Heltec.display->setColor(BLACK);
			Heltec.display->fillRect(this_window_left, this_window_top, this_window_width, this_window_height);
			Heltec.display->setColor(WHITE);
			Heltec.display->drawRect(this_window_left, this_window_top, this_window_width, this_window_height);
			Heltec.display->setTextAlignment(TEXT_ALIGN_CENTER);
			Heltec.display->setFont(ArialMT_Plain_16);
			Heltec.display->drawString(this_window_left+(this_window_width/2), this_window_top, (String)"Temperature");
			Heltec.display->setFont(ArialMT_Plain_24);
			Heltec.display->drawString(this_window_left+(this_window_width/2), this_window_top+30, (String)int(TempF) + "°");
			Heltec.display->display();
			lastSimpleDisplayTemp=TempF;
		}
	} else {
		lastSimpleDisplayTemp=0;
	}
}

void loop(){
	esp32_wifi_wrapper_handleClient();
	if (sinricCredsFound){
		SinricPro.handle();
	}
	process_Dht12();
	process_encoder();
	process_battery_monitor();
	process_thermostat();
	process_screen_update();
	if (simpleDisplay==false && lastStartStop>0){
		if (millis()>nextAutoScreenUpdate){
			updateScreen=true;
			nextAutoScreenUpdate=millis()+autoScreenUpdateMs;
		}
	}
	if (firstLoop && sinricConnected && sinricCredsFound){
		// send initial values to Sinric after connected
  	SinricProThermostat &myThermostat = SinricPro[THERMOSTAT_ID];
		String thisMode="OFF";
		if (autoMode) thisMode="COOL";
		if (myThermostat.sendThermostatModeEvent(thisMode, "PHYSICAL_INTERACTION")){
			Serial.println((String)"initial sendThermostatModeEvent(" + thisMode + ") sent to sinric");
		} else {
			Serial.println("error sending to sinric: sendThermostatModeEvent(" + thisMode + ")");
		}
		if (myThermostat.sendTargetTemperatureEvent(setTemp, "PHYSICAL_INTERACTION")){
			Serial.println((String)"initial sendTargetTemperatureEvent(" + setTemp + ") sent to sinric");
		} else {
			Serial.println((String)"error sending to sinric: sendTargetTemperatureEvent(" + setTemp + ")");
		}
		if (myThermostat.sendTemperatureEvent(TempF, lastHumidity, "PERIODIC_POLL")){
			Serial.println((String)"initial myThermostat.sendTemperatureEvent(" + TempF + ", " + lastHumidity + ") sent to sinric");
		} else {
			Serial.println((String)"error sending to sinric: myThermostat.sendTemperatureEvent(" + TempF + ", " + lastHumidity + ")");
		}
		firstLoop=false;
	}
}

