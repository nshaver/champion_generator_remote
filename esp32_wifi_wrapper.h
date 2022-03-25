#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiAP.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>

Preferences preferences;
WebServer server(80);

String esp32_wifi_wrapper_lanssid="N/A";
String esp32_wifi_wrapper_lanhostname="N/A";
String esp32_wifi_wrapper_lanIP="N/A";

void esp32_wifi_wrapper_Serial(String s){
#ifdef ESP32_WIFI_WRAPPER_DEBUG
	Serial.println((String)"esp32_wifi_wrapper: " + s);
#endif
}

void esp32_wifi_wrapper_sendHtml(int retcode, String content){
	String o = "<!DOCTYPE html>";
	o += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
	o += "<html><body style='background-color:#FFFF66;font-family:courier,serif;'>\n";
	o += "<h1>" + esp32_wifi_wrapper_preferences_app + "</h1>";
	o += "Device SSID = " + esp32_wifi_wrapper_apssid + "<br>";
	o += "WLAN SSID = " + esp32_wifi_wrapper_lanssid + "<br>";
	o += "WLAN Hostname = " + esp32_wifi_wrapper_lanhostname+ "<br>";
	o += "WLAN IP = " + esp32_wifi_wrapper_lanIP+ "<br><br>";
	o += content;
	o += "\n</body></html>";
	server.send(retcode, "text/html", o);
}

void esp32_wifi_wrapper_handleScan(){
	String o="";
	o += "<h1>Wifi Scanner</h1>";

	int n=WiFi.scanNetworks();
	if (n==0){
		o += "No networks found<br>";
		o += "<a href='scan'>Scan again</a><br>";
		o += "<a href='/'>Return to main menu</a><br>";
	} else {
		o += (String)n + " networks found. Select a network to join:<br>";
		for (int i=0; i<n; ++i){
			o += (String)"<a href='join?ssid=" + WiFi.SSID(i) + "'>" + WiFi.SSID(i) + " (" + WiFi.RSSI(i) + ")</a><br>";
		}
	}
	esp32_wifi_wrapper_sendHtml(200, o);
}

void esp32_wifi_wrapper_handleReboot(){
	String o="Rebooting...";
	esp32_wifi_wrapper_sendHtml(200, o);
	delay(1000);
	ESP.restart();
}

void esp32_wifi_wrapper_handleUpdate(){
	String this_user="";
	String this_password="";
	String o="";
	o += "<h1>Update Firmware</h1>";
	if (server.method()==HTTP_POST){
		for (int i=0; i<server.args(); i++){
			if (server.argName(i)=="user"){
				this_user=server.arg(i);
			}
			if (server.argName(i)=="password"){
				this_password=server.arg(i);
			}
		}
	}

	if (this_user=="admin" && this_password=="admin"){
		// update received, apply update
		o += "<h2>Follow the instructions below to update the firmware on your device:</h2>";
		o += "<script src='https://ajax.googleapis.com/ajax/libs/jquery/3.2.1/jquery.min.js'></script>";
		o += "<form method='POST' action='#' enctype='multipart/form-data' id='upload_form'>";
		o += "1. Click here to browse for the firmware bin file: <input type='file' name='update'><br>";
		o += "2. After browsing for and selecting the firmware file click the update button to apply the update: ";
		o += "<input type='submit' value='Update'>";
		o += "</form><br><br>";
		o += "<div id='prgheader'></div><br>";
		o += "<div id='prg'></div>";
		o += "<script>";
		o += "$('form').submit(function(e){";
		o += "e.preventDefault();";
		o += "var form = $('#upload_form')[0];";
		o += "var data = new FormData(form);";
		o += " $.ajax({";
		o += "url: '/applyupdate',";
		o += "type: 'POST',";
		o += "data: data,";
		o += "contentType: false,";
		o += "processData:false,";
		o += "xhr: function() {";
		o += "$('#prgheader').html('Update Status: in progress, do not close this browser window until it finishes with 100% uploaded.');";
		o += "var xhr = new window.XMLHttpRequest();";
		o += "xhr.upload.addEventListener('progress', function(evt) {";
		o += "if (evt.lengthComputable) {";
		o += "var per = evt.loaded / evt.total;";
		o += "if (per>=1){";
		o += "$('#prgheader').html('Update Status: upload complete. Device will automatically reboot now, you can close this window.');";
		o += "}";
		o += "$('#prg').html('Upload progress: ' + Math.round(per*100) + '%');";
		o += "}";
		o += "}, false);";
		o += "return xhr;";
		o += "},";
		o += "success:function(d, s) {";
		o += "console.log('success!')";
		o += "},";
		o += "error: function (a, b, c) {";
		o += "}";
		o += "});";
		o += "});";
		o += "</script>";
	} else {
		// present password collection form
		o += "<h2>You Must Login To Update Firmware:</h2>";
		o += "<form name='loginForm' method=POST>";
		o += "Username:";
		o += "<input type='text' size=25 name='user'><br>";
		o += "Password:";
		o += "<input type='Password' size=25 name='password'><br><br>";
		o += "<input type='submit' value='Login'><br>";
		o += "</form>";
	}

	esp32_wifi_wrapper_sendHtml(200, o);
}

void esp32_wifi_wrapper_handleJoin(){
	String this_ssid="";
	String this_password="";
	String o="";
	o += "<h1>Join Network</h1>";
	if (server.method()==HTTP_GET){
		for (int i=0; i<server.args(); i++){
			if (server.argName(i)=="ssid"){
				this_ssid=server.arg(i);
			}
			if (server.argName(i)=="password"){
				this_password=server.arg(i);
			}
		}

		if (this_ssid!=""){
			if (this_password!=""){
				// got both ssid and password, set preferences variables
				preferences.begin(esp32_wifi_wrapper_preferences_app.c_str(), false);
				preferences.putString("ssid", this_ssid);
				preferences.putString("password", this_password);
				preferences.end();

				o += "<pre>stored ssid=" + this_ssid + " and password to eeprom. rebooting in two seconds...</pre>";
				esp32_wifi_wrapper_sendHtml(200, o);
				delay(2000);

				// reboot
				ESP.restart();
			} else {
				// present password collection form
				o += "<form method=GET action='join'>";
				o += "ssid: " + this_ssid + "<input type=hidden name=ssid value='" + this_ssid + "'><br>";
				o += "password: <input type=text name=password><br>";
				o += "<input type=submit value='submit'>";
			}
		}
	}
	esp32_wifi_wrapper_sendHtml(200, o);
}

void esp32_wifi_wrapper_handleRoot(){
	String o = "";
	o += "<a href='scan'>Scan For Access Points</a><br>";
	o += "<a href='wipe'>Clear WiFi Credentials</a><br>";
	o += "<a href='update'>Upload New Firmware</a><br>";
	o += "<a href='reboot'>Reboot Device</a><br>";
#ifdef ESP32_WIFI_WRAPPER_ADDL_MENUS
	o += esp32_wifi_wrapper_addl_menus();
#endif
	esp32_wifi_wrapper_sendHtml(200, o);
}

void esp32_wifi_wrapper_clear_preferences(){
	// open prefs with app namespace=demo, in readwrite mode (2nd param=false)
	preferences.begin(esp32_wifi_wrapper_preferences_app.c_str(), false);
	preferences.remove("ssid");
	preferences.remove("password");
	preferences.end();
	esp32_wifi_wrapper_Serial("WiFi credentials wiped from memory...");
}

void esp32_wifi_wrapper_handleWipe(){
	esp32_wifi_wrapper_clear_preferences();

	String o = "";
	o += "WiFi credentials cleared. You will have to join the system's WiFi access point and scan for networks to reconnect to a network.";
	esp32_wifi_wrapper_sendHtml(200, o);
}

void esp32_wifi_wrapper_handleNotFound(){
	String o = "";
	o += "<pre>File Not Found\n\n";
	o += "URI: ";
	o += server.uri();
	o += "\nMethod: ";
	o += (server.method() == HTTP_GET) ? "GET" : "POST";
	o += "\nArguments: ";
	o += server.args();
	o += "\n";
	for (uint8_t i = 0; i < server.args(); i++) {
		o += " " + server.argName(i) + ": " + server.arg(i) + "\n";
	}
	o += "</pre>";

	esp32_wifi_wrapper_sendHtml(404, o);
}

void esp32_wifi_wrapper_handleClient(){
	server.handleClient();

	// press built-in PRG button to clear wifi credentials
  if(digitalRead(0) == LOW){
    esp32_wifi_wrapper_Serial("Wiping WiFi credentials from memory...");
    esp32_wifi_wrapper_clear_preferences();
  }
}

String esp32_wifi_wrapper_ipToString(IPAddress ip){
	String s="";
	for (int i=0; i<4; i++)
		s+=i ? "." + String(ip[i]) : String(ip[i]);
	return s;
}

void esp32_wifi_wrapper_connect_to_wifi(){
	String ssid;
	String password;

	// see if a ssid exists
	preferences.begin(esp32_wifi_wrapper_preferences_app.c_str(), true);
	ssid=preferences.getString("ssid", "");
	password=preferences.getString("password", "");
	preferences.end();

	if (ssid=="" || password==""){
		esp32_wifi_wrapper_Serial((String)"ssid+password not found in preferences");
	} else {
		esp32_wifi_wrapper_Serial((String)"found ssid=" + ssid + " in preferences");
		// connect to network
		WiFi.mode(WIFI_STA);
		WiFi.setAutoConnect(true);


		WiFi.begin(ssid.c_str(), password.c_str());

		delay(100);

		int c=0;
		while (c<10){
			if (WiFi.status()==WL_CONNECTED){
				if (!MDNS.begin(esp32_wifi_wrapper_aphostname.c_str())) { //http://esp32.local
					esp32_wifi_wrapper_Serial("Error setting up MDNS responder!");
				} else {
					MDNS.addService("http", "tcp", 80);
					esp32_wifi_wrapper_lanhostname=esp32_wifi_wrapper_aphostname;
				}

				IPAddress stIP = WiFi.localIP();
				esp32_wifi_wrapper_lanIP=esp32_wifi_wrapper_ipToString(stIP);
				esp32_wifi_wrapper_lanssid=ssid;
				esp32_wifi_wrapper_Serial((String)"connected to network, ssid=" + ssid + ", IP=" + esp32_wifi_wrapper_ipToString(stIP) + ", hostname=" + esp32_wifi_wrapper_aphostname);
				break;
			}
			delay(500);
			c++;
		}
		if (WiFi.status()!=WL_CONNECTED){
			esp32_wifi_wrapper_Serial((String)"giving up on network");
		}
	}
}

void esp32_wifi_wrapper_setup_handlers(){
	server.on("/", esp32_wifi_wrapper_handleRoot);
	server.on("/scan", esp32_wifi_wrapper_handleScan);
	server.on("/join", esp32_wifi_wrapper_handleJoin);
	server.on("/wipe", esp32_wifi_wrapper_handleWipe);

	server.onNotFound(esp32_wifi_wrapper_handleNotFound);

 /*return index page which is stored in serverIndex */
  server.on("/reboot", esp32_wifi_wrapper_handleReboot);
  server.on("/update", esp32_wifi_wrapper_handleUpdate);
  server.on("/applyupdate", HTTP_POST, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
		delay(1000);
    ESP.restart();
  }, []() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      Serial.printf("Update: %s\n", upload.filename.c_str());
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { //start with max available size
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      /* flashing firmware to ESP*/
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) { //true to set the size to the current progress
        Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
      } else {
        Update.printError(Serial);
      }
    }
  });
	esp32_wifi_wrapper_Serial("listening for firmware updates via /update");
}

void esp32_wifi_wrapper_server_begin(){
	server.begin();
}

void esp32_wifi_wrapper_setup(){
	// use builtin PRG button to wipe wifi credentials
  pinMode(0,INPUT);

	// setup access point
	WiFi.softAP(esp32_wifi_wrapper_apssid.c_str(), esp32_wifi_wrapper_appassword.c_str());
	IPAddress apIP = WiFi.softAPIP();
	esp32_wifi_wrapper_Serial((String)"created access point. ssid=" + esp32_wifi_wrapper_apssid + ", password=" + esp32_wifi_wrapper_appassword + ", IP=" + esp32_wifi_wrapper_ipToString(apIP));

	// attempt to connect to existing wifi network
	esp32_wifi_wrapper_connect_to_wifi();
	esp32_wifi_wrapper_setup_handlers();
}

