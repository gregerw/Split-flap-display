// (c) Walter Greger, internet.greger@me.com

//***************************
//code for split flap unit //
//***************************

//libs
#include <Arduino.h>
#include <Wire.h>
#include <Stepper.h>

//constants i2c
#define i2cAddress 8  //i2c address

//constants stepper
#define STEPPERPIN1 8
#define STEPPERPIN2 9
#define STEPPERPIN3 10
#define STEPPERPIN4 11
#define STEPS 2038 // 28BYJ-48, number of steps;
#define CALOFFSET 10 //needs to be calibrated for each unit
#define HALLPIN 5
#define AMOUNTFLAPS 45
#define STEPPERSPEED 15 //in rpm
//thermistor
#define THERMISTORPIN A0
float thermistorB = 3950;
#define CRITICALTEMPERATURE 50 
bool stepperOverheated = false;
//constants others
#define BAUDRATE 9600
#define ROTATIONDIRECTION 1 //-1 for reverse direction
#define OVERHEATINGTIMEOUT 2 //timeout in seconds to avoid overheating of stepper. After starting rotation, the counter will start. Stepper won't move again until timeout is passed
unsigned long lastRotation = 0;

//globals
String displayedLetter = " ";
String desiredLetter = " ";
const String letters[] = {" ","A","B","C","D","E","F","G","H","I","J","K","L","M","N","O","P","Q","R","S","T","U","V","W","X","Y","Z","Ä","Ö","Ü","0","1","2","3","4","5","6","7","8","9",":",".","-","?","!"};
Stepper stepper(STEPS, STEPPERPIN1,STEPPERPIN3,STEPPERPIN2,STEPPERPIN4); //stepper setup
bool lastInd1 = false; //store last status of phase
bool lastInd2 = false; //store last status of phase
bool lastInd3 = false; //store last status of phase
bool lastInd4 = false; //store last status of phase
float missedSteps = 0; //cummulate steps <1, to compensate via additional step when reaching >1
byte byteBufferI2C[4]; //buffer for I2C read. Should be max. 4 bytes (three bytes for unicode and one for stop byte)
int amountBytesI2C = 0; //amount of bytes to conevert
bool readBytesFinished = false; //if set to true, bytes will be converted to unicode letter inside loop

//function decleration
int calibrate();
void rotateToLetter(String toLetter);
float getTemperature();
void stopMotor();
void startMotor();
void receiveLetter(int amount);
void requestEvent(void);

//setup
void setup() {
  //initialize serial
  Serial.begin(BAUDRATE);

  //initialize i2c
  Serial.println("starting i2c slave");
  Wire.begin(i2cAddress); //i2c address of this unit 
  Wire.onReceive(receiveLetter); //call-function if for transfered letter via i2c 
  Wire.onRequest(requestEvent);

  //setup motor
  pinMode(HALLPIN, INPUT);
  calibrate(); //going to zero point
}

//loop
void loop() {
  //check if new bytes via I2C available for convert
  if(readBytesFinished) {
    //convert to utf string
    String utfLetter;
    //go through the bytes and get the utf letter
    for(int i = 0; i < amountBytesI2C; i++) {
      int charLength = 2; //at least two bytes including null termination
      if(bitRead(byteBufferI2C[i],7)&&bitRead(byteBufferI2C[i],6)) { //if true, utf char consists of at least two bytes + null termination
        charLength++;
        if(bitRead(byteBufferI2C[i],5)) charLength++; //if true, utf char consists of at least three bytes + null termination
        if(bitRead(byteBufferI2C[i],4)) charLength++; //if true, utf char consists of four bytes + null termination
      }
      byte aBuffer[charLength];
      for(int j = 0; j < charLength; j++) {
        aBuffer[j] = byteBufferI2C[i+j];
      }
      utfLetter = String((char*)aBuffer);
      i = i + (charLength-1); //skip bytes corresponding to length of utf letter
    }
    //convert small 'umlaute' to capital
    if(utfLetter == "ä") utfLetter = "Ä";
    if(utfLetter == "ö") utfLetter = "Ö";
    if(utfLetter == "ü") utfLetter = "Ü";
    desiredLetter = utfLetter;

    //reset values
    readBytesFinished = false;
    amountBytesI2C = 0;
    byteBufferI2C[0] = 0;
    byteBufferI2C[1] = 0;
    byteBufferI2C[2] = 0;
    byteBufferI2C[3] = 0;
    Serial.println("1 out");
  }
  //temp for test calibration settings
  /*
  String calLetters[10] = {" ","Z","A","U","N","!","0","1","2","9"};
  for(int i = 0; i < 10; i++) {
    String currentCalLetter = calLetters[i];
    rotateToLetter(currentCalLetter);
    Serial.print("Current motor temperature:");
    Serial.print(getTemperature());
    Serial.println(" °C");
    delay(5000);
  }
  */
  //end temp

  //check for overheated motor
  if(getTemperature() < CRITICALTEMPERATURE) {
    //temperature ok
    stepperOverheated = false;
    //check if currently displayed letter differs from desired letter  
    if(displayedLetter!=desiredLetter) rotateToLetter(desiredLetter);
    delay(100);
  }
  else {
    //overheating alarm
    stopMotor();
    stepperOverheated = true;
    Serial.print("ciritcal temperature reached. current temperature:");
    Serial.print(getTemperature());
    Serial.println(" °C. turn off motor.");
    delay(10000);
  }
}

//doing a calibration of the revolver using the hall sensor
int calibrate() {
  Serial.println("calibrate revolver");
  bool reachedMarker = false;
  stepper.setSpeed(STEPPERSPEED);
  int i = 0;
  while(!reachedMarker) {
    int currentHallValue = digitalRead(HALLPIN);
    if(currentHallValue == 1 && i == 0) { //already in zero position move out a bit and do the calibration {
      //not reached yet
      i = 50;
      stepper.step(ROTATIONDIRECTION * 50); //move 50 steps to get out of scope of hall
      }
    else if(currentHallValue == 1) {
      //not reached yet
      stepper.step(ROTATIONDIRECTION * 1);
    }
    else {
      //reached marker, go to calibrated offset position
      reachedMarker = true;
      stepper.step(ROTATIONDIRECTION * CALOFFSET);
      displayedLetter = " ";
      missedSteps = 0;
      stopMotor();
      return i;
    }
    if(i > 3 * STEPS) {
      //seems that there is a problem with the marker or the sensor. turn of the motor to avoid overheating.
      displayedLetter = " ";
      desiredLetter = " ";
      reachedMarker = true;
      Serial.println("calibration revolver failed");
      return -1;
    }
    i++;
  }
  return i;
}

//rotate to desired letter
void rotateToLetter(String toLetter) {
  if(lastRotation == 0 || (millis() - lastRotation > OVERHEATINGTIMEOUT * 1000)) {
    lastRotation = millis();
    //get letter position
    int posLetter = -1;
    int posCurrentLetter = -1;
    int amountLetters = sizeof(letters) / sizeof(String);
    for(int i = 0; i < amountLetters; i++) {
      //current char
      String currentSearchLetter = letters[i];
      if(toLetter == currentSearchLetter) posLetter = i;
      if(displayedLetter == currentSearchLetter) posCurrentLetter = i;
    }
    Serial.print("go to letter:");
    Serial.println(toLetter);
    //go to letter, but only if available (>-1)
    if(posLetter > -1) { //check if letter exists
      //check if letter is on higher index, then no full rotaion is needed
      if(posLetter >= posCurrentLetter) {
        Serial.println("direct");
        //go directly to next letter, get steps from current letter to target letter
        int diffPosition = posLetter - posCurrentLetter;
        startMotor();
        stepper.setSpeed(STEPPERSPEED);
        //doing the rotation letterwise
        for(int i=0;i<diffPosition;i++) {
          float preciseStep = (float)STEPS/(float)AMOUNTFLAPS;
          int roundedStep = (int)preciseStep;
          missedSteps = missedSteps + ((float)preciseStep - (float)roundedStep);
          if(missedSteps > 1) {
            roundedStep = roundedStep +1;
            missedSteps--;
          }
          stepper.step(ROTATIONDIRECTION*roundedStep);
        }
      }
      else {
        //full rotation is needed, good time for a calibration
        Serial.println("full rotation incl. calibration");
        calibrate();
        startMotor();
        stepper.setSpeed(STEPPERSPEED);
        for(int i=0;i<posLetter;i++) {
          float preciseStep = (float)STEPS/(float)AMOUNTFLAPS;
          int roundedStep = (int)preciseStep;
          missedSteps = missedSteps + (float)preciseStep - (float)roundedStep;
          if(missedSteps > 1) {
            roundedStep = roundedStep +1;
            missedSteps--;
          }
          stepper.step(ROTATIONDIRECTION*roundedStep);
        }
      }
      //store new position
      displayedLetter = toLetter;
      //rotation is done, stop the motor
      delay(100); //important to stop rotation before shutting of the motor to avoid rotation after switching off current
      stopMotor();
    }
    else {
      Serial.println("letter unknown, go to space");
      desiredLetter = " ";
    }
  }
}

//temperature of motor
float getTemperature() {
  float thermistorReading = analogRead(THERMISTORPIN);
  float thermistorResistance = 100000/((1023/thermistorReading)-1);
  //Serial.println(thermistorResistance);
  float thermistorTemperature = (1/((log(thermistorResistance/100000.0)/thermistorB)+1/(273.15+25.0)))-273.15;
  return thermistorTemperature;
}

//switching off the motor driver
void stopMotor() {
  lastInd1 = digitalRead(STEPPERPIN1);
  lastInd2 = digitalRead(STEPPERPIN2);
  lastInd3 = digitalRead(STEPPERPIN3);
  lastInd4 = digitalRead(STEPPERPIN4);
  
  digitalWrite(STEPPERPIN1,LOW);
  digitalWrite(STEPPERPIN2,LOW);
  digitalWrite(STEPPERPIN3,LOW);
  digitalWrite(STEPPERPIN4,LOW);
}

void startMotor() {
  digitalWrite(STEPPERPIN1,lastInd1);
  digitalWrite(STEPPERPIN2,lastInd2);
  digitalWrite(STEPPERPIN3,lastInd3);
  digitalWrite(STEPPERPIN4,lastInd4);
}

void receiveLetter(int amount) {
  amountBytesI2C = amount;
  int i = 0;
  while(Wire.available()){
    byteBufferI2C[i] = Wire.read();
    Serial.println(byteBufferI2C[i]);
    i++;
  }
  readBytesFinished = true;
}

void requestEvent(void) {
  if(stepperOverheated) {
    Wire.write("O"); //sending tag o to master for overheating
    Serial.println("send O");
  }
  else {
    Wire.write("P"); //sending tag p to master for ping/alive
    Serial.println("send P");
  }
}
