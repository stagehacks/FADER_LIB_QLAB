// FADER_8 VERSION 1.0

// Ensure Board Type (Tools > Board Type) is set to Teensyduino Teensy 4.1
#include <TeensyThreads.h>
#include <NativeEthernet.h>
#include <ResponsiveAnalogRead.h>

#define REST 0
#define MOTOR 1
#define TOUCH 2

elapsedMillis sinceBegin = 0;
elapsedMillis sinceMoved[8];
elapsedMillis sinceSent[8];
byte lastSentValue[8] = {0, 0, 0, 0, 0, 0, 0, 0};
int target[8] = {0, 0, 0, 0, 0, 0, 0, 0};
byte mode[8] = {1, 1, 1, 1, 1, 1, 1, 1};
int previous[8] = {0, 0, 0, 0, 0, 0, 0, 0};
byte readPins[8] = {A9, A8, A7, A6, A5, A4, A3, A2};
static byte MOTOR_PINS_A[8] = {0, 2, 4, 6, 8, 10, 24, 28};
static byte MOTOR_PINS_B[8] = {1, 3, 5, 7, 9, 12, 25, 29};
ResponsiveAnalogRead faders[8] = {
  ResponsiveAnalogRead(A9, true),
  ResponsiveAnalogRead(A8, true),
  ResponsiveAnalogRead(A7, true),
  ResponsiveAnalogRead(A6, true),
  ResponsiveAnalogRead(A5, true),
  ResponsiveAnalogRead(A4, true),
  ResponsiveAnalogRead(A3, true),
  ResponsiveAnalogRead(A2, true)
};


void faderLoop(){
  for (byte i = 0; i < 8; i++) {
    faders[i].update();
    int distanceFromTarget = target[i] - getFaderValue(i);

    if (faders[i].hasChanged()) {
      sinceMoved[i] = 0;
      if (mode[i] != MOTOR && previous[i] - getFaderValue(i) != 0) {
        target[i] = -1;
        mode[i] = TOUCH;
      }
    } else if (mode[i] == REST && target[i] != -1 && abs(distanceFromTarget) > 4) {
      mode[i] = MOTOR;
    }

    if (mode[i] == TOUCH) {
      if (lastSentValue[i] == getFaderValue(i) && sinceMoved[i] > 900) {
        mode[i] = REST;
        target[i] = -1;
      } else if (sinceSent[i] > 30 && lastSentValue[i] != getFaderValue(i)) {
        sinceMoved[i] = 0;
        sinceSent[i] = 0;
        lastSentValue[i] = getFaderValue(i);
        faderHasMoved(i);
      }
    }

    if (mode[i] == MOTOR) {
      faders[i].disableSleep();
      byte motorSpeed = min(MOTOR_MAX_SPEED, MOTOR_MIN_SPEED + abs(distanceFromTarget / 4));

      if (abs(distanceFromTarget) < 2) {
        analogWrite(MOTOR_PINS_A[i], 255);
        analogWrite(MOTOR_PINS_B[i], 255);
        if (sinceMoved[i] > 10) {
          faders[i].enableSleep();
        }
        if (sinceMoved[i] > 200) {
          mode[i] = REST;
          target[i] = -1;
        }
      } else if (distanceFromTarget > 0) {
        sinceMoved[i] = 0;
        analogWrite(MOTOR_PINS_A[i], motorSpeed);
        analogWrite(MOTOR_PINS_B[i], 0);
      } else if (distanceFromTarget < 0) {
        sinceMoved[i] = 0;
        analogWrite(MOTOR_PINS_A[i], 0);
        analogWrite(MOTOR_PINS_B[i], motorSpeed);
      }

    } else {
      faders[i].enableSleep();
      analogWrite(MOTOR_PINS_A[i], 255);
      analogWrite(MOTOR_PINS_B[i], 255);
    }
    if (DEBUG) {
      Serial.print(getFaderValue(i));
      Serial.print("\t");
    }
    previous[i] = getFaderValue(i);

  }
  if (DEBUG) {
    Serial.println("");
  }
}

byte networkThreadID = 0;
void faderSetup() {
  if(DEBUG){
    delay(2000);
  }
  Serial.println("########  FADER_8  ########");
  Serial.println("Setting up Faders...");

  for (byte i = 0; i < 8; i++) {
    pinMode(MOTOR_PINS_A[i], OUTPUT);
    pinMode(MOTOR_PINS_B[i], OUTPUT);
    digitalWrite(MOTOR_PINS_A[i], LOW);
    digitalWrite(MOTOR_PINS_B[i], LOW);
    analogWriteFrequency(MOTOR_PINS_A[i], 18000);
    analogWriteFrequency(MOTOR_PINS_B[i], 18000);
    faders[i].setActivityThreshold(TOUCH_THRESHOLD);
  }
  
  Serial.println("Starting Network Thread...");
  networkThreadID = threads.addThread(networkInit);
}


#define ETHERNET_OFFLINE 0
#define ETHERNET_ONLINE_DHCP 1
#define ETHERNET_ONLINE_STATIC 2
byte ethernetStatus = 0;

void networkInit() {
  Serial.println("Testing Ethernet Connection...");

  IPAddress SELF_IP(IP_ADDRESS[0], IP_ADDRESS[1], IP_ADDRESS[2], IP_ADDRESS[3]);

  Ethernet.begin(MAC_ADDRESS, SELF_IP);
  ethernetStatus = ETHERNET_ONLINE_STATIC;

  Serial.print("Ethernet Status: ");
  Serial.println(Ethernet.linkStatus());

  if (Ethernet.linkStatus() == LinkON) {
    ethernetSetup();
  } else {
    Serial.println("Ethernet is not connected.");
  }

  threads.suspend(networkThreadID);

}


void setFaderTarget(byte fader, byte value){
  target[fader] = value;
}

byte getEthernetStatus(){
  return ethernetStatus;
}

int getFaderValue(byte fader) {
  return max(0, min(255, map(faders[fader].getValue(), faderTrimBottom[fader], faderTrimTop[fader], 0, 255)));
}
