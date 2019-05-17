// (c) Walter Greger, internet.greger@me.com

//*****************************
//code for split flap master //
//*****************************

//libs
#include <Arduino.h>
#include <Wire.h> //for i2c communication
#include <SoftwareSerial.h> //to talk to esp8266 via uart
#include "WiFiEsp.h" //ESP8266-ESP01 -> https://github.com/bportaluri/WiFiEsp
#include "WiFiEspUdp.h" //ESP8266-ESP01 -> https://github.com/bportaluri/WiFiEsp
#include <TimeLib.h>  //Time management incl. NTP sync -> https://github.com/PaulStoffregen/Time
#include <Timezone.h> //Timezone management -> https://github.com/JChristensen/Timezone/blob/master/src/Timezone.h

//Settings for ESP8266-ESP01
SoftwareSerial Serial1(3, 2); //UART to ESP8266
const char ssid[] = "XXXXX";            // your network SSID (name)
const char pass[] = "XXXXX";        // your network password
#define BAUDRATE 9600 //ESP8266 default is 115200. This produces communication issues. Set baudrate to 9600 via AT-command: "AT+UART_DEF=9600,8,1,0,0"
int status = WL_IDLE_STATUS;     // the Wifi radio's status
//WiFiEspClient client;
//char server[] = "arduino.cc";

//Settings for connected units
const int displayUnits[] = {8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27}; //array of i2c addresses; Index 0 will get first letter, index 1 the second,...
#define DISTRIBUTEUNITDELAY 0 //delay between units for letter transmission in ms.

//NTP settings
char timeServer[] = "0.de.pool.ntp.org";//"time.nist.gov";  // NTP server
const unsigned int localPortNPT = 2390;        // local port for ntp response  
const int NTP_PACKET_SIZE = 48;  // NTP timestamp is in the first 48 bytes of the message
const int UDP_TIMEOUT = 2000;    // timeout in miliseconds to wait for an UDP packet to arrive
byte packetBuffer[NTP_PACKET_SIZE]; // buffer to hold incoming and outgoing packets
const unsigned long intervallTimeSync = 60; //Time (in minutes) intervall where time is synchroized with NTP server
unsigned long lastTimeSync = 0; //last sync in absolute millis since start of device
String timeDateString;
#define OFFSETTIMESTRING 3 //offset of units for displaying date and time, e.g. for 2, the time date will displayed on the third + x units
bool showdate = true; //show date and time. For false, only time is displayed

//Timezone
TimeChangeRule myDST = {"MEZ", Last, Sun, Mar, 2, 120};    // Summer time = UTC +2 hours
TimeChangeRule mySTD = {"MESZ", Last, Sun, Oct, 3, 60};     // Standard time = UTC +1 hours
Timezone myTZ(myDST, mySTD);
TimeChangeRule *tcr;
time_t localtime;

//Settings for UDP server for message transmission
WiFiEspUDP Udp; //udp instance
const unsigned int localPort = 10002;  // local port to listen on  
const char ReplyBuffer[] = "ACK";      // a string to send back to UDP client after message received
unsigned displayState = 0; //states: 0: show nothing; 1: show time/date; 2: show message
unsigned long lastDisplayUpdate = 0;
const unsigned long displayUpdateInterval = 1; //update interval for display in s
unsigned long showedMessageSince = 0; //message presented since this time (absolute millis)
unsigned long lifetimeMessage = 60; //lifetime of message in s. if lifetime is reached, display will switch back to date/time display
bool showMultipleLines = true; //if true, message will be splitted in sub messages corresponding to amount of segments.
unsigned long showedLineSince = 0; //presented current line of message since this time (in absolute millis)
const unsigned long lifetimePerLine = 15; //lifetime of line in s. when reaching lifetime, display will show next line of message
int currentLineIndex = 0; //index of byte of current line
String currentMessage = ""; 
#define messageToken "XXXXX" //Token for UDP message; All message without the token at beginning will be removed
#define messageOffTime 23 //after this hour, no more messages will be transfered on display
#define messageOnTime -1 //from this hour, messages will be transfered until off-time is reached; for always on, use -1
bool switchmode = false; //switching continues between message and date/time, lifetime of message will be ignored

//function decleration
void getTimeFromTimeserver();
void sendNTPpacket(char *ntpSrv);
void distributeMessageToUnits(String message);
void getStatusOfUnit(int adress);
void overheatingAlarm(int adress);
void receiveFailure(int adress);
String getTimeString();
String fillDecimal(int value);
String convertByteStringToUTFString(String bytestringMessage);

void setup() {
  //setup Serial
  Serial.begin(BAUDRATE);
  Serial1.begin(BAUDRATE);
  
  // initialize ESP module
  WiFi.init(&Serial1);
  // check for the presence of the shield
  if (WiFi.status() == WL_NO_SHIELD) {
    Serial.println("wiFi shield not present");
    // don't continue
    while (true);
  }

  // attempt to connect to WiFi network
  while ( status != WL_CONNECTED) {
    Serial.print("attempting to connect to wpa SSID: ");
    Serial.println(ssid);
    status = WiFi.begin(ssid, pass); //connect to wpa/wpa2 network
  }

  Serial.println("you're connected to the network!");
  IPAddress ip = WiFi.localIP(); //show IP adress
  Serial.print("IP-Address:");
  Serial.println(ip);

  Serial.println("starting i2c on master");
  Wire.begin();  //start i2c
  Serial.println("starting udp server...");
  Udp.begin(localPortNPT); //start udp server for NTP
  Udp.begin(localPort); //start udp server for incoming messages
  Serial.print("listening on port ");
  Serial.println(localPort);

  //switch display state to time
  displayState = 1;

  //temp
  Serial.println("shut down display");
  //overheatingAlarm(8);
}

void loop() {
  //http request
    // if there are incoming bytes available
  // from the server, read them and print them

  //get time from timeserver with defined interval
  if(lastTimeSync == 0 || (millis() - lastTimeSync > intervallTimeSync * 1000 * 60)) {
    Serial.println("get time");
    getTimeFromTimeserver();
    lastTimeSync = millis();
  }

  //check for udp messages
  int packetSize = Udp.parsePacket();
  if (packetSize) {
    char packetBufferMessage[100]; // buffer for received udp message
    int len = Udp.read(packetBufferMessage, 100);
    if (len > 0) {
      packetBufferMessage[len] = 0;
    }  
    String udpMessage = packetBufferMessage;
    //convert bytes string to utf8 string 
    String utfMessage = convertByteStringToUTFString(udpMessage);
    String token = messageToken;
    if(utfMessage.substring(0,token.length()) == token) {
      Serial.println("token ok");
      //check for config commando
      String messageWOToken = utfMessage.substring(token.length());
      Serial.println(messageWOToken);
      if(messageWOToken == "!SWITCHMODE") {
        if(switchmode) {
          switchmode = false;
          currentMessage = "SWITCHMODE OFF";
          displayState = 3;
          showedMessageSince = millis();
        }
        else {
          switchmode = true;
          currentMessage = "SWITCHMODE ON";
          displayState = 3;
          showedMessageSince = millis();
        }
      }
      else if(messageWOToken == "!MULTILINE") {
        if(showMultipleLines) {
          showMultipleLines = false;
          currentMessage = "MULTLINES OFF";
          displayState = 3;
          showedMessageSince = millis();
        }
        else {
          showMultipleLines = true;
          currentMessage = "MULTILINES ON";
          displayState = 3;
          showedMessageSince = millis();
        }
      }
      else if(messageWOToken == "!SHOWDATE") {
        if(showdate) {
          showdate = false;
          currentMessage = "DATE OFF";
          displayState = 3;
          showedMessageSince = millis();
        }
        else {
          showdate = true;
          currentMessage = "DATE ON";
          displayState = 3;
          showedMessageSince = millis();
        }
      }
      else if(messageWOToken.startsWith("!LIFETIME")) {
        double lifetime = messageWOToken.substring(9).toDouble();
        lifetimeMessage = (long)lifetime;
        String answerString = "LIFETIME ";
        answerString += lifetime;
        currentMessage = answerString;
        displayState = 3;
        showedMessageSince = millis();
      }
      else {
        //send message to units
        currentMessage = messageWOToken;
        showedMessageSince = millis();
        showedLineSince = millis();
        currentLineIndex = 0;
        displayState = 2;
      }
    }
    Udp.beginPacket(Udp.remoteIP(),Udp.remotePort()); //udpAnswerPort
    Udp.write(ReplyBuffer);
    Udp.endPacket();
  }

  if(lastDisplayUpdate == 0 || (millis() - lastDisplayUpdate > displayUpdateInterval * 1000)) {
    lastDisplayUpdate = millis();
    //update display
    switch(displayState) {
      case 0:
        //show blank segments
        distributeMessageToUnits("");
        break;
      case 1:
        //show time
        distributeMessageToUnits(getTimeString());
        if(switchmode && (millis() - showedMessageSince >  2*lifetimeMessage* 1000)) {
          //switch back to message
          displayState = 2;
          showedMessageSince = millis();
        }
        break;
      case 2:
        //show message
        distributeMessageToUnits(currentMessage);
        //check if lifetime of message is over
        if(showedMessageSince == 0 || (millis() - showedMessageSince >  lifetimeMessage* 1000)) {
          if(!switchmode) currentMessage = "";
          currentLineIndex = 0;
          displayState = 1;
        }
        break;
      case 3:
        //config mode
        distributeMessageToUnits(currentMessage);
        if(showedMessageSince == 0 || (millis() - showedMessageSince >  10000)) {
          currentLineIndex = 0;
          displayState = 1;
          currentMessage = "";
        }
        break;
      default:
      //show blank segments
      distributeMessageToUnits("");
      break;
    }
  }
}

void getTimeFromTimeserver() {
  //send request to ntp server
  sendNTPpacket(timeServer);
  // wait for a reply for udp_timeout miliseconds
  unsigned long startMs = millis();
  while (!Udp.available() && (millis() - startMs) < UDP_TIMEOUT) {}

  Serial.println(Udp.parsePacket());
  if (Udp.parsePacket()) {
    Udp.read(packetBuffer, NTP_PACKET_SIZE); // we've received a packet, read the data from it into the buffer
    unsigned long highWord = word(packetBuffer[40], packetBuffer[41]); // the timestamp starts at byte 40 of the received packet and is four bytes,
    unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]); // or two words, long. first, esxtract the two words:
    unsigned long secsSince1900 = highWord << 16 | lowWord; // combine the four bytes (two words) into a long integer. this is ntp time (seconds since Jan 1 1900):
    Serial.print("unix time = ");
    const unsigned long seventyYears = 2208988800UL; // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
    unsigned long epoch = secsSince1900 - seventyYears; // subtract seventy years:
    Serial.println(epoch); // print unix time:
    setTime(epoch);
    time_t utc = now();
    localtime = myTZ.toLocal(utc, &tcr); //set local time using the timezone settings
  }
}

// send an NTP request to the time server at the given address
void sendNTPpacket(char *ntpSrv) {
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // initialize values needed to form ntp request
  packetBuffer[0] = 0b11100011;   // lI, version, mode
  packetBuffer[1] = 0;     // stratum, or type of clock
  packetBuffer[2] = 6;     // polling Interval
  packetBuffer[3] = 0xEC;  // peer clock precision
  // 8 bytes of zero for root delay & root dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;

  // all ntp fields have been given values, now
  // you can send a packet requesting a timestamp:
  Udp.beginPacket(ntpSrv, 123); //ntp requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}

//this function sends message to units
void distributeMessageToUnits(String message) {
  message.toUpperCase(); //convert characters to capital characters
  unsigned int lastParseIndex = 0;
  int amountUnits = sizeof(displayUnits) / sizeof(int);
  byte bytes[message.length() + 1];
  message.getBytes(bytes, message.length() + 1);
  unsigned int amountBytes = sizeof(bytes) / sizeof(byte);
  //if multiple lines are activated, get current line and
  if(showMultipleLines) lastParseIndex = currentLineIndex;
  else lastParseIndex = 0;

  //split message into utf letters with variable bytes
  for(int i = 0; i < amountUnits;i++) {
    if(lastParseIndex < amountBytes-1) {
      //there is a letter
      for(unsigned int j = 0 + lastParseIndex; j < amountBytes-1; j++) {
      int charLength = 1;
      if(bitRead(bytes[j],7)&&bitRead(bytes[j],6)) { //if true, utf char consists of at least two bytes, if false, letter consists of one byte
        charLength++;
        if(bitRead(bytes[j],5)) charLength++; //if true, utf char consists of at least three bytes
        if(bitRead(bytes[j],4)) charLength++; //if true, utf char consists of four bytes
      }
      byte aBuffer[charLength+1]; //length corresponding to amount of bytes + one byte for NULL termination of string
      for(int k = 0; k < charLength; k++) {
        aBuffer[k] = bytes[k+j];
      }
      aBuffer[charLength] = 0; //terminate string with NULL byte at end
      //sending byte to i2c
      Wire.beginTransmission(displayUnits[i]);
      for(int k = 0; k < charLength+1; k++) {
        Wire.write(aBuffer[k]);
      }
      Wire.endTransmission();
      //ask for unit for response (p, nil or o)
      getStatusOfUnit(displayUnits[i]);
      delay(DISTRIBUTEUNITDELAY);
      lastParseIndex = j + charLength;
      break;
    }
  }
  else {
      //no more letters for the unit, fill with spaces
      Wire.beginTransmission(displayUnits[i]);
      Wire.write((byte)32);
      Wire.write((byte)0);
      Wire.endTransmission();
      //ask for unit for response (p, nil or o)
      getStatusOfUnit(displayUnits[i]);
    }
  }

  //check if next line needs to be shown
  if((millis() - showedLineSince >  lifetimePerLine * 1000) && showMultipleLines) { //lifetime of line is reached, go to next line
    if(lastParseIndex >=message.length()) { 
      //no more characters, reset parseIndex
      currentLineIndex = 0;
      showedLineSince = millis();
    }
    else {
      //next line
      currentLineIndex = lastParseIndex;
      showedLineSince = millis();
    }
  }
}

void getStatusOfUnit(int adress) {
  //check for acknowledge and overheating of unit
  int answerBytes = Wire.requestFrom(adress,1);
  int readIndex = 0;
  char unitAnswer[1];
  bool receivedAnswer = false;
  
  while (Wire.available()) { 
    unitAnswer[readIndex]= Wire.read();
    readIndex++;
    receivedAnswer = true;
  }

  //check answer
  if(unitAnswer[0]=='P' && receivedAnswer); //received alive signal, nothing to do
  else if (unitAnswer[0]=='O' && receivedAnswer) overheatingAlarm(adress); //an overheating was detected
  else if (receivedAnswer) receiveFailure(adress); //answer but weather p or o
  else if (answerBytes != 1) receiveFailure(adress); //no response
}

void overheatingAlarm(int adress) {
  //an overheating was detected
  Serial.print("Unit ");
  Serial.print(adress);
  Serial.println(" sent an overheating alarm.");
  //put in some code to handle overheating, e.g. shutdown of display

}

void receiveFailure(int adress) {
  //unit didn't answer correctly
  Serial.print("Unit ");
  Serial.print(adress);
  Serial.println(" didn't send an P.");
  //put in some code to handle missing ACK

}

String getTimeString() {
  //get time
  time_t utc = now(); //get utc time
  localtime = myTZ.toLocal(utc, &tcr); //convert to local time corresponding to timezone settings
  String timeString;
  
  //build offset
  for(int i = 0; i < OFFSETTIMESTRING;i++) {
    //fill offset with spaces
    timeString.concat(" ");
  }
  
  //build date
  if(showdate) {
    int dateDay = day(localtime);
    int dateMonth = month(localtime);
    int dateYear = year(localtime);
    timeString.concat(dateDay);
    timeString.concat(".");
    timeString.concat(dateMonth);
    timeString.concat(".");
    timeString.concat(dateYear);
    timeString.concat(" ");
  }
  //build time
  int _h = hour(localtime);
  int _m = minute(localtime);
  timeString.concat(fillDecimal(_h));
  timeString.concat(":");
  timeString.concat(fillDecimal(_m));
  
  return timeString;
}

String fillDecimal(int value) {
  String _value;
  if(value < 10) {
    _value.concat("0");
    _value.concat(value);
  }
  else _value.concat(value);

  return _value;
}

String convertByteStringToUTFString(String bytestringMessage) {
  //convert to bytes
  byte bytes[bytestringMessage.length() + 1];
  bytestringMessage.getBytes(bytes, bytestringMessage.length() + 1);
  int amountBytes = sizeof(bytes) / sizeof(byte);
  String utfMessage;
  //go through the bytes and get the utf letter
  for(int i = 0; i < amountBytes-1; i++) {
    int charLength = 1;
    if(bitRead(bytes[i],7)&&bitRead(bytes[i],6)) { //if true, utf char consists of at least two bytes, if false, letter consists of one byte
      charLength++;
      if(bitRead(bytes[i],5)) charLength++; //if true, utf char consists of at least three bytes
      if(bitRead(bytes[i],4)) charLength++; //if true, utf char consists of four bytes
    }
    byte aBuffer[charLength+1]; //length corresponding to amount of bytes + one byte for NULL termination of string
    for(int j = 0; j < charLength; j++) {
      aBuffer[j] = bytes[i+j];
    }
    aBuffer[charLength] = 0; //terminate string with NULL byte at end
    String utfLetter = String((char*)aBuffer);
    utfMessage.concat(utfLetter);
    i = i + (charLength-1); //skip bytes corresponding to length of utf letter
  }
  return utfMessage;
}
