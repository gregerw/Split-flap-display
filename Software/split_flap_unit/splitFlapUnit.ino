
// (c) Walter Greger, internet.greger@me.com

//libs
#include <Wire.h>
#include <Stepper.h>

//constants i2c
#define i2cAddress 1 //i2c address
//constants stepper
#define stepperIn1Pin 8
#define stepperIn2Pin 9
#define stepperIn3Pin 10
#define stepperIn4Pin 11
#define STEPS 2038 // 28BYJ-48, number of steps; sometimes 2038
#define CALOFFSET 0 //offset for zero position (black flap)
#define hallPin 5
#define amountFlaps 45
#define correctedSteps 2038
#define stepperSpeed 15 //in rpm
//constants others
#define baudrate 9600
#define rotationDirection 1

//globals
String displayedLetter = " ";
String desiredLetter = " ";
String letters[] = {" ","A","B","C","D","E","F","G","H","I","J","K","L","M","N","O","P","Q","R","S","T","U","V","W","X","Y","Z","Ä","Ö","Ü","0","1","2","3","4","5","6","7","8","9",":",".","-","?","!"};
Stepper stepper(STEPS, stepperIn1Pin,stepperIn3Pin,stepperIn2Pin,stepperIn4Pin); //stepper setup
bool lastInd1 = false; //store last status of phase
bool lastInd2 = false; //store last status of phase
bool lastInd3 = false; //store last status of phase
bool lastInd4 = false; //store last status of phase
float missedSteps = 0; //cummulate steps <1, to compensate via additional when reaching >1
bool demoMode = false;
bool demoInProgress = false;

void setup() {
  //initialize serial
  Serial.begin(baudrate);
  Serial.println("starting i2c slave");

  //initialize i2c
  Wire.begin(i2cAddress); //i2c address of this unit 
  Wire.onReceive(receiveLetter); //call-function if for transfered letter via i2c 

  //setup motor
  pinMode(hallPin, INPUT);
  calibrate(); //doing a calibration
}

void loop() {
  static int i = 0;
  //check if currently displayed letter differs from desired letter  
  if((displayedLetter!=desiredLetter) && !demoMode ) rotateToLetter(desiredLetter);
  else if(demoMode && !demoInProgress) {
    demoInProgress = true;
    showDemo();
  }
}

//doing a calibration of the revolver using the hall sensor
void calibrate() {
  Serial.println("calibrate revolver");
  bool reachedMarker = false;
  stepper.setSpeed(stepperSpeed);
  int i = 0;
  while(!reachedMarker) {
    int currentHallValue = digitalRead(hallPin);
      if(currentHallValue == 1) {
        //not reached yet
        stepper.step(rotationDirection * 1);
      }
      else {
        reachedMarker = true;
        stepper.step(rotationDirection * CALOFFSET);
        displayedLetter = " ";
        missedSteps = 0;
        stopMotor();
      }
      if(i > 3 * STEPS) {
        displayedLetter = " ";
        desiredLetter = " ";
        reachedMarker = true; //safty aspect. if revolver turned 3 times without getting hall-signal, better turn of in terms of heating
        Serial.println("calibration revolver failed");
      }
      i++;
  }
}

//rotate to desired letter
void rotateToLetter(String toLetter) {
  //get letter position
  int posLetter = -1;
  int posCurrentLetter = -1;
  size_t amountLetters = sizeof(letters) / sizeof(String);
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
      stepper.setSpeed(stepperSpeed);
      //doing the rotation letterwise
      for(int i=0;i<diffPosition;i++) {
        float preciseStep = (float)correctedSteps/(float)amountFlaps;
        int roundedStep = (int)preciseStep;
        missedSteps = missedSteps + ((float)preciseStep - (float)roundedStep);
        if(missedSteps > 1) {
          roundedStep = roundedStep +1;
          missedSteps--;
        }
        stepper.step(rotationDirection*roundedStep);
      }
    }
    else {
      //full rotation is needed, good time for a calibration
      Serial.println("full rotation incl. calibration");
      calibrate();
      startMotor();
      stepper.setSpeed(stepperSpeed);
      for(int i=0;i<posLetter;i++) {
        float preciseStep = (float)correctedSteps/(float)amountFlaps;
        int roundedStep = (int)preciseStep;
        missedSteps = missedSteps + (float)preciseStep - (float)roundedStep;
        if(missedSteps > 1) {
          roundedStep = roundedStep +1;
          missedSteps--;
        }
        stepper.step(rotationDirection*roundedStep);
      }
    }
    //store new position
    displayedLetter = toLetter;
    //rotation is done, stop the motor
    delay(100); //important to stop rotation before shutting of the motor
    stopMotor();
  }
  else {
    Serial.println("letter unknown, go to space");
    calibrate();
    desiredLetter = " ";
  }
}

//switching off the motor driver
void stopMotor() {
  lastInd1 = digitalRead(stepperIn1Pin);
  lastInd2 = digitalRead(stepperIn2Pin);
  lastInd3 = digitalRead(stepperIn3Pin);
  lastInd4 = digitalRead(stepperIn4Pin);
  
  digitalWrite(stepperIn1Pin,LOW);
  digitalWrite(stepperIn2Pin,LOW);
  digitalWrite(stepperIn3Pin,LOW);
  digitalWrite(stepperIn4Pin,LOW);
}

void startMotor() {
  digitalWrite(stepperIn1Pin,lastInd1);
  digitalWrite(stepperIn2Pin,lastInd2);
  digitalWrite(stepperIn3Pin,lastInd3);
  digitalWrite(stepperIn4Pin,lastInd4);
}

void receiveLetter(int amount)
{
  byte bytes[amount];
  int i = 0;
  while(Wire.available()){
    bytes[i] = Wire.read();
    Serial.print("received byte:");
    Serial.println(bytes[i]);
    i++;
  }

  //convert it to utf string
  size_t amountBytes = sizeof(bytes) / sizeof(byte);
  String utfLetter;
  //go through the bytes and get the utf letter
  for(int i = 0; i < amountBytes; i++) {
    int charLength = 2; //at least two bytes including null termination
    if(bitRead(bytes[i],7)&&bitRead(bytes[i],6)) { //if true, utf char consists of at least two bytes + null termination
      charLength++;
      if(bitRead(bytes[i],5)) charLength++; //if true, utf char consists of at least three bytes + null termination
      if(bitRead(bytes[i],4)) charLength++; //if true, utf char consists of four bytes+ null termination
    }
    byte aBuffer[charLength];
    for(int j = 0; j < charLength; j++) {
      aBuffer[j] = bytes[i+j];
    }
    //aBuffer[charLength] = 0; //terminate string with NULL byte at end
    utfLetter = aBuffer;
    //utfMessage.concat(utfLetter);
    i = i + (charLength-1); //skip bytes corresponding to length of utf letter
  }
  //umlaute as capital
  if(utfLetter == "ä") utfLetter = "Ä";
  if(utfLetter == "ö") utfLetter = "Ö";
  if(utfLetter == "ü") utfLetter = "Ü";
  Serial.print("utf letter:");
  Serial.println(utfLetter);
  if (demoMode && !(utfLetter == "©")) {
    demoMode = false;
  }
  else if(utfLetter == "©") {
    demoMode = true;
  }
  else desiredLetter = utfLetter; //normal character)
}

void showDemo() {
    demoMode = true;
    size_t amountLetters = sizeof(letters) / sizeof(String);
   //go directly to next letter, get steps from current letter to target letter
   //doing the rotation letterwise
   for(int i=0;i<amountLetters;i++) {
    startMotor();
    stepper.setSpeed(stepperSpeed);
    float preciseStep = (float)correctedSteps/(float)amountFlaps;
    int roundedStep = (int)preciseStep;
    missedSteps = missedSteps + ((float)preciseStep - (float)roundedStep);
    if(missedSteps > 1) {
      roundedStep = roundedStep +1;
      missedSteps--;
    }
    stepper.step(rotationDirection * roundedStep);
    float timeForRotation = 60/stepperSpeed/amountLetters*1000;
    delay(100);
    stopMotor();
    delay(1450);
  }
  delay(10000);
  desiredLetter = " ";
  Serial.print("shutting off demo mode");
  demoMode = false;
  demoInProgress = false;
  desiredLetter = " ";
}
