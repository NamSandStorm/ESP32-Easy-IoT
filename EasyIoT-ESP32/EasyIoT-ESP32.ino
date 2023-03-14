/* [General & Notes]: ------------------------------------------------------------------------------------- */ 
  /*   [License]: ----------------------------------------------------------------------------------------- */
    /*
      MIT License

      Copyright (c) 2021-2023 FlexTech Engineering CC & Kai Eysselein

      Permission is hereby granted, free of charge, to any person obtaining a copy
      of this software and associated documentation files (the "Software"), to deal
      in the Software without restriction, including without limitation the rights
      to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
      copies of the Software, and to permit persons to whom the Software is
      furnished to do so, subject to the following conditions:

      The above copyright notice and this permission notice shall be included in
      all copies or substantial portions of the Software.

      THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
      IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
      FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
      AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
      LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
      OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
      THE SOFTWARE.
    */
  /*   [CAUTION !!!]: ------------------------------------------------------------------------------------- */
    /*
      Always change the usernames and passwords to avoid the related risks !!!
      This code is generic and the default settings will expose the device to various risks !!!    
    */
  /*   [Notes]: ------------------------------------------------------------------------------------------- */
    /*
      1. before compiling ensure the following are installed
        1.1 WiFiManager   			  		  https://github.com/tzapu/WiFiManager
        1.2 MQTT						            https://github.com/knolleary/pubsubclient
        1.3 ArduinoJSON                 https://github.com/bblanchon/ArduinoJson    
        1.4 Timer Interrupts			      https://github.com/khoih-prog/ESP32TimerInterrupt
        1.5 NTP time                    https://github.com/arduino-libraries/NTPClient
        1.6 Async web server			      https://github.com/me-no-dev/ESPAsyncWebServer
        1.7 Required for WiFi Manager	  https://github.com/me-no-dev/AsyncTCP
        
      2. MQTT and Console Commands (first login via the command below)
        2.1 Console Topic on which the system is listening:
          /console
          2.2.1 Public Commands:
            hello
          2.3.2 Admin Commands: 
            2.3.2.1 User Login:
              user login [user] [password] 
              -> default admin: is admin admin
              -> default user: is user user
            2.3.2.2 User Logout
              user logout      
            2.3.2.3 Restart 
              restart
            2.3.2.4 Get IP address
              ip
            2.3.2.5 Get Firmware version
              version
            2.3.2.6 Flash Firmware
              firware update [path to firmware binary]
            2.3.2.7 Firmware Rollback
              firmware rollback           
            2.3.3.8 preferences
              NOTE: - serialdiag and/or mqttdiag has to be enabled to see feedback.
                    - sensitive / potentionally compromising information may be sent via MQTT and posted on a topic for others to see
                Read
                  preferences read startcounter
                  preferences read serialdiagnostics
                  preferences read mqttdiagnostics
                  preferences read mqtthost
                  preferences read mqttport
                  preferences read mqttusername
                  preferences read mqttpassword
                  preferences read mqttrootpath
                  preferences read adminuser
                  preferences read adminpassword
                  preferences read all
            2.3.3.9 Enable Diagnostic Messages via MQTT
              mqttdiag
              mqttdiag set
              mqttdiag set persistent
            2.3.3.10 Clear WiFi Settings
              wifisettings cear
            2.3.3.11 Number of starts of device
              starts
    */
/* [Includes / Libraries]: -------------------------------------------------------------------------------- */
  /* [Operating System Level]: ---------------------------------------------------------------------------- */
    #include "Arduino.h"              
    #include "WiFiManager.h"          
    #include <PubSubClient.h>         
    #include <HTTPClient.h>           
    #include <Update.h>               
    #include <ArduinoJson.h>          
    #include <WiFiUdp.h>              
    #include <NTPClient.h>            
    #include <ESP.h>                  
    #include "esp_system.h"           
    #include "esp32-hal.h"
    #include "ESP32TimerInterrupt.h"  
    #include "SPIFFS.h"               
    #include <FS.h>
    #include <ESPAsyncWebServer.h>               
    #include <Preferences.h>
      Preferences preferences;     
/* [Generic Functions]: ----------------------------------------------------------------------------------- */
  /* [GF: remove text from a string]: --------------------------------------------------------------------- */
    String removeStringFromString (String sourceText, String removeString) {
      String cleanedText = "";
      for (int i = 0; i < sourceText.length(); i++) {
        if (sourceText.substring(i, i + removeString.length()) != removeString) {
          cleanedText += sourceText.charAt(i);
          i += removeString.length() - 1;
        }
      }
      return cleanedText;
    }
  /* [GF: Convert IP Address to String] ------------------------------------------------------------------- */
    String ip2String (const IPAddress& ipAddress){
      return String (ipAddress[0]) + String(".") + String (ipAddress[1]) + String(".") +\
      String (ipAddress[2]) + String(".") + String (ipAddress[3]); 
    }
/* [Network]: --------------------------------------------------------------------------------------------- */
  //required during first phase of Setup
  /* [Network: get ESP32 Efuse mac as string]: ------------------------------------------------------------ */
    String getESP32UID () {
      uint64_t chipid = ESP.getEfuseMac ();
      char uid[17];
      snprintf (uid, sizeof (uid), "%04X%08X", (uint16_t)(chipid>>32), (uint32_t)chipid);
      return String (uid);
    }
/* [Variables]: ------------------------------------------------------------------------------------------- */
  /* [V: Preferences, data that can be changed and stored]: ----------------------------------------------- */
    long totalStarts;
    bool messagingDiagSerial; //Diagnostic notifications
    bool messagingDiagMQTT; //Diagnostic notifications
    String adminUserName                    = "admin";
    String adminPassword                    = "adminpass";    
      
  /* [V: MQTT related]: ----------------------------------------------------------------------------------- */
    String MQTThost                         = "mozzie.iot.com.na";
    int MQTTport                            = 1883;
    String MQTTrootPath                     = "EasyIoT/";
    String MQTTuserName                     = "EasyIoT";
    String MQTTpassword                     = "EasyIoT";
  /* [V: Device / Server etc. Unique]: -------------------------------------------------------------------- */
    #define UIDproductName                   "EasyIoT-"    
    #define firmwareVersion                  "00.00.00.01"    
    #define joinWiFiSSID                     "Join your EasyIoT device to WiFi"
    #define joinWiFiPassword                 "password"
    #define timeZoneOffset                  0
  /* [V: Authentication and Security]: -------------------------------------------------------------------- */
    bool adminActive                        = false;
  /* [V: Environment and other]: -------------------------------------------------------------------------- */
    bool internetConnected                  = false;
    String UID                              = UIDproductName + getESP32UID();   //only works with ESP32
    String wiFiManagerHostName              = UID;
    int wiFiManagerConfigPortalTimeout      = 120;    //how long the connect portal should remain live
    String firmwareRollBackUrl              = "";
  /* [V: Detail Settings]: -------------------------------------------------------------------------------- */
    int bootDelay                           = 2;     //delay until loop can execute, avoids crazy initial readings
    float timer0Interval                    = 1.2;    //in seconds: may need to increase, depending on loop load
    float timer1Interval                    = 60;     //every in seconds:MQTT status update
  /* [V: Pin Assignments]: -------------------------------------------------------------------------------- */
    #define heartBeatLEDPin                   32  //Specific to M5Stack Stamp Pico
    #define resetButtonPin                    39  //Specific to M5Stack Stamp Pico, note that it default high 
  /* [V: MQTT]: --------------------------------------------------------------------------------------------*/
    String MQTTconsoleTopic                 = MQTTrootPath+UID + "/console";
    String MQTTwillTopic                    = MQTTrootPath+UID + "/online"; 
    int MQTTwillQoSstate                    = 0;
    bool MQTTwillRetain                     = true;  
    String MQTTwillMessage                  = "false";  

/* [Serial Monitor]: -------------------------------------------------------------------------------------- */
    void setupSerialMonitor (){
      Serial.begin(115200);
      delay(100);
    }
    void handleSerialMonitor (){
      String data = "";
      data = Serial.readString();
      if (data != ""){
        data.remove (data.length() - 1,1);
        handleInstruction (data);
      }
    }

/* [Spiffs]: ---------------------------------------------------------------------------------------------- */  
    void setupSPIFFS (){
      if(!SPIFFS.begin(true)){
        Serial.println("An Error has occurred while mounting SPIFFS");
      return;
      }      
    }
/* [Communication Functions]: ----------------------------------------------------------------------------- */
  /* [COMSF: WifiManager]: -------------------------------------------------------------------------------- */
    WiFiManager wm;             
    bool wmNonblocking = false;
    WiFiManagerParameter custom_field;
    void setupWiFiManager (){
      Serial.println ("WiFi config: starting...");
      WiFi.mode (WIFI_STA); 
      wm.setHostname (wiFiManagerHostName);
      Serial.setDebugOutput (true);  
      if (wmNonblocking) wm.setConfigPortalBlocking (false);
      std::vector<const char *> menu = {"wifi","sep","exit"};
      wm.setMenu (menu);
      wm.setClass ("invert");
      wm.setConfigPortalTimeout (wiFiManagerConfigPortalTimeout); // auto close configportal after n seconds
      bool res;
      res = wm.autoConnect (joinWiFiSSID,joinWiFiPassword); // password protected ap
      if (!res){
        Serial.println ("Failed to connect or hit timeout");
        internetConnected = false;
      } 
      else {
        Serial.println ("WiFi connected...");
        internetConnected = true;
      }
      Serial.println ("WiFi config: concluded");
    }
    void handleWiFiManager (){
      if (wmNonblocking){
        wm.process (); // avoid delays() in loop when non-blocking and other long running code  
      }
    }
    void clearWiFiManager (){
      wm.resetSettings ();
    }    
  /* [COMSF: AsyncWebServer]: ----------------------------------------------------------------------------- */
    AsyncWebServer server (80);
    const char* index_html = "<html><body><h1>This is a place holder, stay tuned</h1></body></html>";
    void setupAsyncWebServer (){
      server.on ("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send_P (200, "text/html", index_html);
      });
      server.on ("/console", HTTP_POST, [](AsyncWebServerRequest *request){
        String cmd = request->getParam ("cmd")->value();
        Serial.println (cmd);
        request->send (200, "text/plain", "OK");
      });
      server.begin();
    }    
  /* [COMSF: MQTT]: --------------------------------------------------------------------------------------- */
    WiFiClient MQTTclient;
    PubSubClient MQTT (MQTTclient);
    bool MQTTconnected = false;
    void reconnectMQTT () {
      while (!MQTT.connected ()) {
        if (MQTT.connect(UID.c_str(),MQTTuserName.c_str(),MQTTpassword.c_str(), MQTTwillTopic.c_str(), MQTTwillQoSstate, MQTTwillRetain, MQTTwillMessage.c_str())) {
          MQTT.subscribe (MQTTconsoleTopic.c_str());
        } 
        else {
        }
      }
    }
    void callbackMQTT (char* topic, byte* message, unsigned int length) {
      String messageTemp;
      for (int i = 0; i < length; i++) {
        messageTemp += (char)message[i];
      }
      if (String(topic) == MQTTconsoleTopic.c_str()) {
        evaluateMQTT (messageTemp);
      }
    }
    void sendMQTT (String channel = "", String message = ""){
      MQTT.publish(channel.c_str(), message.c_str());
    }
    void evaluateMQTT (String message){
      Serial.println("received mqtt message");
      Serial.print("sending message to be evaluated: ");
      Serial.println(message);
      if( message != ""){
        handleInstruction (message);
      }
    }
    void disconnectMQTT (){
      MQTT.disconnect();
    }
    void setupMQTT (){
      Serial.println("MQTT config: starting...");
      char Server[MQTThost.length() + 1];
      MQTThost.toCharArray(Server, sizeof(Server));       
      MQTT.setServer(Server, MQTTport);
      MQTT.setCallback(callbackMQTT);
      handleMQTT();
      sendMQTT(MQTTwillTopic, "true");
      Serial.println("MQTT config: complete");
    }
    void handleMQTT (){
      if (!MQTT.connected()) {
        reconnectMQTT();
      }
      MQTT.loop();
      if(MQTTconnected == false){
        MQTTconnected = true;
        Serial.println("MQTT config: concluded");
      }
      else{}
    }    
  /* [COMSF: Messaging]: ---------------------------------------------------------------------------------- */
    void sendMessage (String Message = "", String MessagePath = "", bool SysMessage = false){
      if(SysMessage){
        if (messagingDiagSerial){
          Serial.println (MessagePath + "/" + Message);
        }
        if (messagingDiagMQTT){
          String MQTTPathSend = MQTTrootPath+UID+MessagePath;
          sendMQTT(MQTTPathSend, Message);
        }
      }
      else{
        String MQTTPathSend = MQTTrootPath + UID + MessagePath;
        sendMQTT(MQTTPathSend, Message);      
        Serial.println(Message);
      }
    }
/* [User Functions]: -------------------------------------------------------------------------------------- */
  /* [U: User Login]: ------------------------------------------------------------------------------------- */
    void loginUser(String user, String password){
      sendMessage ("checking credentials","/console/",true); 
      if (user == adminUserName && password == adminPassword){
          adminActive = true;
          sendMessage ("login successful","/console/");  
      }
      else{
        sendMessage ("login failed","/console/");   
      }
    }
  /* [U: User Logout]: ------------------------------------------------------------------------------------ */
    void logoutUser(){
      adminActive = false;
      sendMessage ("logged out","/console/");  
    }
/* [Sensors]: --------------------------------------------------------------------------------------------- */
  /* [S: Reset Button]: ----------------------------------------------------------------------------------- */
    int resetButtonCounter = 0;
    void setupResetButton(){
      pinMode(resetButtonPin, INPUT);
    }
    void handleResetButton(){
      if (digitalRead(resetButtonPin) == LOW){
        resetButtonCounter++;                
      }
      else{
        resetButtonCounter = 0;
      }
      if (resetButtonCounter >= 3){
        sendMessage ("to default settings, including WiFi","/status/reset",false);
        clearWiFiManager();
        delay(200);
        preferences.begin("preferences", false);
        preferences.clear();
        adminActive=true;
        handleInstruction("var totalstarts write "+ String(totalStarts));
        adminActive=false;
        preferences.end();
        delay(200);
        restartDevice();
      }
    }
  /* [S: Default Sensor Functions]: ----------------------------------------------------------------------- */
    void setupSensors(){
      Serial.println("running setupResetButton ...");setupResetButton();Serial.println("... setupResetButton complete");      
    }
    void handleSensors(){
      handleResetButton();
    }
/* [Actuators]: ------------------------------------------------------------------------------------------- */
  /* [A: Heart Beat LED]: --------------------------------------------------------------------------------- */
    void setupHeartBeat(){
      pinMode(heartBeatLEDPin, OUTPUT);
    }
    void handleHeartBeat(){
      sendMessage ("triggering ...","/diagnostics/heartbeat",true);
      bool LEDstate = (digitalRead(heartBeatLEDPin));
      digitalWrite(heartBeatLEDPin,!LEDstate);      
    }
    void handleActuators(){
      handleHeartBeat();      
    }
  /* [A: Default Actuator Functions]: ----------------------------------------------------------------------- */
      void setupActuators(){
        Serial.println("running setupHeartBeat ...");setupHeartBeat();Serial.println("... setupHeartBeat complete");    
      }
/* [Timers]: ---------------------------------------------------------------------------------------------- */
  /* [T: Interrupt Timers]: ------------------------------------------------------------------------------- */
    #define _TimerINTERRUPT_LOGLEVEL_ 4 // To be included only in main(), .ino with setup() to avoid `Multiple Definitions` Linker Error you can't use Serial.print/println in ISR or crash.
    ESP32Timer timer0(0);
    ESP32Timer timer1(1);
    volatile bool timer0Triggered = false;
    volatile bool timer1Triggered = false;
    bool IRAM_ATTR timer0ISR(void * TimerNo){ 
      timer0Triggered = true;
      return true;
    }
    bool IRAM_ATTR timer1ISR(void * TimerNo){ 
      timer1Triggered = true;
      return true;
    } 
    void setupTimerInterrupt(){
      if (timer0.attachInterruptInterval(timer0Interval * 1000000, timer0ISR)){
        sendMessage ("complete", "/diagnostics/setup/timerinterrupt/timer0", true);
      }
      else{
        sendMessage ("failed", "/diagnostics/setup/timerinterrupt/timer0", true);
      }
      if (timer1.attachInterruptInterval(timer1Interval * 1000000, timer1ISR)){
        sendMessage ("complete", "/diagnostics/setup/timerinterrupt/timer1", true);
      }
      else{
        sendMessage ("failed", "/diagnostics/setup/timerinterrupt/timer1", true);
      }  
    }
    void handleTimerInterrupt(){
      if(timer0Triggered){
        sendMessage ("triggered", "/diagnostics/timerinterrupt/timer0", true);
        timer0Triggered = false;
        handleTimer0Payload();
      }
      if(timer1Triggered){
        sendMessage ("triggered", "/diagnostics/timerinterrupt/timer1", true);
        timer1Triggered = false;
        handleTimer1Payload();
      }
    }
    void handleTimer0Payload(){
      sendMessage ("started ...", "/diagnostics/timerinterrupt/timer0/payload", true);
      handleActuators();
      handleSensors();          
      handleSerialMonitor();
      handleMQTT();
      handleWiFiManager();
      handleJSONevents();
      sendMessage ("complete", "/diagnostics/timerinterrupt/timer0/payload", true);
    }
    void handleTimer1Payload(){ 
      sendMessage ("started ...", "/diagnostics/timerinterrupt/timer1/payload", true);
      handleJSONstatus();         
      sendMQTT(MQTTwillTopic, "true");
      sendMessage ("complete", "/diagnostics/timerinterrupt/timer1/payload", true);
    }
/* [Firmware Update] : ------------------------------------------------------------------------------------ */
  HTTPClient client;      
  int firmwareTotalLength;       //total size of firmware
  int firmwareCurrentLength = 0; //current size of written firmware
  void updateFirmware(uint8_t *data, size_t len){
    Update.write(data, len);
    firmwareCurrentLength += len;
    Serial.print('.');
    if(firmwareCurrentLength != firmwareTotalLength) return;
    Update.end(true);
  }
  void updateFirmwareWithFile(String firmwareHost){
    sendMessage ("attempting update: " + firmwareHost,"/firmware",true);  
    client.begin(firmwareHost);
    int resp = client.GET();
    if(resp == 200){
      firmwareTotalLength = client.getSize();
      int len = firmwareTotalLength;
      Update.begin(UPDATE_SIZE_UNKNOWN);
      uint8_t buff[128] = { 0 };
      WiFiClient * stream = client.getStreamPtr();
      while(client.connected() && (len > 0 || len == -1)) {
        size_t size = stream->available();
        if(size) {
          int c = stream->readBytes(buff, ((size > sizeof(buff)) ? sizeof(buff) : size));
          updateFirmware(buff, c);
            if(len > 0) {
              len -= c;
            }
          }
          delay(1);
        }
        sendMessage ("update successful, resarting... ","/firmware");  
      }
      else{
        sendMessage ("update not successful, error: "+String(resp),"/firmware");  
      }
      client.end();
    }
/* [Device Restart] : ------------------------------------------------------------------------------------- */
  void restartDevice(){
    sendMessage ("in 5","/status/restart",false);digitalWrite(heartBeatLEDPin,LOW);
    delay(1000);
    sendMessage ("in 4","/status/restart",false);
    delay(1000);
    sendMessage ("in 3","/status/restart",false);
    delay(1000);
    sendMessage ("in 2","/status/restart",false);
    delay(1000);
    sendMessage ("in 1","/status/restart",false);
    delay(1000);
    sendMessage ("forced restart initiated","/status/restart",false);digitalWrite(heartBeatLEDPin,HIGH);
    disconnectMQTT();
    ESP.restart();        
  }
/* [NTP Time] --------------------------------------------------------------------------------------------- */
  WiFiUDP ntpUDP;
  NTPClient timeClient(ntpUDP, "pool.ntp.org", timeZoneOffset * 60);
  void setupNTP(){
    timeClient.begin(); // initialize the time client
  }
/* [ArduinoJson] ------------------------------------------------------------------------------------------ */
    const size_t statusCapacity = JSON_OBJECT_SIZE(10);
    const size_t eventsCapacity = JSON_OBJECT_SIZE(2);
    DynamicJsonDocument JSONdocStatus(statusCapacity);
    DynamicJsonDocument JSONdocEvents(eventsCapacity);
    void handleJSONstatus(){
      //#1 source
        JSONdocStatus["src"] = UID;
      //#2 power cycles
        JSONdocStatus["pwrcyc"] = totalStarts;
      //#3 get time since startup
        JSONdocStatus["millis"] = millis();
      //#4 get timestamp
        timeClient.update();
        JSONdocStatus["ts"] = timeClient.getEpochTime();
      //#5 get free memory
        JSONdocStatus["free_heap"] = ESP.getFreeHeap();
      //#6 get total memory
        JSONdocStatus["total_heap"] = ESP.getHeapSize();
      //#7 get CPU temperature      
        JSONdocStatus["MCU_temp"] = temperatureRead();
      //convert to JSON
        String JSONstringStatus;
        serializeJson(JSONdocStatus, JSONstringStatus);
      //Send message
        sendMessage (JSONstringStatus, "/status/system");
    }
    void handleJSONevents(){
      //#1 get time since startup
        JSONdocEvents["millis"] = millis();
      //#2 get timestamp
        timeClient.update();
        JSONdocEvents["ts"] = timeClient.getEpochTime();
      //convert to JSON
        String JSONstringEvents;
        serializeJson(JSONdocEvents, JSONstringEvents);
      //Send message
        sendMessage (JSONstringEvents, "/events/reading");      
    }
    void setupJSON(){
      handleJSONstatus();
      handleJSONevents();
    }    
/* [Preferences]: ----------------------------------------------------------------------------------------- */
    void setupPreferences(){
      adminActive=true;        
      handleInstruction("var totalstarts read");
      ++totalStarts;
      handleInstruction("var totalstarts write "+ String(totalStarts));
      handleInstruction("var mqttdiag read");
      handleInstruction("var serialdiag read");
      handleInstruction("var adminusername read");
      handleInstruction("var adminpassword read");
      handleInstruction("var mqtthost read");
      handleInstruction("var mqttport read");
      handleInstruction("var mqttusername read");
      handleInstruction("var mqttpassword read");
      handleInstruction("var mqttrootpath read");
      adminActive=false;
    }    
/* [Instructions]: ---------------------------------------------------------------------------------------- */      
  String consoleCommand[6];
  void splitInstruction(String text,String separator=" "){
    //break up the instruction into max 6 parameters
    text.replace("\n","");
    int length = text.length();
    int consoleCommands = 0;
    consoleCommand[0] = "";
    String test = "";
    for (int i = 0; i < length; i++) {
      test = text.substring(i,i+1);
      if (test == separator){
        consoleCommands++;
        consoleCommand[consoleCommands] = "";
      }
      else {
        consoleCommand[consoleCommands] = consoleCommand[consoleCommands]+test;
      }
    }
  }
  void handleInstruction (String receivedInstruction){
    splitInstruction(receivedInstruction);
    if(receivedInstruction != "message received ..."){
      //clear old commands from console, important for public facing MQTT        
      sendMessage ("message received ...","/console");
    }
    if(!adminActive){      
      //no user logged in
      if (consoleCommand[0] == "hello"){
        sendMessage ("world","/console/"); 
      }
      if (consoleCommand[0] == "user"){
        if (consoleCommand[1] == "login"){
          loginUser(consoleCommand[2], consoleCommand[3]);
        }
      }
    }
    else {      
      //admin user logged in
      if (consoleCommand[0] == "user"){
        if (consoleCommand[1] == "logout"){
          logoutUser();
        }  
      }
      if (consoleCommand[0] == "millis"){
        //millis
        sendMessage (String(millis(),DEC),"/console/"); 
      }        
      if (consoleCommand[0] == "ip"){
        //get ip
        sendMessage (ip2String(WiFi.localIP()),"/console/"); 
      }
      if (consoleCommand[0] == "starts"){
        //number of starts of device
        sendMessage (String(totalStarts),"/console/");  
      }
      if (consoleCommand[0] == "restart"){
        //restart
        sendMessage ("restarting","/console/"); 
        restartDevice();
      }
      if (consoleCommand[0] == "firmware"){
        //firmware
        if (consoleCommand[1] == "version"){
          sendMessage (firmwareVersion,"/console/"); 
        }
        if (consoleCommand[1] == "update"){
          sendMessage ("Updating Firmware to: "+consoleCommand[2],"/console/"); 
          updateFirmwareWithFile(consoleCommand[2]);
          sendMessage ("Firmware update complete, restarting ..."+consoleCommand[2],"/console/"); 
          delay(1000);
          restartDevice();
        }
        if (consoleCommand[1] == "rollback"){
          sendMessage ("Restoring firmware to previous version ...","/console/firmware"); 
          updateFirmwareWithFile(firmwareRollBackUrl);
          sendMessage ("Firmware update complete, restarting ..."+consoleCommand[2],"/console/");
          delay(1000);
          restartDevice();
        }
      }
      if (consoleCommand[0] == "var"){
        preferences.begin("preferences", false);
        if (consoleCommand[1] == "mqttdiag"){
          //MQTT diagnostics on/off
          if (consoleCommand[2] == "get"){
            sendMessage (String(messagingDiagMQTT), "/console/var/mqtt diagnostics/get",true);              
          }
          if (consoleCommand[2] == "set"){
            if (consoleCommand[3] == "true" || consoleCommand[3] == "1"){
              messagingDiagMQTT = true;
            } else {
              messagingDiagMQTT = false;
            }
            sendMessage (String(messagingDiagMQTT), "/console/var/mqtt diagnostics/set",true);              
          }
          if (consoleCommand[2] == "read"){
              messagingDiagMQTT = preferences.getBool("mqttdiag",false);
              sendMessage (String(messagingDiagMQTT), "/console/var/mqtt diagnostics/read",true);              
          }                                    
          if (consoleCommand[2] == "write"){
            if (consoleCommand[3] == "true" || consoleCommand[3] == "1"){
              messagingDiagMQTT = true;
            } else {
              messagingDiagMQTT = false;
            }
            preferences.putBool("mqttdiag",messagingDiagMQTT);
            sendMessage (String(messagingDiagMQTT), "/console/var/mqtt diagnostics/write",true);
          }
          if (consoleCommand[2] == "default"){
            preferences.remove("mqttdiag");              
            sendMessage ("set to default, restart to apply!", "/console/var/mqttdiag/default",false);
          }
        }
        if (consoleCommand[1] == "serialdiag"){
          //Serial diagnostics on/off
          if (consoleCommand[2] == "get"){
            sendMessage (String(messagingDiagSerial), "/console/var/serial diagnostics/get",true);              
          }
          if (consoleCommand[2] == "set"){
            if (consoleCommand[3] == "true" || consoleCommand[3] == "1"){
              messagingDiagSerial = true;
            } else {
              messagingDiagSerial = false;
            }
            sendMessage (String(messagingDiagSerial), "/console/var/serial diagnostics/set",true);              
          }
          if (consoleCommand[2] == "read"){
              messagingDiagSerial = preferences.getBool("serialdiag",false);
              sendMessage (String(messagingDiagSerial), "/console/var/serial diagnostics/read",true);              
          }                                    
          if (consoleCommand[2] == "write"){
            if (consoleCommand[3] == "true" || consoleCommand[3] == "1"){
              messagingDiagSerial = true;
            } else {
              messagingDiagSerial = false;
            }
            preferences.putBool("serialdiag",messagingDiagSerial);
            sendMessage (String(messagingDiagSerial), "/console/var/serial diagnostics/write",true);
          }
          if (consoleCommand[2] == "default"){
            preferences.remove("serialdiag");              
            sendMessage ("set, requires restart", "/console/var/serialdiags/default",false);
          }
        }
        if (consoleCommand[1] == "totalstarts"){
          if (consoleCommand[2] == "get"){
            sendMessage (String(totalStarts), "/console/var/totalstarts/get",true);    
          }
          if (consoleCommand[2] == "set"){
            totalStarts = atol(consoleCommand[3].c_str());
            sendMessage (String(totalStarts), "/console/var/totalstarts/set",true);  
          }
                      
          if (consoleCommand[2] == "read"){
            totalStarts = preferences.getLong("totalStarts", 0);
            sendMessage (String(totalStarts), "/console/var/totalstarts/read",true);    
          }
          if (consoleCommand[2] == "write"){
            totalStarts = atol(consoleCommand[3].c_str());
            preferences.putLong("totalStarts",totalStarts);
            sendMessage (String(totalStarts), "/console/var/totalstarts/write",true);    
          }
          if (consoleCommand[2] == "default"){
            preferences.remove("totalStarts");              
            sendMessage ("set to default, restart to apply!", "/console/var/totalStarts/default",false);
          }
        }            
        if (consoleCommand[1] == "adminusername"){
          if (consoleCommand[2] == "get"){
            sendMessage (adminUserName, "/console/var/adminusername/get",true);
          }
          if (consoleCommand[2] == "set"){
            adminUserName = consoleCommand[3];
            sendMessage (adminUserName, "/console/var/adminusername/set",true);
          }
          if (consoleCommand[2] == "read"){
            adminUserName = preferences.getString("adminusername",adminUserName);
            sendMessage (adminUserName, "/console/var/adminusername/read",true);
          }
          if (consoleCommand[2] == "write"){
            adminUserName = consoleCommand[3];
            preferences.putString("adminusername",adminUserName);              
            sendMessage (adminUserName, "/console/var/adminusername/write",true);
          }
          if (consoleCommand[2] == "default"){
            preferences.remove("adminusername");              
            sendMessage ("set to default, restart to apply!", "/console/var/adminusername/default",true);
          }
        }
        if (consoleCommand[1] == "adminpassword"){
          if (consoleCommand[2] == "get"){
            sendMessage (adminPassword, "/console/var/adminpassword/get",true);
          }
          if (consoleCommand[2] == "set"){
            adminPassword = consoleCommand[3];
            sendMessage (adminPassword, "/console/var/adminpassword/set",true);
          }
          if (consoleCommand[2] == "read"){
            adminPassword = preferences.getString("adminpassword",adminPassword);
            sendMessage (adminPassword, "/console/var/adminpassword/read",true);
          }
          if (consoleCommand[2] == "write"){
            adminPassword = consoleCommand[3];
            preferences.putString("adminpassword",adminPassword);              
            sendMessage (adminPassword, "/console/var/adminpassword/write",true);
          }
          if (consoleCommand[2] == "default"){
            preferences.remove("adminpassword");              
            sendMessage ("set to default, restart to apply!", "/console/var/adminpassword/default",true);
          }
        }
        if (consoleCommand[1] == "mqtthost"){
          if (consoleCommand[2] == "get"){
            sendMessage (MQTThost, "/console/var/mqtthost/get",true);
          }
          if (consoleCommand[2] == "set"){
            MQTThost = consoleCommand[3];
            sendMessage (MQTThost, "/console/var/mqtthost/set",true);
          }
          if (consoleCommand[2] == "read"){
            MQTThost = preferences.getString("mqtthost",MQTThost);
            sendMessage (MQTThost, "/console/var/mqtthost/read",true);
          }
          if (consoleCommand[2] == "write"){
            MQTThost = consoleCommand[3];
            preferences.putString("mqtthost",MQTThost);  
            sendMessage (MQTThost, "/console/var/mqtthost/write",true);
          }
          if (consoleCommand[2] == "default"){
            preferences.remove("mqtthost");              
            sendMessage ("set to default, restart to apply!", "/console/var/mqtthost/default",true);
          }         
        }
        if (consoleCommand[1] == "mqttport"){
          if (consoleCommand[2] == "get"){
            sendMessage (String(MQTTport), "/console/var/mqttport/get",true);
          }
          if (consoleCommand[2] == "set"){
            MQTTport = atol(consoleCommand[3].c_str());
            sendMessage (String(MQTTport), "/console/var/mqttport/set",true);
          }
          if (consoleCommand[2] == "read"){
            MQTTport = preferences.getInt("mqttport",MQTTport);
            sendMessage (String(MQTTport), "/console/var/mqttport/read",true);
          }
          if (consoleCommand[2] == "write"){
            MQTTport = atol(consoleCommand[3].c_str());
            preferences.putInt("mqttport",MQTTport);  
            sendMessage (String(MQTTport), "/console/var/mqttport/write",true);
          }
          if (consoleCommand[2] == "default"){
            preferences.remove("mqttport");              
            sendMessage ("set to default, restart to apply!", "/console/var/mqttport/default",true);
          }         
        }
        if (consoleCommand[1] == "mqttusername"){
          if (consoleCommand[2] == "get"){
            sendMessage (MQTTuserName, "/console/var/mqttusername/get",true);
          }
          if (consoleCommand[2] == "set"){
            MQTTuserName = consoleCommand[3];
            sendMessage (MQTTuserName, "/console/var/mqttusername/set",true);
          }
          if (consoleCommand[2] == "read"){
            MQTTuserName = preferences.getString("mqttusername",MQTTuserName);
            sendMessage (MQTTuserName, "/console/var/mqttusername/read",true);
          }
          if (consoleCommand[2] == "write"){
            MQTTuserName = consoleCommand[3];
            preferences.putString("mqttusername",MQTTuserName);  
            sendMessage (MQTTuserName, "/console/var/mqttusername/write",true);
          }
          if (consoleCommand[2] == "default"){
            preferences.remove("mqttusername");              
            sendMessage ("set to default, restart to apply!", "/console/var/mqttusername/default",true);
          }         
        }
        if (consoleCommand[1] == "mqttpassword"){
          if (consoleCommand[2] == "get"){
            sendMessage (MQTTpassword, "/console/var/mqttpassword/get",true);
          }
          if (consoleCommand[2] == "set"){
            MQTTpassword = consoleCommand[3];
            sendMessage (MQTTpassword, "/console/var/mqttpassword/set",true);
          }
          if (consoleCommand[2] == "read"){
            MQTTpassword = preferences.getString("mqttpassword",MQTTpassword);
            sendMessage (MQTTpassword, "/console/var/mqttpassword/read",true);
          }
          if (consoleCommand[2] == "write"){
            MQTTpassword = consoleCommand[3];
            preferences.putString("mqttpassword",MQTTpassword);  
            sendMessage (MQTTpassword, "/console/var/mqttpassword/write",true);
          }
          if (consoleCommand[2] == "default"){
            preferences.remove("mqttpassword");              
            sendMessage ("set to default, restart to apply!", "/console/var/mqttpassword/default",true);
          }         
        }
        if (consoleCommand[1] == "mqttrootpath"){
          if (consoleCommand[2] == "get"){
            sendMessage (MQTTrootPath, "/console/var/mqttrootpath/get",true);
          }
          if (consoleCommand[2] == "set"){
            MQTTrootPath = consoleCommand[3];
            sendMessage (MQTTrootPath, "/console/var/mqttrootpath/set",true);
          }
          if (consoleCommand[2] == "read"){
            MQTTrootPath = preferences.getString("mqttrootpath",MQTTrootPath);
            sendMessage (MQTTrootPath, "/console/var/mqttrootpath/read",true);
          }
          if (consoleCommand[2] == "write"){
            MQTTrootPath = consoleCommand[3];
            preferences.putString("mqttrootpath",MQTTrootPath);  
            sendMessage (MQTTrootPath, "/console/var/mqttrootpath/write",true);
          }
          if (consoleCommand[2] == "default"){
            preferences.remove("mqttrootpath");              
            sendMessage ("set to default, restart to apply!", "/console/var/mqttrootpath/default",true);
          }         
        }
        preferences.end();
      }
    }
  }
/* [Default Arduino Functions]: --------------------------------------------------------------------------- */
  /* [DAF: Arduino Setup] : ------------------------------------------------------------------------------- */
    void setup(){
      setupSerialMonitor(); Serial.println("startup commencing ....");
      Serial.println("setupPreferences ...");setupPreferences(); Serial.println("... setupPreferences complete");
      Serial.println("running setupWiFiManager ...");setupWiFiManager();Serial.println("... setupWiFiManager complete");
      Serial.println("running setupMQTT ...");setupMQTT();Serial.println("... setupMQTT complete");
      Serial.println("running setupSPIFFS ...");setupSPIFFS();Serial.println("... setupSPIFFS complete");
      Serial.println("running setupTimerInterrupt ...");setupTimerInterrupt();Serial.println("... setupTimerInterrupt complete");
      Serial.println("running setupNTP ...");setupNTP();Serial.println("... setupNTP complete");
      Serial.println("running setupJSON ...");setupJSON();Serial.println("... setupJSON complete");
      Serial.println("running setupAsyncWebServer ...");setupAsyncWebServer();Serial.println("... setupAsyncWebServer complete");
      Serial.println("running setupSensors ...");setupSensors();Serial.println("... setupSensors() complete");
      Serial.println("running setupActuators ...");setupActuators();Serial.println("... setupActuators complete");
      Serial.println("running planned bootup delay to stabilise the system ...");while (bootDelay*1000>millis()){}Serial.println("... bootup delay complete");
      Serial.println("... startup complete");
    }
  /* [DAF: Arduino Loop] : -------------------------------------------------------------------------------- */
    void loop(){
      handleTimerInterrupt();
    }