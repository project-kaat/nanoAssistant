#include <LowPower.h>

/* It is important to disable auto-reset on opening a serial connection to the MCU
 *  For Arduino Uno a 6-10 uF capacitor between RES and GND should work
 *  Disabling DTR didn't work for me on linux (stty did work, but for the port itself, not just one program or even a specific device)
 *  120 Ohm resistor between RST and 5V should work for some Arduino boards (did not work for my Uno)
 */

#define DBG false //enable/disable debug messages
#define dbgp(msg) if (DBG) Serial.print(msg)

#define PC_5V_INPUT         2 //this pin is connected to the host pc's 5V rail to detect if the host is up and operational
#define AUDIO_RELAY_OUT     7 //this is attached to a ~~S/R bistable~~ actual fucking relay module that controls audio subsystem's power delivery
#define CCFL_ENABLE_OUTPUT  8 //CCFL controller enable/disable digital output
#define CCFL_DIM_OUTPUT     6 //PWM CCFL controller brightness control

#define CCFL_DIM_MIN_VALUE        100 //This works nicely for my lamp
#define CCFL_DIM_MAX_VALUE        255 //This workds nicely for almost any lamp
#define SERIAL_PACKET_MAX_SIZE    8 //Increase for more complex commands, or more arguments
#define REQUIRED_OFF_SAMPLE_COUNT 10 //require this number of samples, indicating that the host is off, to finally consider it off; needed to guard against false-positives on unstable power networks
#define BEFORE_SLEEP_DELAY        1000 //wait some time just before putting MCU in sleep mode, when the host is off; 1000 milliseconds by default

#define SERIAL_COM_AUDIO_ON     0x71 //Toggle the relay if off
#define SERIAL_COM_AUDIO_OFF    0x72 //Toggle the relay if on
#define SERIAL_COM_AUDIO_TOGGLE 0x73 //Toggle the relay if not dead

#define SERIAL_COM_LIGHT_ON     0x81 //Enable the CCFLC
#define SERIAL_COM_LIGHT_OFF    0x82 //Disable the CCFLC
#define SERIAL_COM_LIGHT_SET    0x83 //Set the dim value of CCFLC (includes min,max value checks)
#define SERIAL_COM_LIGHT_ADD    0x84 //Add a value to the current dim value (this MCU will remember the last set value at all times)
#define SERIAL_COM_LIGHT_SUB    0x85 //Same as above, but subtract
#define SERIAL_COM_LIGHT_TOGGLE 0x86 //Toggle depending on the last state

bool isAudioOn = false;
volatile bool isHostOn = false;
byte ccflDimValue = 0;
bool isCcflOn = false;

void handleSerialCommand() {
  byte buf[SERIAL_PACKET_MAX_SIZE];
  byte totalRead = Serial.readBytes(buf, SERIAL_PACKET_MAX_SIZE);
  dbgp("read ");
  dbgp(totalRead);
  dbgp(" bytes\n");

  //integrity checks via magic values:
  if (buf[0] != 'S' || buf[1] != 'I') {
    dbgp("Integrity checks not passed\n");
    return;
  }

  dbgp("Parsing the command\n");

  switch (buf[2]) {
    case SERIAL_COM_AUDIO_ON:
      dbgp("audio on com\n");
      setAudioRelay(HIGH);
      break;
    case SERIAL_COM_AUDIO_OFF:
      dbgp("audio off com\n");
      setAudioRelay(LOW);
      break;
    case SERIAL_COM_AUDIO_TOGGLE:
      dbgp("audio toggle com\n");
      setAudioRelay(!isAudioOn);
      break;
    case SERIAL_COM_LIGHT_ON:
      dbgp("light on com\n");
      ccflSetState(1);
      break;
    case SERIAL_COM_LIGHT_OFF:
      dbgp("light off com\n");
      ccflSetState(0);
      break;
    case SERIAL_COM_LIGHT_SET:
      dbgp("light SET com\n");
      ccflSetDim(buf[3]);
      break;
    case SERIAL_COM_LIGHT_ADD:
      dbgp("light increment com\n");
      ccflAddDim(buf[3]);
      break;
    case SERIAL_COM_LIGHT_SUB:
      dbgp("light decrement com\n");
      ccflSubDim(buf[3]);
      break;  
    case SERIAL_COM_LIGHT_TOGGLE:
      dbgp("light toggle com\n");
      ccflSetState(!isCcflOn);
      break;
    default:
      dbgp("Unknown serial command: ");
      dbgp(buf[2]);
      dbgp("\n");      
  }
}

void hostOffLoop() {
  dbgp("Reached host off loop\n");
  Serial.end();
  setAudioRelay(LOW);
  ccflSetState(0);
  delay(BEFORE_SLEEP_DELAY); //delay just before assigning the interrupt and going to sleep to avoid false positives
  attachInterrupt(digitalPinToInterrupt(PC_5V_INPUT), hostOnISR, RISING);
  LowPower.powerDown(SLEEP_FOREVER, ADC_OFF, BOD_OFF);
  //cleanup after waking up
  detachInterrupt(digitalPinToInterrupt(PC_5V_INPUT));
  Serial.begin(9600);
}

void hostOnLoop() {
  
  dbgp("Reached host on loop\n");
  setAudioRelay(HIGH);
  static byte offSampleCount = 0; //sample the state of host detection pin for improved accuracy
  while (isHostOn) {
    if (Serial.available() > 1) {
      dbgp("Trying to handle a serial command\n");
      handleSerialCommand();
    }
    if (!detectHostState()) {
      dbgp("Host appears to be off... counting\n");
      offSampleCount++;
      if (offSampleCount >= REQUIRED_OFF_SAMPLE_COUNT) {
        dbgp("Alright, host is off\n");
        isHostOn = false;
        offSampleCount = 0;
      }
    }
    else {
      offSampleCount = 0; //reset on single deviation
    }
    delay(500);
  }
}

void hostOnISR() {
  isHostOn = true;
}

void setupIO() {
  pinMode(PC_5V_INPUT, INPUT);
  pinMode(AUDIO_RELAY_OUT, OUTPUT);
  pinMode(CCFL_ENABLE_OUTPUT, OUTPUT);
  pinMode(CCFL_DIM_OUTPUT, OUTPUT);
  digitalWrite(CCFL_ENABLE_OUTPUT, LOW);
  digitalWrite(AUDIO_RELAY_OUT, LOW);
  analogWrite(CCFL_DIM_OUTPUT, 0);
  Serial.begin(9600);
}

bool detectHostState() {
  return digitalRead(PC_5V_INPUT);
}

void ccflSetState(bool state) {
  if (state) {
    analogWrite(CCFL_DIM_OUTPUT, 255);
    digitalWrite(CCFL_ENABLE_OUTPUT, HIGH);
    ccflDimValue = 255;
  }
  else {
   digitalWrite(CCFL_ENABLE_OUTPUT, LOW);
   analogWrite(CCFL_DIM_OUTPUT, 0);
   ccflDimValue = 0;
  }
  isCcflOn = state;
}

void ccflSetDim(byte dimValue) {
  dbgp("received dimValue is ");
  dbgp(dimValue);
  dbgp("\n");
  if ((int)dimValue < CCFL_DIM_MIN_VALUE) {
    dimValue = CCFL_DIM_MIN_VALUE;
  }
  else if ((int)dimValue > CCFL_DIM_MAX_VALUE) {
    dimValue = CCFL_DIM_MAX_VALUE;
  }
    dbgp("Setting ");
    dbgp(dimValue);
    dbgp(" as a dim value\n");
    analogWrite(CCFL_DIM_OUTPUT, dimValue);
    ccflDimValue = dimValue;
}

void ccflAddDim(byte increment) {
  ccflSetDim(ccflDimValue + increment);
}

void ccflSubDim(byte decrement) {
  ccflSetDim(ccflDimValue - decrement);
}

void setAudioRelay(bool state) {
  digitalWrite(AUDIO_RELAY_OUT, state);
  isAudioOn = state;
}

void setup() {
  /*PLAN:
   * initialize state (read the control pin)
   * do the correct stuff:
   *  if pc is up, turn on the audio subsystem; open a serial connection to wait for the ccfl control; wait until pc is off to sleep
   *  if pc is off, wait until it's on
   * attach correct interrupt
   * sleep until woken
   */
  setupIO();
  isHostOn = detectHostState();
}

void loop() {
    if (isHostOn) {
      dbgp("host on detected\n");
      hostOnLoop();
    }
    else {
      dbgp("host off detected\n");
      hostOffLoop();
    }
}
