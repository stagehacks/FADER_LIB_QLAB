#include <ResponsiveAnalogRead.h>
#include <NativeEthernet.h>
#include <NativeEthernetUdp.h>
#include <OSCMessage.h>
#include <OSCBundle.h>
#include <TeensyThreads.h>

//     _____ ______ _______ _______ _____ _   _  _____  _____ 
//    / ____|  ____|__   __|__   __|_   _| \ | |/ ____|/ ____|
//   | (___ | |__     | |     | |    | | |  \| | |  __| (___  
//    \___ \|  __|    | |     | |    | | | . ` | | |_ |\___ \.
//    ____) | |____   | |     | |   _| |_| |\  | |__| |____) |
//   |_____/|______|  |_|     |_|  |_____|_| \_|\_____|_____/.

byte MAC_ADDRESS[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };

IPAddress SELF_IP(192, 168, 1, 130);
#define SELF_PORT 53001

IPAddress DESTINATION_IP(192, 168, 1, 120);
#define DESTINATION_PORT 53000

// FADER TRIM SETTINGS
int faderTrimTop[8] = {232, 235, 235, 235, 235, 235, 235, 235}; // ADJUST THIS IF A FADER ISN'T READING 127 AT THE TOP OF ITS TRAVEL
int faderTrimBottom[8] = {17, 15, 15, 15, 15, 15, 15, 15}; // ADJUST THIS IF A FADER ISN'T READING 0 AT THE BOTTOM OF ITS TRAVEL





#define REST 0
#define MOTOR 1
#define TOUCH 2
elapsedMillis sinceHeartbeat = 0;
elapsedMillis sinceBegin = 0;
elapsedMillis sinceMoved[8];
elapsedMillis sinceSent[8];
byte lastSentValue[8] = {0, 0, 0, 0, 0, 0, 0, 0};
byte mode[8] = {MOTOR, MOTOR, MOTOR, MOTOR, MOTOR, MOTOR, MOTOR, MOTOR};
int target[8] = {128, 128, 128, 128, 128, 128, 128, 128};
int previous[8] = {0, 0, 0, 0, 0, 0, 0, 0};
EthernetUDP Udp;
byte ethernetStatus = 0;
uint8_t packetBuffer[UDP_TX_PACKET_MAX_SIZE];
int packetSize = 0;
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




//  ______      _____  ______ _____    ________      ________ _   _ _______ 
// |  ____/\   |  __ \|  ____|  __ \  |  ____\ \    / /  ____| \ | |__   __|
// | |__ /  \  | |  | | |__  | |__) | | |__   \ \  / /| |__  |  \| |  | |   
// |  __/ /\ \ | |  | |  __| |  _  /  |  __|   \ \/ / |  __| | . ` |  | |   
// | | / ____ \| |__| | |____| | \ \  | |____   \  /  | |____| |\  |  | |   
// |_|/_/    \_\_____/|______|_|  \_\ |______|   \/   |______|_| \_|  |_|   

void faderHasMoved(byte i) {
  if (ethernetStatus != 0) {
    float faderValueLog = getFaderValueLogarithmic(getFaderValue(i), -60, 12);
    char addr[] = "/cue/selected/level/0/x";
    addr[22] = i + 48;
    OSCMessage msg(addr);
    msg.add((float) faderValueLog);

    Udp.beginPacket(DESTINATION_IP, DESTINATION_PORT);
    msg.send(Udp);
    Udp.endPacket();
  }
}


//     ____   _____  _____ 
//    / __ \ / ____|/ ____|
//   | |  | | (___ | |     
//   | |  | |\___ \| |     
//   | |__| |____) | |____ 
//    \____/|_____/ \_____|

void OSCHandler(OSCMessage &msg) {
  msg.dispatch("/reply/workspaces", OSCworkspaces);
  msg.dispatch("/update/workspace/*/cueList/*/playbackPosition", OSCSliderLevels);
  msg.dispatch("/update/workspace/*/dashboard", OSCDashboard);
  msg.dispatch("/update/workspace/*/cue_id/*", OSCSliderLevels);
  msg.dispatch("/reply/cue_id/*/sliderLevels", OSCSliderLevelsReply);
}
elapsedMillis sinceOSCSliderLevelsReply = 0;
void OSCSliderLevelsReply(OSCMessage &msg) { //reply/cue_id/*/sliderLevels
  char param[512];
  msg.getString(0, param, 512);
  String str = String(param);

  int s = str.indexOf("\"data\":[") + 8;
  for (byte i = 0; i < 8; i++) {
    int val = str.substring(s, str.indexOf(",", s)).toInt();
    float mapped = setFaderValueLogarithmic(val, -60, 12);

    //    Serial.print(i);
    //    Serial.print(" =>");
    //    Serial.print(val);
    //    Serial.print("  ");

    target[i] = mapped;
    s = str.indexOf(",", s) + 1;
  }
  sinceOSCSliderLevelsReply = 0;
  Serial.print(".");
  //Serial.println("");
}
elapsedMillis sinceOSCSliderLevels = 0;
void OSCSliderLevels(OSCMessage &msg) { //update/workspace/*/cueList/*/playbackPosition
  if (sinceOSCSliderLevels > 200) {
    OSCMessage outMsg("/cue/playbackPosition/sliderLevels");
    Udp.beginPacket(DESTINATION_IP, DESTINATION_PORT);
    outMsg.send(Udp);
    Udp.endPacket();
    sinceOSCSliderLevels = 0;
  }
}
void OSCworkspaces(OSCMessage &msg) { //reply/workspaces
  char param[255];
  msg.getString(0, param, 255);
  String str = String(param);

  // ToDo: get updates for all workspaces, not just the first in the list
  int s = str.indexOf("uniqueID\":\"");
  int e = str.indexOf("\",\"", s);
  String workspace = str.substring(s + 11, e);
  String address = "/workspace/" + workspace + "/updates";
  char addr[address.length() + 1];
  address.toCharArray(addr, address.length() + 1);

  OSCMessage outMsg(addr);
  outMsg.add((int32_t) 1);
  Udp.beginPacket(DESTINATION_IP, DESTINATION_PORT);
  outMsg.send(Udp);
  Udp.endPacket();
}
void OSCDashboard(OSCMessage &msg){ //update/workspace/*/dashboard/
  // if a dashboard update message arrives without a sliderLevels message recently,
  // we know a cue with no sliderLevels is selected, and should move the faders to 0
  if(sinceOSCSliderLevelsReply>300){
    for(byte i=0; i<8; i++){
      target[i] = 0;
    }
  }
}
void heartbeat() {
  OSCMessage msg("/workspaces");
  Udp.beginPacket(DESTINATION_IP, DESTINATION_PORT);
  msg.send(Udp);
  Udp.endPacket();
}



//     _____ ______ _______ _    _ _____  
//    / ____|  ____|__   __| |  | |  __ \.
//   | (___ | |__     | |  | |  | | |__) |
//    \___ \|  __|    | |  | |  | |  ___/. 
//    ____) | |____   | |  | |__| | |     
//   |_____/|______|  |_|   \____/|_|     

void setup() {
  Serial.begin(9600);
  delay(1500);
  mainInit();
}

//    __  __          _____ _   _   _      ____   ____  _____  
//   |  \/  |   /\   |_   _| \ | | | |    / __ \ / __ \|  __ \.
//   | \  / |  /  \    | | |  \| | | |   | |  | | |  | | |__) |
//   | |\/| | / /\ \   | | | . ` | | |   | |  | | |  | |  ___/.
//   | |  | |/ ____ \ _| |_| |\  | | |___| |__| | |__| | |     
//   |_|  |_/_/    \_\_____|_| \_| |______\____/ \____/|_|     

void loop() {
  if (ethernetStatus != 0) {
    if (sinceHeartbeat > 10000) {
      sinceHeartbeat = 0;
      heartbeat();
    }
    packetSize = Udp.parsePacket();
    OSCMessage oscMsg;
    if (packetSize) {
      for (int j = 0; j < packetSize; j += UDP_TX_PACKET_MAX_SIZE - 1) {
        Udp.read(packetBuffer, UDP_TX_PACKET_MAX_SIZE - 1);
        oscMsg.fill(packetBuffer, UDP_TX_PACKET_MAX_SIZE - 1);
      }
      char addr[200];
      oscMsg.getAddress(addr, 0);
      Serial.println(addr);
      OSCHandler(oscMsg);
    }
  }

  if (sinceBegin > 3000 && sinceBegin < 3100) {
    Serial.println("###############################");
    sinceBegin = 3100;
  } else if (sinceBegin < 3000) {
    return;
  }

  for (byte i = 0; i < 8; i++) {
    faders[i].update();
    int distanceFromTarget = target[i] - getFaderValue(i);
    
    if (faders[i].hasChanged()) {
      sinceMoved[i] = 0;
      if (mode[i] != MOTOR && previous[i] - getFaderValue(i) != 0) {
        //        Serial.print(i);
        //        Serial.print(" changed from ");
        //        Serial.print(previous[i]);
        //        Serial.print(" to ");
        //        Serial.println(getFaderValue(i));
        mode[i] = TOUCH;
      }
    } else if (mode[i] == REST && target[i] != -1 && abs(distanceFromTarget) >= 2) {
      mode[i] = MOTOR;
    }

    if (mode[i] == TOUCH) {
      if (lastSentValue[i] == getFaderValue(i) && sinceMoved[i] > 900) {
        mode[i] = REST;
      } else if (sinceSent[i] > 10 && lastSentValue[i] != getFaderValue(i)) {
        sinceMoved[i] = 0;
        sinceSent[i] = 0;
        lastSentValue[i] = getFaderValue(i);
        faderHasMoved(i);
      }
    }

    if (mode[i] == MOTOR) {
      faders[i].disableSleep();
      byte motorSpeed = min(215, 190 + abs(distanceFromTarget / 2));

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
    //    Serial.print(mode[i]);
    //    Serial.print("\t");
    previous[i] = getFaderValue(i);

  }
  //  Serial.println("");
}


//    _____ _   _ _____ _______ 
//   |_   _| \ | |_   _|__   __|
//     | | |  \| | | |    | |   
//     | | | . ` | | |    | |   
//    _| |_| |\  |_| |_   | |   
//   |_____|_| \_|_____|  |_|   

byte networkThreadID = 0;
void mainInit() {
  Serial.println("###############################");
  Serial.println("Setting up Faders...");

  for (byte i = 0; i < 8; i++) {
    pinMode(MOTOR_PINS_A[i], OUTPUT);
    pinMode(MOTOR_PINS_B[i], OUTPUT);
    digitalWrite(MOTOR_PINS_A[i], LOW);
    digitalWrite(MOTOR_PINS_B[i], LOW);
    analogWriteFrequency(MOTOR_PINS_A[i], 18000);
    analogWriteFrequency(MOTOR_PINS_B[i], 18000);

    faders[i].setAnalogResolution(256);
    faders[i].setSnapMultiplier(0.1);
    faders[i].setActivityThreshold(3);
  }
  analogReadResolution(8);
  Serial.println("Starting Network Thread...");
  networkThreadID = threads.addThread(networkInit);
}


#define ETHERNET_OFFLINE 0
#define ETHERNET_ONLINE_DHCP 1
#define ETHERNET_ONLINE_STATIC 2
void networkInit() {
  Serial.println("Testing Ethernet Connection...");

  Ethernet.begin(MAC_ADDRESS, SELF_IP);
  ethernetStatus = ETHERNET_ONLINE_STATIC;

  Serial.print("Ethernet Status: ");
  Serial.println(Ethernet.linkStatus());

  if (Ethernet.linkStatus() == LinkON) {
    Udp.begin(SELF_PORT);
  } else {
    Serial.println("Ethernet is not connected.");
  }
  threads.suspend(networkThreadID);

}


//    _    _ ______ _      _____  ______ _____   _____ 
//   | |  | |  ____| |    |  __ \|  ____|  __ \ / ____|
//   | |__| | |__  | |    | |__) | |__  | |__) | (___  
//   |  __  |  __| | |    |  ___/|  __| |  _  / \___ \.
//   | |  | | |____| |____| |    | |____| | \ \ ____) |
//   |_|  |_|______|______|_|    |______|_|  \_\_____/.

int getFaderValue(byte fader) {
  return max(0, min(255, map(faders[fader].getValue(), faderTrimBottom[fader], faderTrimTop[fader], 0, 255)));
}
int getRawFaderValue(byte fader) {
  return map(analogRead(readPins[fader]), faderTrimBottom[fader], faderTrimTop[fader], 0, 255);
}
float getFaderValueLogarithmic(int val, int low, int high) {
  return max(low, min(high, map(pow(val / 255.0, 0.6666), 0, 1, low, high)));
}
float setFaderValueLogarithmic(float val, int low, int high) {
  return pow(map(val, -60, 12, 0, 127) / 127.0, 1.5) * 255.0;
}
