// File: ParentSide.ino
// Authors: Caitlyn Rawlings, Hao Tian
// Date: 8/12/24
// Description: This files defines the parent side of a baby monitoring system. 
//    It allows viewing values or sensors sent from the baby side, triggering an 
//    alarm if a value is in a bad setting. As well as allowing the parent to 
//    send a song to the baby.

// =============== Includes ================
#include <esp_now.h>
#include <WiFi.h>
#include <LiquidCrystal_I2C.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>


// =============== Macros ================
#define NUM_SENSORS 6  // number of sensors

// for pins using: gpio number (legacy) 
#define BUZZER_PIN 9  // Buzzer pin

// Joystick pins
#define X_PIN 13  //the VRX attach to
#define Y_PIN 14  //the VRY attach to
#define SW_PIN 17  //the SW attach to

// for determing joystick direction
#define GO_UP(y) ((y) < 500)
#define GO_DOWN(y) ((y) == 4095)
#define GO_LEFT(x) ((x) < 500)


// ============= Data Types ===============
// defines sensor ids
enum sensorIDs { NOISE, TEMP, HUMIDITY, MOTION, WATER, LIGHT };

// defines the possible lcd views options
enum lcdView {
    MENU,  // menu displays sensor and song options
    SENSOR_MENU,  // menu displays the all the sensor names
    SONG_MENU,  // menu displays the all the song names
    SENSOR_VIEW,  // view of one sensor and its value
    SONG_VIEW,  // view of one song and its value
    ALARM  // display when alarm is triggered
};

// for storing information associated with a sensor
typedef struct {
  int idNum;
  char name[15];  // name to display 
  float value;  // value of sensor. updated when new value received from baby side
  bool (*triggerAlarm)(float);  // function to check if sensor value should trigger an alarm
  bool alarm;  // if alarm was triggered by this sensor
} SensorInfo;


// ============ Function Prototypes ==============
void IRAM_ATTR onTimer32Hz();
void IRAM_ATTR onDataRecv(const uint8_t *mac, const uint8_t *data, int len);
void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status);
bool debounce(int *lastInputTime);
void receiveDataTask(void *arg);
void moveCursor();
void printMenu();
void printSensorPreviews();
void printSongPreviews();
void printSensorMenu();
void printSensorView();
void printSongMenu();
void printSongView();
void printAlarm();
void buzzAlarmTask(void *arg);
void lcdDisplayTask(void *arg);
bool checkNoise(float value);
bool checkTempurature(float value);
bool checkHumidity(float value);
bool checkMotion(float value);
bool checkWater(float value);
bool checkLight(float value);


// =============== Global Variables ================
LiquidCrystal_I2C lcd(0x27, 20, 4);
uint8_t broadcastAddress[6] = {0xDC, 0xDA, 0x0C, 0x21, 0x5B, 0x94};  // adress of baby side board
volatile char incomingMessage[10];  // buffer for message from baby side

// stores sensor info for all sensors
SensorInfo sensorValues[6] = {
  {NOISE, "Noise level", 0, checkNoise, false},
  {TEMP, "Tempurature", 0, checkTempurature, false},
  {HUMIDITY, "Humidity", 0, checkHumidity, false},
  {MOTION, "Motion", 0, checkMotion, false},
  {WATER, "Water level", 0, checkWater, false},
  {LIGHT, "Light level", 0, checkLight, false}
};

// song names
char *songs[3] = {
  "Little Star",
  "Mary's Lamb",
  "Wheels on Bus"
};

enum lcdView currLcdView = MENU;  // start lcd display on menu
int sensorOnTopLcdLineID = 0;  // sensor that will be on top line of lcd when sensor menu displayed
int songOnTopLcdLineID = 0;  // song that will be on top line of lcd when song menu displayed
int cursorLine = 0;  // indicator of which line of lcd is selected

// for tracking last user input times for deboucing
int lastYInputTime = 0;
int lastXInputTime = 0;
int lastSwInputTime = 0;

// timer
hw_timer_t *timer32Hz = NULL;

// task handles
TaskHandle_t lcdDisplayTaskHandle;
TaskHandle_t buzzAlarmTaskHandle;
TaskHandle_t receiveDataTaskHandle;

// queue handle
QueueHandle_t sensorDataQueue;

// semaphore handle
SemaphoreHandle_t lcdSemaphore;


// Name: onTimer32Hz
// Description: ISR for Timer. Triggers buzzAlarmTask to run
void IRAM_ATTR onTimer32Hz() {
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  vTaskNotifyGiveFromISR(buzzAlarmTaskHandle, &xHigherPriorityTaskWoken);
  if (xHigherPriorityTaskWoken) {
    portYIELD_FROM_ISR();
  }
}

// Name: onDataRecv
// Definition: ISR that gets called when a message is received. Updates sensor value
void IRAM_ATTR onDataRecv(const uint8_t * mac, const uint8_t *data, int len) {
  memcpy((void*)incomingMessage, data, len);
  incomingMessage[len] = '\0';  // Null-terminate the string
  xQueueSendFromISR(sensorDataQueue, (void*)incomingMessage, 0);
}

// Name: onDataSent
// Description: Callback function for sending data to baby board
void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {}

// Name: debounce
// Description: Checks if the specified debounce interval has passed 
// since the last recorded input time. If sufficient time has elapsed,
// it updates the last input time and returns true, otherwise, returns false.
bool debounce(int *lastInputTime) {
  int currentTime = millis();
  if (currentTime - *lastInputTime > 500) {
    *lastInputTime = currentTime;
    return true;
  }
  return false;
}

// Name: receiveDataTask
// Description: FreeRTOS task that handles data from the queue. parses the values 
// for sensor id and value and stores the values as well as checks if alarm should trigger
void receiveDataTask(void *arg) {
  char receivedSensorData[10];

  while (true) {
    if (xQueueReceive(sensorDataQueue, receivedSensorData, portMAX_DELAY) == pdTRUE) {
      char* sensorIdStr = strtok(receivedSensorData, ":");
      char* sensorValueStr = strtok(NULL, ":");

      if (sensorIdStr != NULL && sensorValueStr != NULL) {
        int sensorId = atoi(sensorIdStr);  // Convert the sensor id string to a float
        float sensorValue = atof(sensorValueStr);  // Convert the sensor value string to a float

        SensorInfo *sensor = &sensorValues[sensorId];  // sensor info was sent for
        sensor->value = sensorValue;  // update stored sensor value
        sensor->triggerAlarm(sensorValue);  // check if alarm should be triggered
      }     
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

// Name: moveCursor
// Description: moves cursor on lcd to the line its not on.
// From line 0 to 1, or 1 to 0
void moveCursor() {
  // determine x pos of cursor (different depending on lcd display)
  int xPos = 0;
  if (currLcdView == SONG_MENU || currLcdView == SENSOR_MENU) {
    xPos = 1;
  }
  // delete current cursor
  lcd.setCursor(xPos, cursorLine);
  lcd.print(" ");
  cursorLine = (cursorLine + 1) % 2;  // F(0) = 1; F(1) = 0
  // place new cursor
  lcd.setCursor(xPos, cursorLine);
  lcd.print(">");
}

// Name: printMenu
// Description: prints menu screen to lcd and handles navigation logic
void printMenu() {
  // print menu options
  lcd.setCursor(1, 0);
  lcd.print("View Sensors");
  lcd.setCursor(1, 1);
  lcd.print("Send Song");

  if (!digitalRead(SW_PIN) && debounce(&lastSwInputTime)) {
    // go to the view of that menu option
    lcd.clear();
    if (cursorLine == 0) {
      cursorLine = 0;
      currLcdView = SENSOR_MENU;
    } else {
      cursorLine = 0;
      currLcdView = SONG_MENU;
    }
  } else if (GO_DOWN(analogRead(Y_PIN)) && debounce(&lastYInputTime)) {
    // scroll down
    if (cursorLine == 0) {
      moveCursor();
    }
  } else if (GO_UP(analogRead(Y_PIN)) && debounce(&lastYInputTime)) {
    // scroll up
    if (cursorLine == 1) {
      moveCursor();
    }
  }
}

// Name: printSensorPreviews
// Description: prints two sensor names to the lcd
void printSensorPreviews() {
  SensorInfo sensorInfo1 = sensorValues[sensorOnTopLcdLineID];  // get first line sensor
  SensorInfo sensorInfo2 = sensorValues[(sensorOnTopLcdLineID + 1) % NUM_SENSORS];  // get second line sensor

  // print sensor names
  lcd.setCursor(2, 0);
  lcd.print(sensorInfo1.name);

  lcd.setCursor(2, 1);
  lcd.print(sensorInfo2.name);
}

// Name: printSongPreviews
// Description: prints two song names to the lcd
void printSongPreviews() {
  char* song1 = songs[songOnTopLcdLineID];  // get first line song
  char* song2 = songs[(songOnTopLcdLineID + 1) % 3];  // get second line song

  // print song names
  lcd.setCursor(2, 0);
  lcd.print(song1);

  lcd.setCursor(2, 1);
  lcd.print(song2);
}

// Name: printSensorMenu
// Description: prints sensor menu screen to lcd and handles navigation logic
void printSensorMenu() {
  // print back arrow
  lcd.setCursor(0, 0);
  lcd.print("<");

  printSensorPreviews();
  if (!digitalRead(SW_PIN) && debounce(&lastSwInputTime)) {
    // go to the view of selected sensor
    lcd.clear();
    sensorOnTopLcdLineID = (sensorOnTopLcdLineID + cursorLine) % NUM_SENSORS;
    cursorLine = 0;
    currLcdView = SENSOR_VIEW;
  } else if (GO_LEFT(analogRead(X_PIN)) && debounce(&lastXInputTime)) {
    // go back to main menu
    lcd.clear();
    cursorLine = 0;
    currLcdView = MENU;
    } else if (GO_DOWN(analogRead(Y_PIN)) && debounce(&lastYInputTime)) {
    // scroll down
    if (cursorLine == 0) {
      moveCursor();  // move cursor to second line
    } else {
      // scroll page
      sensorOnTopLcdLineID = (sensorOnTopLcdLineID + 1) % NUM_SENSORS;
      lcd.clear();
      printSensorPreviews();
    }
  } else if (GO_UP(analogRead(Y_PIN)) && debounce(&lastYInputTime)) {
    // scroll up
    if (cursorLine == 1) {
      moveCursor();  // move cursor to first line
    } else {
      // scroll page
      if (sensorOnTopLcdLineID == 0) {
        sensorOnTopLcdLineID = 5;
      } else {
        sensorOnTopLcdLineID--;
      }
      lcd.clear();
      printSensorPreviews();
    }
  }
}

// Name: printSensorView
// Description: prints individual sensor info to screen and handles navigation logic
void printSensorView() {
  SensorInfo sensorInfo = sensorValues[sensorOnTopLcdLineID];

  // print back arrow
  lcd.setCursor(0, 0);
  lcd.print("<");

  // print sensor name
  lcd.print(sensorInfo.name);
  lcd.print(":");

  // print sensor value
  lcd.setCursor(1, 1);
  lcd.print(sensorInfo.value);

  if (GO_LEFT(analogRead(X_PIN)) && debounce(&lastXInputTime)) {
    // go back to menu
    lcd.clear();
    cursorLine = 0;
    currLcdView = SENSOR_MENU;
  }
}

// Name: printSongMenu
// Description: prints song menu screen to lcd and handles navigation logic
void printSongMenu() {
  lcd.setCursor(0, 0);
  lcd.print("<");
  printSongPreviews();
  if (!digitalRead(SW_PIN) && debounce(&lastSwInputTime)) {
    // go to the view of that song
    lcd.clear();
    songOnTopLcdLineID = (songOnTopLcdLineID + cursorLine) % 3;
    cursorLine = 0;
    currLcdView = SONG_VIEW;
  } else if (GO_LEFT(analogRead(X_PIN)) && debounce(&lastXInputTime)) {
    // go back to main menu
    lcd.clear();
    cursorLine = 0;
    currLcdView = MENU;
  } else if (GO_DOWN(analogRead(Y_PIN)) && debounce(&lastYInputTime)) {
    // scroll down
    if (cursorLine == 0) {
      moveCursor();  // move cursor to second line
    } else {
      // scroll page
      songOnTopLcdLineID = (songOnTopLcdLineID + 1) % 3;
      lcd.clear();
      printSongPreviews();
    }
  } else if (GO_UP(analogRead(Y_PIN)) && debounce(&lastYInputTime)) {
    // scroll up
    if (cursorLine == 1) {
      moveCursor();  // move cursor to first line
    } else {
      // scroll page
      if (songOnTopLcdLineID == 0) {
        sensorOnTopLcdLineID = 2;
      } else {
        songOnTopLcdLineID--;
      }
      lcd.clear();
      printSongPreviews();
    }
  }
}

// Name: printSongView
// Description: prints individual song info to screen and handles navigation logic
void printSongView() {
  char* song = songs[songOnTopLcdLineID];  // get selected song

  // print back arrow
  lcd.setCursor(0, 0);
  lcd.print("<");

  lcd.print("Click to send");

  lcd.setCursor(1, 1);
  lcd.print(song);

  if (!digitalRead(SW_PIN) && debounce(&lastSwInputTime)) {
    // send song
    esp_now_send(broadcastAddress, (uint8_t *)song, strlen(song)); 
    // print confirmation message and return to song menu
    lcd.clear();
    lcd.print("Song sent!");
    vTaskDelay(800 / portTICK_PERIOD_MS);
    lcd.clear();
    cursorLine = 0;
    currLcdView = SONG_MENU;
  }
  if (GO_LEFT(analogRead(X_PIN)) && debounce(&lastXInputTime)) {
    // go back to song menu
    lcd.clear();
    cursorLine = 0;
    currLcdView = SONG_MENU;
  }
}

// Name: printAlarm
// Description: prints alarm display to screen and handles navigation logic 
// involving seeing which sensors set off the alarm and resetting the alarm.
// Handles sending song to baby in response to alarm
void printAlarm() {
  lcd.setCursor(0, 0);
  lcd.print("<Alarm caused by:");
  lcd.setCursor(1, 1);
  // get sensors that caused alarm
  String sensorInitials = "";
  for (int i = 0; i < NUM_SENSORS; i++) {
    SensorInfo sensor = sensorValues[i];
    if (sensor.alarm) {
      sensorInitials += sensor.name[0];
      sensorInitials += " ";
    }
  }

  lcd.print(sensorInitials.c_str());
  
  if (GO_LEFT(analogRead(X_PIN)) && debounce(&lastXInputTime)) {
    // attempt to reset and go back to menu
    lcd.clear();
    cursorLine = 0;
    // reset sensors alarm flags
    for (int i = 0; i < NUM_SENSORS; i++) {
      sensorValues[i].alarm = false;
    }
    currLcdView = MENU;
  } else if (!digitalRead(SW_PIN) && debounce(&lastSwInputTime)) {
    // send song to baby
    esp_now_send(broadcastAddress, (uint8_t *)songs[0], strlen(songs[0])); 
    lcd.clear();
    lcd.print(songs[0]);
    lcd.setCursor(0, 1);
    lcd.print("sent to baby!");
    vTaskDelay(800 / portTICK_PERIOD_MS);
    lcd.clear();
  } else if (GO_UP(analogRead(Y_PIN)) && debounce(&lastYInputTime)) {
    // send song to baby
    esp_now_send(broadcastAddress, (uint8_t *)songs[1], strlen(songs[1]));
    lcd.clear();
    lcd.print(songs[1]);
    lcd.setCursor(0, 1);
    lcd.print("sent to baby!");
    vTaskDelay(800 / portTICK_PERIOD_MS);
    lcd.clear();
  } else if (GO_DOWN(analogRead(Y_PIN)) && debounce(&lastYInputTime)) {
    // send song to baby
    esp_now_send(broadcastAddress, (uint8_t *)songs[2], strlen(songs[2]));
    lcd.clear();
    lcd.print(songs[2]);
    lcd.setCursor(0, 1);
    lcd.print("sent to baby!");
    vTaskDelay(800 / portTICK_PERIOD_MS);
    lcd.clear();
  }
}

// Name: buzzAlarmTask
// Description: FreeRTOS task that hanldles buzzing the alarm
void buzzAlarmTask(void *arg) {
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (currLcdView == ALARM) {
      // sound alarm
      tone(BUZZER_PIN, 1000);
      vTaskDelay(200 / portTICK_PERIOD_MS);
      noTone(BUZZER_PIN);
      vTaskDelay(200 / portTICK_PERIOD_MS);
    }
  }
}

// Name: lcdDisplayTask
// Description: FreeRTOS task that hanldles lcd display
void lcdDisplayTask(void *arg) {
  while (true) {
    xSemaphoreTake(lcdSemaphore, portMAX_DELAY);
    if (currLcdView == MENU) {
      lcd.setCursor(0, cursorLine);
      lcd.print(">");
      printMenu();
    } else if (currLcdView == SENSOR_MENU) {
      lcd.setCursor(1, cursorLine);
      lcd.print(">");
      printSensorMenu();
    } else if (currLcdView == SENSOR_VIEW) {
      printSensorView();
    } else if (currLcdView == SONG_MENU) {
      lcd.setCursor(1, cursorLine);
      lcd.print(">");
      printSongMenu();
    } else if (currLcdView == SONG_VIEW) {
      printSongView();
    } else if (currLcdView == ALARM) {
      printAlarm();
    }
    xSemaphoreGive(lcdSemaphore);
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

// Name: checkNoise
// Description: returns if sensor value should trigger an alarm
bool checkNoise(float value) {
  bool trigger = false;
  xSemaphoreTake(lcdSemaphore, portMAX_DELAY);
  if (currLcdView != ALARM && value > 3500) {
    lcd.clear();
    currLcdView = ALARM;
    sensorValues[NOISE].alarm = true;
    trigger = true;
  }
  xSemaphoreGive(lcdSemaphore);
  return trigger;
}

// Name: checkTempurature
// Description: returns if sensor value should trigger an alarm
bool checkTempurature(float value) {
  bool trigger = false;
  xSemaphoreTake(lcdSemaphore, portMAX_DELAY);
  if (currLcdView != ALARM && (value > 90 || value < 60)) {
    lcd.clear();
    currLcdView = ALARM;
    sensorValues[TEMP].alarm = true;
    trigger = true;
  }
  xSemaphoreGive(lcdSemaphore);
  return trigger;
}

// Name: checkHumidity
// Description: returns if sensor value should trigger an alarm
bool checkHumidity(float value) {
  bool trigger = false;
  xSemaphoreTake(lcdSemaphore, portMAX_DELAY);
  if (currLcdView != ALARM && (value < 30 || value > 60)) {
    lcd.clear();
    currLcdView = ALARM;
    sensorValues[HUMIDITY].alarm = true;
    trigger = true;
  }
  xSemaphoreGive(lcdSemaphore);
  return trigger;
}

// Name: checkMotion
// Description: returns if sensor value should trigger an alarm
bool checkMotion(float value) {
  bool trigger = false;
  xSemaphoreTake(lcdSemaphore, portMAX_DELAY);
  if (currLcdView != ALARM && value != 0) {
    lcd.clear();
    currLcdView = ALARM;
    sensorValues[MOTION].alarm = true;
    trigger = true;
  }
  xSemaphoreGive(lcdSemaphore);
  return trigger;
}

// Name: checkWater
// Description: returns if sensor value should trigger an alarm
bool checkWater(float value) {
  bool trigger = false;
  xSemaphoreTake(lcdSemaphore, portMAX_DELAY);
  if (currLcdView != ALARM && value > 300) {
    lcd.clear();
    currLcdView = ALARM;
    sensorValues[WATER].alarm = true;
    trigger = true;
  }
  xSemaphoreGive(lcdSemaphore);
  return trigger;
}

// Name: checkLight
// Description: returns if sensor value should trigger an alarm
bool checkLight(float value) {
  bool trigger = false;
  xSemaphoreTake(lcdSemaphore, portMAX_DELAY);
  if (currLcdView != ALARM && value > 3500) {
    lcd.clear();
    currLcdView = ALARM;
    sensorValues[LIGHT].alarm = true;
    trigger = true;
  }
  xSemaphoreGive(lcdSemaphore);
  return trigger;
}

// Setup function
void setup() {
  Serial.begin(115200); // Initialize serial communication

  // Initialize LCD
  lcd.init();
  lcd.backlight();

  // Initialize Wi-Fi
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  // Initialize pins
  pinMode(X_PIN, INPUT); 
  pinMode(Y_PIN, INPUT); 
  pinMode(SW_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  noTone(BUZZER_PIN);

  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
      Serial.println("Error initializing ESP-NOW");
  }

  // Register callback function to handle received data
  esp_now_register_recv_cb(onDataRecv);

  // Set up esp-now for sending
  esp_now_register_send_cb(onDataSent); // Register the send callback function
  // Configure peer information
  esp_now_peer_info_t peerInfo; // Data structure for handling peer information
  memset(&peerInfo, 0, sizeof(peerInfo)); // Clear peerInfo structure
  memcpy(peerInfo.peer_addr, broadcastAddress, 6); // Copy MAC address of the receiver
  peerInfo.channel = 0; // Set WiFi channel to 0 (default)
  peerInfo.encrypt = false; // Disable encryption
  // Add peer to ESP-NOW
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("adding peer failed");
  }

  // Create the queue
  sensorDataQueue = xQueueCreate(20, sizeof(incomingMessage));
  if (sensorDataQueue == NULL) {
    Serial.println("Failed to create the queue");
  }

  // Create the semaphore
  lcdSemaphore = xSemaphoreCreateBinary();

  // pin tasks
  xTaskCreatePinnedToCore(lcdDisplayTask, "LCD Display Task", 4096, NULL, 1, &lcdDisplayTaskHandle, 0);
  xTaskCreatePinnedToCore(buzzAlarmTask, "Buzz Alarm Task", 2024, NULL, 1, &buzzAlarmTaskHandle, 1);
  xTaskCreatePinnedToCore(receiveDataTask, "Receive Data Task", 2024, NULL, 1, &receiveDataTaskHandle, 1);

  // timers
  timer32Hz = timerBegin(0, 80, true); // 80 prescaler, counts at 1 MHz
  timerAttachInterrupt(timer32Hz, &onTimer32Hz, true);
  timerAlarmWrite(timer32Hz, 31250, true); // 31.250 ms for 32 Hz
  timerAlarmEnable(timer32Hz);

  xSemaphoreGive(lcdSemaphore);
}

void loop() {}
