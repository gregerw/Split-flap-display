// (c) Walter Greger, internet.greger@me.com

//*****************************
//code for flap split master //
//*****************************

//libs
#include <Wire.h> //for i2c communication
#include <SoftwareSerial.h> //to talk to esp8266 via uart
#include "WiFiEsp.h" //ESP8266-ESP01 -> https://github.com/bportaluri/WiFiEsp
#include "WiFiEspUdp.h" //ESP8266-ESP01 -> https://github.com/bportaluri/WiFiEsp
#include <TimeLib.h>  //Time management incl. NTP sync -> https://github.com/PaulStoffregen/Time
#include <Timezone.h> //Timezone management -> https://github.com/JChristensen/Timezone/blob/master/src/Timezone.h

//Settings for ESP8266-ESP01
SoftwareSerial Serial1(2, 3); //UART to ESP8266
char ssid[] = "YOUR SSID";            // your network SSID (name)
char pass[] = "YOUR PASSWORD TO AP";        // your network password
#define baudRate 9600 //ESP8266 default is 115200. This produces communication issues. Set baudrate to 9600 via AT-command: "AT+UART_DEF=9600,8,1,0,0"
int status = WL_IDLE_STATUS;     // the Wifi radio's status

//Settings for connected units
int displayUnits[] = {1,2,3,4,5,6,7,8,9,10}; //array of i2c addresses; Index 0 will get first letter, index 1 the second,...
#define distributeUnitDelay 100 //delay between units for letter transmission in ms. default 500 ms, so max 16 devices parallel (15 rpm = 4s per rotation) due to power consupmtion -> max. 100w.

//NTP settings
char timeServer[] = "0.de.pool.ntp.org";//"time.nist.gov";  // NTP server
unsigned int localPortNPT = 2390;        // local port for ntp response  
const int NTP_PACKET_SIZE = 48;  // NTP timestamp is in the first 48 bytes of the message
const int UDP_TIMEOUT = 2000;    // timeout in miliseconds to wait for an UDP packet to arrive
byte packetBuffer[NTP_PACKET_SIZE]; // buffer to hold incoming and outgoing packets
const unsigned long intervallTimeSync = 60; //Time (in minutes) intervall where time is synchroized with NTP server
unsigned long lastTimeSync = 0; //last sync in absolute millis since start of device
String timeDateString;
#define offsetTimeString 0 //offset of units for displaying date and time, e.g. for 2, the time date will displayed on the third + x units
#define showDate false //show date and time. For false, only time is displayed

//Timezone
TimeChangeRule myDST = {"MEZ", Last, Sun, Mar, 2, 120};    // Summer time = UTC +2 hours
TimeChangeRule mySTD = {"MESZ", Last, Sun, Oct, 3, 60};     // Standard time = UTC +1 hours
Timezone myTZ(myDST, mySTD);
TimeChangeRule *tcr;
time_t localtime;

//Settings for UDP server for message transmission
WiFiEspUDP Udp; //udp instance
unsigned int localPort = 10002;  // local port to listen on  
char ReplyBuffer[] = "ACK";      // a string to send back to UDP client after message received
unsigned long showedMessageSince = 0; //last sync in absolute millis since last start
const unsigned long lifetimeMessage = 600; //UDP messages will be displayed for that time in (s) before switiching back to time & date
const unsigned long switchIntervallMessageToTime = 60; //intervall for switching between time and message until message lifetime is reached. For -1 message will be shown for the whole lifetime
bool timePresented = false; //true if time is shown; Needed for switching mode
unsigned long intervallTimer = 0; //timer to switch between messages and time
String currentMessage = ""; 
#define messageToken "AH6715" //Token for UDP message; All message without the token at beginning will be removed
#define messageOffTime 23 //after this hour, no more messages will be transfered on display
#define messageOnTime -1 //from this hour, messages will be transfered until off-time is reached; for always on, use -1

void setup() {
  //setup Serial
  Serial.begin(baudRate);
  Serial1.begin(baudRate);
  
  // initialize ESP module
  WiFi.init(&Serial1);
  // check for the presence of the shield
  if (WiFi.status() == WL_NO_SHIELD) {
    Serial.println("WiFi shield not present");
    // don't continue
    while (true);
  }

  // attempt to connect to WiFi network
  while ( status != WL_CONNECTED) {
    Serial.print("Attempting to connect to WPA SSID: ");
    Serial.println(ssid);
    // Connect to WPA/WPA2 network
    status = WiFi.begin(ssid, pass);
  }

  // you're connected now, so print out the data
  Serial.println("You're connected to the network");
  IPAddress ip = WiFi.localIP(); //show IP adress
  Serial.print("IP-Address:");
  Serial.println(ip);

  //start i2c
  Serial.println("starting i2c on master");
  Wire.begin();

  //start UDP server for NTP
  Udp.begin(localPortNPT);

  //start udp server for message transfer
  Serial.println("\nStarting connection to server...");
  Udp.begin(localPort);
  Serial.print("Listening on port ");
  Serial.println(localPort);
}

void loop() {
  //check if time sync with NTP is necessary
  if(lastTimeSync == 0 || (millis() - lastTimeSync > intervallTimeSync * 1000 * 60)) {
    Serial.println("1 in");
    Serial.println("get time");
    getTimeFromTimeserver();
    lastTimeSync = millis();
    Serial.println("1 out");
  }

  //check if message lifetime is reached
  if(showedMessageSince == 0 || (millis() - showedMessageSince >  lifetimeMessage* 1000)) {
    //lifetime of message is over
    Serial.println("2 in");
    currentMessage = "";
    distributeMessageToUnits(getTimeString()); //show time
    timePresented = true;
    Serial.println("message lifetime is over. switch to time and release message.");
    Serial.println("2 out");
  }
  else if((millis() - intervallTimer >  switchIntervallMessageToTime* 1000) && currentMessage.length() > 0 && !timePresented && currentMessage != "!demo!" && switchIntervallMessageToTime >0) { //check if switching intervall is reached and time should be presented
    //show time
    Serial.println("3 in");
    timePresented = true;
    Serial.println("switch to time");
    distributeMessageToUnits(getTimeString()); //show time
    Serial.println("3 out");
  }
  else if((millis() - intervallTimer >  switchIntervallMessageToTime* 1000) && currentMessage.length() > 0 && timePresented && currentMessage != "!demo!" && switchIntervallMessageToTime >0) { //check if switching intervall is reached and message should be presented
    //show message
    Serial.println("4 in");
    timePresented = false;
    Serial.println("switch to message");
    distributeMessageToUnits(currentMessage); //show time
    Serial.println("4 out");
  }
  // if there's data available, read a packet
  int packetSize = Udp.parsePacket();
  if (packetSize) {
    Serial.println("5 in");
    Serial.print("Received packet of size ");
    Serial.println(packetSize);
    Serial.print("From ");
    IPAddress remoteIp = Udp.remoteIP();
    Serial.print(remoteIp);
    Serial.print(", port ");
    Serial.println(Udp.remotePort());

    // read the packet into packetBufffer
    char packetBufferMessage[40];          // buffer for received udp message
    int len = Udp.read(packetBufferMessage, 40);
    if (len > 0) {
      packetBufferMessage[len] = 0;
    }    

    //check for on and off time
    if(((int)messageOnTime == -1) || ((hour(localtime) >= (int)messageOnTime)&&(hour(localtime) < (int)messageOffTime)) ) {
      Serial.println("inside on/off time. check for token...");
      //check if message has token at beginning
      String udpMessage = packetBufferMessage;
      //convert bytes string to utf8 string 
      String utfMessage = convertByteStringToUTFString(udpMessage);
      String token = messageToken;
      if(utfMessage.substring(0,token.length()) == token) {
        Serial.println("token ok");
        showedMessageSince = millis();
        //check for demo keyword !demo!
        if(utfMessage.substring(token.length()) == "!demo!") {
          currentMessage = "!demo!";
          showDemo();
          Serial.println("show demo");
        }
        else {
          Serial.println("distribute message to units");
          //send message to units
          timePresented = false;
          currentMessage = utfMessage.substring(token.length());
          distributeMessageToUnits(currentMessage); //send message to units
        }
      }
    }
    // send a reply, to the IP address and port that sent us the packet we received
    Serial.print("...sending back ACK:");
    Serial.print(Udp.remoteIP());
    Serial.print(":");
    Serial.println(Udp.remotePort());
    Udp.beginPacket(Udp.remoteIP(),Udp.remotePort()); //udpAnswerPort
    Udp.write(ReplyBuffer);
    Udp.endPacket();
    Serial.println("5 out");
  }
}


void getTimeFromTimeserver() {
  //send request to NTP server
  sendNTPpacket(timeServer);
  
  // wait for a reply for UDP_TIMEOUT miliseconds
  unsigned long startMs = millis();
  while (!Udp.available() && (millis() - startMs) < UDP_TIMEOUT) {}

  Serial.println(Udp.parsePacket());
  if (Udp.parsePacket()) {
    Serial.println("packet received");
    // We've received a packet, read the data from it into the buffer
    Udp.read(packetBuffer, NTP_PACKET_SIZE);

    // the timestamp starts at byte 40 of the received packet and is four bytes,
    // or two words, long. First, esxtract the two words:
    unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
    unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
    // combine the four bytes (two words) into a long integer
    // this is NTP time (seconds since Jan 1 1900):
    unsigned long secsSince1900 = highWord << 16 | lowWord;

    // now convert NTP time into everyday time:
    Serial.print("Unix time = ");
    // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
    const unsigned long seventyYears = 2208988800UL;
    // subtract seventy years:
    unsigned long epoch = secsSince1900 - seventyYears;
    // print Unix time:
    Serial.println(epoch);
    setTime(epoch);
    time_t utc = now();
    //set local time using the timezone settings
    localtime = myTZ.toLocal(utc, &tcr);
  }
}

// send an NTP request to the time server at the given address
void sendNTPpacket(char *ntpSrv)
{
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)

  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  Udp.beginPacket(ntpSrv, 123); //NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}

//this function splits the message in letters corresponding to the available units
void distributeMessageToUnits(String message) {
  message.toUpperCase(); //convert characters to capital characters
  intervallTimer = millis(); //reset intervall time
  //get amount of letter units
  size_t amountUnits = sizeof(displayUnits) / sizeof(int);
  int emptyUnits = amountUnits - message.length();
  for(int i = 0; i< emptyUnits; i++) {
    message.concat(" ");//fill messages with spaces in order to overwrite old messages
  } 
  int lastParseIndex = 0; //index for parsing the string into utf bytes
  byte bytes[message.length() + 1];
  message.getBytes(bytes, message.length() + 1);
  size_t amountBytes = sizeof(bytes) / sizeof(byte);

  //split message into utf letters with variable bytes
  for(int i = 0; i < amountUnits;i++) {
    for(int j = 0 + lastParseIndex; j < amountBytes-1; j++) {
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
      //Serial.print("begin i2c connection to:");
      //Serial.println(displayUnits[i]);
      for(int k = 0; k < charLength+1; k++) {
        Wire.write(aBuffer[k]);
        //Serial.print("write byte:");
        //Serial.println(aBuffer[k]);
      }
      Wire.endTransmission();
      if(message.indexOf("©") == -1) delay(distributeUnitDelay); //in case of no demo, the unit delay will be applied
      lastParseIndex = j + charLength;
      break;
    }
  }
}

String getTimeString() {
  //get time
  time_t utc = now(); //get utc time
  localtime = myTZ.toLocal(utc, &tcr); //convert to local time corresponding to timezone settings
  String timeString;
  
  //build offset
  for(int i = 0; i < offsetTimeString;i++) {
    //fill offset with spaces
    timeString.concat(" ");
  }
  
  //build date
  if(showDate) {
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
  size_t amountBytes = sizeof(bytes) / sizeof(byte);
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
    String utfLetter = aBuffer;
    utfMessage.concat(utfLetter);
    i = i + (charLength-1); //skip bytes corresponding to length of utf letter
  }
  Serial.print("utf message:");
  Serial.println(utfMessage);
  return utfMessage;
}

void showDemo() {
  //just a demo of the flap display. starting on the most signifikant flap, going through the available flaps. With a delay of one letter, the next display follows, etc.
  //character to trigger show is æ
  String triggerChar = "©";
  size_t amountDisplays = sizeof(displayUnits) / sizeof(int);
  //first show spaces   
  String zeroWord;
  for(int i = 0; i < amountDisplays; i++){
    zeroWord.concat(" ");
  }
  timePresented = false;
  
  distributeMessageToUnits(zeroWord);
  zeroWord = "";
  delay(5000);
  //now send in 1s intervall trigger to units
  for(int i = 0; i < amountDisplays; i++){
    String triggerWord;
      for(int j = 0; j < amountDisplays; j++){
        if(j <= i) triggerWord.concat(triggerChar);
        else triggerWord.concat(" ");
      }
      //Serial.print(triggerWord);
      distributeMessageToUnits(triggerWord);
      delay(1000);
  }
  
}
