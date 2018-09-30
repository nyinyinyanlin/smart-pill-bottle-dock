#include <WiFiClient.h>
#include <WiFi.h>

#define BLACK_BTN 12
#define BLUE_BTN 19
#define BUZZER 13
#define NUM_TLC5974 1
#define TLC_DATA 25
#define TLC_CLOCK 26
#define TLC_LATCH 27
#define TLC_OE -1
#define PCD_DC 17
#define PCD_CS 5
#define PCD_RST 16
#define DOUT_SLOT1 4
#define CLK_SLOT1 15
#define DOUT_SLOT2 21
#define CLK_SLOT2 22
#define DOUT_SLOT3 32
#define CLK_SLOT3 33

#define PILL_SUCCESS 1
#define PILL_UNDERDOSE 2
#define PILL_OVERDOSE 3
#define SELECT_BTN 0
#define CONFIRM_BTN 1
#define LED_GREEN 2
#define LED_BLUE 3
#define LED_RED 1
#define LED_OFF 0
#define LED_YELLOW 4

#define SETTING_UPDATE_INTERVAL 60000
#define TASK_REFRESH_INTERVAL 60000
#define LIFT_WAIT_INTERVAL 60000
#define DOWN_WAIT_INTERVAL 30000
#define SELECT_BTN_INTERVAL 200
#define CONFIRM_BTN_INTERVAL 500
#define BTN_INTERVAL 500
#define LED_INTERVAL 500
#define BUZZ_INTERVAL 500

#define STATE_NONE 0
#define STATE_LIFT_WAIT 1
#define STATE_DOWN_WAIT 2
#define STATE_FINISH 3

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_PCD8544.h>
#include "Adafruit_TLC5947.h"
#include <WiFi.h>
#include "HX711.h"
#include "soc/rtc.h"

const char* ssid     = "HUAWEI-B310-DF6A";//"n3l";
const char* password = "LH98Y32A9R9";//"nyinyinyanlin";

const char* host = "192.168.8.100";
const char* getSettingUrl = "/getsettinghw";
const char* getJobsUrl = "/getjobs";
const char* endJobUrl = "/endjob";

const char* streamId   = "....................";
const char* privateKey = "....................";

byte pill_register = B00000000;
byte state_register = B00000000;
byte btn_register = B00000000;

long setting_timeout = 0;
long task_timeout = 0;
long btn_timeout = 0;
long select_btn_timeout = 0;
long confirm_btn_timeout = 0;
long led_timeout = 0;
long buzz_timeout = 0;
long slot_timeout[3] = {0, 0, 0};
long state_timeout[3] = {0, 0, 0};
long timeout1 = 0;
long timeout2 = 0;

byte cindex = 1;
bool alarmFlag = false;
bool ledFlag = false;
bool buzzFlag = false;
int pillWeights[3] = {0, 0, 0};
float scaleWeight[3] = {0, 0, 0};
float slot1window[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
float slot2window[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
float slot3window[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
String responseStrings[3] = {"", "", ""};

Adafruit_TLC5947 tlc = Adafruit_TLC5947(NUM_TLC5974, TLC_CLOCK, TLC_DATA, TLC_LATCH);
Adafruit_PCD8544 display = Adafruit_PCD8544(17, 5, 16);

float calibration_factor[3] = {875, -970, 861};
float units;

HX711 scale[3] = {
  HX711(DOUT_SLOT1, CLK_SLOT1),
  HX711(DOUT_SLOT2, CLK_SLOT2),
  HX711(DOUT_SLOT3, CLK_SLOT3)
};

void pushIntoWindow(byte slotnum, float value) {
  if (slotnum == 1) {
    Serial.print("[");
    for (byte i = 1; i < 10; i++) {
      slot1window[i - 1] = slot1window[i];
      Serial.print(slot1window[i - 1]);
      Serial.print(",");
    }
    slot1window[9] = value;
    Serial.print(slot1window[9]);
    Serial.println("]");
  } else if (slotnum == 2) {
    Serial.print("[");
    for (byte i = 1; i < 10; i++) {
      slot2window[i - 1] = slot2window[i];
      Serial.print(slot2window[i - 1]);
      Serial.print(",");
    }
    slot2window[9] = value;
    Serial.print(slot2window[9]);
    Serial.println("]");
  } else if (slotnum == 3) {
    Serial.print("[");
    for (byte i = 1; i < 10; i++) {
      slot3window[i - 1] = slot3window[i];
      Serial.print(slot3window[i - 1]);
      Serial.print(",");
    }
    slot3window[9] = value;
    Serial.print(slot3window[9]);
    Serial.println("]");
  }
}

float getRevAvgWindow(byte slotnum, byte count) {
  float avg = 0.0;
  if (slotnum == 1) {
    for (byte i = 0; i < count; i++) {
      avg += slot1window[9 - i];
    }
  } else if (slotnum == 2) {
    for (byte i = 0; i < count; i++) {
      avg += slot2window[9 - i];
    }
  } else if (slotnum == 3) {
    for (byte i = 0; i < count; i++) {
      avg += slot3window[9 - i];
    }
  }
  return avg / count;
}

void cleanWindow(byte slotnum) {
  for (byte j = 0; j < 10; j++) {
    pushIntoWindow(slotnum, 0);
  }
}

float getWindowAverage(byte slotnum) {
  float average = 0;
  if (slotnum == 1) {
    for (byte i = 0; i < 10; i++) {
      average += slot1window[i];
    }
  } else if (slotnum == 2) {
    for (byte i = 0; i < 10; i++) {
      average += slot2window[i];
    }
  } else if (slotnum == 3) {
    for (byte i = 0; i < 10; i++) {
      average += slot3window[i];
    }
  }
  Serial.print("Average -> ");
  Serial.println(average);
  return average / 10;
}

bool isTimeout(long start_time, long interval) {
  if (millis() - start_time >= interval) {
    return true;
  } else {
    return false;
  }
}

bool isTask(byte & task_register, byte slot_number) {
  return bitRead(task_register, slot_number - 1);
}

void setTimestamp(long & timestamp) {
  timestamp = millis();
}

byte getTaskState(byte & state_register, byte slot_number) {
  byte mask = B00000000;
  bitSet(mask, (slot_number - 1) * 2);
  bitSet(mask, ((slot_number - 1) * 2) + 1);
  return (state_register & mask) >> (slot_number - 1) * 2;
}

void setTaskState(byte & state_register, byte state, byte slot_number) {
  bitWrite(state_register, (slot_number - 1) * 2, bitRead(state, 0));
  bitWrite(state_register, ((slot_number - 1) * 2) + 1, bitRead(state, 1));
}

String getStateName(byte state) {
  switch (state) {
    case STATE_NONE:
      return "STATE_NONE";
    case STATE_LIFT_WAIT:
      return "STATE_LIFT_WAIT";
    case STATE_DOWN_WAIT:
      return "STATE_DOWN_WAIT";
    case STATE_FINISH:
      return "STATE_FINISH";
  }
}
bool getSelectBtn() {
  return !digitalRead(BLACK_BTN);
}

bool getConfirmBtn() {
  return !digitalRead(BLUE_BTN);
}

void buzzOn() {
  digitalWrite(BUZZER, HIGH);
}

void buzzOff() {
  digitalWrite(BUZZER, LOW);
}

void setLed(byte ledNum, byte set) {
  if (set == LED_OFF) {
    tlc.setLED(ledNum, 0, 0, 0);
  } else if (set == LED_RED) {
    tlc.setLED(ledNum, 1023, 0, 0);
  } else if (set == LED_GREEN) {
    tlc.setLED(ledNum, 0, 1023, 0);
  } else if (set == LED_BLUE) {
    tlc.setLED(ledNum, 0, 0, 1023);
  } else if (set == LED_YELLOW) {
    tlc.setLED(ledNum, 1023, 1023, 0);
  }
  tlc.write();
}

String makeRequest(const char* host, String url, String reqMethod, String params) {
  WiFiClient client;
  uint16_t httpPort = 8080;
  if (!client.connect(host, httpPort)) {
    //Serial.println("connection failed");
    return "";
  }

  //Serial.print("Requesting URL: ");
  //Serial.println(url + params);

  // This will send the request to the server
  client.print(String("GET ") + url + params + " HTTP/1.1\r\n" +
               "Host: " + host + "\r\n" +
               "Connection: close\r\n\r\n");
  unsigned long timeout = millis();
  while (client.available() == 0) {
    if (millis() - timeout > 5000) {
      //Serial.println(">>> Client Timeout !");
      client.stop();
      return "";
    }
  }
  String line = "";
  // Read all the lines of the reply from server and print them to Serial
  while (client.available()) {
    line += client.readStringUntil('\r');
  }
  //line.remove(line.length());
  //line.remove(0,50);
  line.remove(0, line.lastIndexOf('\n') + 1);
  return line;
}

void zeroArray(int jobArr[], byte arrSize) {
  for (byte i = 0; i < arrSize / 2; i++) {
    jobArr[i] = 0;
  }
}

String munchStr(String & munchStr, char delimiter) {
  int index = munchStr.indexOf(delimiter);
  if (index < 0)return "";
  String munchedStr = munchStr.substring(0, index);
  munchStr = munchStr.substring(index + 1, munchStr.length());
  return munchedStr;
}


void parseResponse(int resArr[], byte arrSize, String resStr) {
  String tmp = resStr;
  byte index = 0;
  zeroArray(resArr, arrSize);
  while (true) {
    String rtrStr = munchStr(tmp, ',');
    if (!rtrStr.length() && !tmp.length())break;
    else if (!rtrStr.length()) {
      rtrStr = tmp;
      tmp = "";
    }
    String rtrStr2 = munchStr(rtrStr, '-');
    resArr[index] = rtrStr2.toInt();
    resArr[index + 1] = rtrStr.toInt();
    index += 2;
  }
}


void parsePillSetting(int pillSetting[], String resStr) {
  int setting[6];
  parseResponse(setting, sizeof(setting) / 2, resStr);
  for (byte i = 0; i < 6; i += 2) {
    if (setting[i]) {
      pillSetting[setting[i] - 1] = setting[i + 1];
    }
  }
}

void parseJobs(byte & state_register, byte & pill_register, String resStr) {
  int setting[6];
  parseResponse(setting, sizeof(setting) / 2, resStr);
  for (byte i = 0; i < 6; i += 2) {
    if (setting[i]) {
      if ((!setting[i + 1]) || (setting[i + 1] > 4) || (setting[i] > 3) || getTaskState(state_register, setting[i]))break;
      setTaskState(state_register, 1, setting[i]);
      setTaskState(pill_register, setting[i + 1] - 1, setting[i]);
      setTimestamp(state_timeout[setting[i] - 1]);
    }
  }
  Serial.print("Num of Pills -> ");
  Serial.println(pill_register, BIN);
}

void printArray(int arr[], byte arrSize) {
  for (byte i = 0; i < arrSize / 2; i++) {
    Serial.print(arr[i]);
    Serial.print(" ");
  }
  Serial.println();
}

void setup() {
  // put your setup code here, to run once:
  rtc_clk_cpu_freq_set(RTC_CPU_FREQ_80M);
  pinMode(BLACK_BTN, INPUT_PULLUP);
  pinMode(BLUE_BTN, INPUT_PULLUP);
  pinMode(BUZZER, OUTPUT);
  Serial.begin(9600);
  scale[0].set_scale();
  scale[0].tare();
  scale[1].set_scale();
  scale[1].tare();
  scale[2].set_scale();
  scale[2].tare();
  Serial.println("Put your );bottles on");
  tlc.begin();
  if (TLC_OE >= 0) {
    pinMode(TLC_OE, OUTPUT);
    digitalWrite(TLC_OE, LOW);
  }
  setLed(0, LED_OFF);
  setLed(1, LED_OFF);
  setLed(2, LED_OFF);
  display.begin();
  display.setContrast(50);
  display.setTextSize(1);
  display.setTextColor(BLACK);
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Wifi > Trying");
  display.display();
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    display.print(".");
    display.display();
  }
  display.println("");
  display.println("WiFi - OK");
  display.println(WiFi.localIP());
  display.display();
  Serial.begin(9600);
  parsePillSetting(pillWeights, makeRequest(host, getSettingUrl, "GET", ""));
  setTimestamp(setting_timeout);
  parseJobs(state_register, pill_register, makeRequest(host, getJobsUrl, "GET", ""));
  setTimestamp(task_timeout);
}

void loop() {
  if (isTimeout(setting_timeout, SETTING_UPDATE_INTERVAL)) {
    //do something
    Serial.println("SETTING UPDATE");
    parsePillSetting(pillWeights, makeRequest(host, getSettingUrl, "GET", ""));
    setTimestamp(setting_timeout);
  }
  //Check if time out for setting update
  //If Setting Update -> Do Setting update
  if (isTimeout(task_timeout, TASK_REFRESH_INTERVAL)) {
    //do something
    Serial.println("TASK REFRESH");
    parseJobs(state_register, pill_register, makeRequest(host, getJobsUrl, "GET", ""));
    setTimestamp(task_timeout);
  }
  //Check if time out for task exist
  //If time out -> Check if task exist
  //If task exist set flags and notifications
  //updateButtons(btn_register, btn_timeout);
  /*if (isTimeout(select_btn_timeout, SELECT_BTN_INTERVAL)) {
    if (getSelectBtn()) {
      cindex++;
      if (cindex == 4) {
        cindex = 1;
      }
    }
    setTimestamp(select_btn_timeout);
    }*/
  alarmFlag = false;
  for (byte i = 1; i <= 3; i++) {
    byte state = getTaskState(state_register, i);
    if (state == STATE_LIFT_WAIT) {
      alarmFlag = true;
      break;
    }
  }
  if (isTimeout(buzz_timeout, BUZZ_INTERVAL)) {
    setTimestamp(buzz_timeout);
    if (alarmFlag) {
      if (buzzFlag) {
        buzzOff();
        buzzFlag = false;
      } else {
        buzzOn();
        buzzFlag = true;
      }
    } else {
      buzzOff();
      buzzFlag = false;
    }
  }
  if (isTimeout(led_timeout, LED_INTERVAL)) {
    setTimestamp(led_timeout);
    for (byte i = 1; i <= 3; i++) {
      byte state = getTaskState(state_register, i);
      if (ledFlag) {
        setLed(i - 1, LED_OFF);
        ledFlag = false;
      } else {
        ledFlag = true;
        switch (state) {
          case STATE_NONE:
            setLed(i - 1, LED_OFF);
            break;
          case STATE_LIFT_WAIT:
            setLed(i - 1, LED_GREEN);
            break;
          case STATE_DOWN_WAIT:
            setLed(i - 1, LED_RED);
            break;
        }
      }
    }
  }

  for (byte i = 1; i <= 3; i++) {
    if (getTaskState(state_register, i)) {
      byte state = getTaskState(state_register, i);
      if (state > 0) {
        bool flag = false;
        switch (state) {
          case STATE_LIFT_WAIT:
            flag = isTimeout(state_timeout[i - 1], LIFT_WAIT_INTERVAL);
            break;
          case STATE_DOWN_WAIT:
            flag = isTimeout(state_timeout[i - 1], DOWN_WAIT_INTERVAL);
            break;
          case STATE_FINISH:
            String params = "?slotnum=";
            params += char(i + 48);
            params += "&status=" + responseStrings[i - 1];
            setTaskState(state_register, 0, i);
            setTimestamp(state_timeout[i - 1]);
            makeRequest(host, endJobUrl, "GET", params);
            cleanWindow(i);
            setTaskState(pill_register, 0, i);
            break;
        }
        if (flag) {
          String params = "?slotnum=";
          params += char(i + 48);
          params += "&status=timeout";
          setTaskState(state_register, 0, i);
          setTimestamp(state_timeout[i - 1]);
          makeRequest(host, endJobUrl, "GET", params);
          cleanWindow(i);
          setTaskState(pill_register, 0, i);
        } else {
          scale[i - 1].set_scale(calibration_factor[i - 1]);
          units = scale[i - 1].get_units(40);
          if (units < 0) {
            units = 0.0000;
          }
          Serial.print("Units -> ");
          Serial.println(units);
          if (state == STATE_LIFT_WAIT) {
            if (units < 1) {
              scale[i - 1].tare();
              state++;
              setTaskState(state_register, state, i);
              setTimestamp(state_timeout[i - 1]);
            } else {
              float winAvg = getWindowAverage(i);
              if (winAvg == 0) {
                for (byte j = 0; j < 10; j++) {
                  pushIntoWindow(i, units);
                }
                winAvg = units;
              }
              if ((units < (winAvg - 1.25) || units > (getWindowAverage(i) + 1.25))) {
                units = winAvg;
              }
              pushIntoWindow(i, units);
              Serial.println(getWindowAverage(i));
            }
          } else if (state == STATE_DOWN_WAIT) {
            int desired = getTaskState(pill_register, i) + 1;
            if ((units > 5) && (units < getWindowAverage(i))) {
              float lastAverage = getWindowAverage(i);
              cleanWindow(i);
              for (byte index = 0; index < 15; index++) {
                float winAvg = getWindowAverage(i);
                float curScale = scale[i - 1].get_units(40);
                if (winAvg == 0) {
                  for (byte j = 0; j < 10; j++) {
                    pushIntoWindow(i, curScale);
                  }
                  winAvg = curScale;
                }
                if ((curScale < (winAvg - 1.25) || curScale > (getWindowAverage(i) + 1.25))) {
                  curScale = winAvg;
                }
                pushIntoWindow(i, curScale);
              }
              Serial.print("Last Average -> ");
              Serial.println(lastAverage);
              units = getWindowAverage(i);
              int actual = round((lastAverage - units) / (float(pillWeights[i - 1]) / 1000));
              Serial.print("Current Average -> ");
              Serial.println(units);
              Serial.print("Desired -> ");
              Serial.println((getTaskState(pill_register, i) + 1));
              Serial.print("Took -> ");
              Serial.println(actual);
              if (actual == desired) {
                responseStrings[i - 1] = "success";
                Serial.println("Took correctly");
              } else if (actual < desired) {
                responseStrings[i - 1] = "less";
                Serial.println("Underdose");
              } else if (actual > desired) {
                responseStrings[i - 1] = "more";
                Serial.println("Overdose");
              }
              state++;
              setTaskState(state_register, state, i);
              setTimestamp(state_timeout[i - 1]);
            }
          }
        }
      }
    }
  }
}
