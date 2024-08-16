// File: BabySide.ino
// Authors: Caitlyn Rawlings, Hao Tian
// Date: 8/12/24
// Description: This files defines the baby side of a baby monitoring system. 
//    It reads values from sensors in the babys room and send them to the parent side
//    It also plays a song to the baby if instructed by the parent side

// ============== Includes ==============
#include <esp_now.h>
#include <WiFi.h>
#include <Arduino.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <LiquidCrystal_I2C.h>
#include <Bonezegei_DHT11.h>  // temp and humidity
#include "songs.h"  // for playSong(int buzzerPin, int songNum)


// =============== Macros ================
#define NUM_SENSORS 6  // number of sensors getting input from

// for pins using: gpio number (legacy)
// sensors pins
#define SOUND_ANA_PIN 3 // sound detector
#define WATER_PIN 1 // water detector
#define MOTION_PIN 7 // motion detector
#define LIGHT_PIN 2 // light level
#define DHT_PIN 6    // Define the pin where the temp/humidity sensor
#define BUZZER_PIN 9

// joystick pins
#define X_PIN 13  //the VRX attach to
#define Y_PIN 14  //the VRY attach to
#define SW_PIN 17  //the SW attach to

// for determing joystick direction
#define GO_UP(y) ((y) < 400)
#define GO_DOWN(y) ((y) == 4095)
#define GO_LEFT(x) ((x) < 300)


// ============= Data Types ===============
// defines the possible lcd views options
enum lcdView {
    MENU,  // menu displays the all the sensor names
    SENSOR_VIEW  // view of one sensor and its value
};

// defines sensor ids
enum sensorIDs { NOISE, TEMP, HUMIDITY, MOTION, WATER, LIGHT };

// stores information associated with a sensor
typedef struct {
  sensorIDs id;
  char name[13];  // name to display for a sensor
  float value;  // value of sensor. updated as values are read
} SensorInfo;


// =============== Global Variables ================
uint8_t broadcastAddress[6] = {0xEC, 0xDA, 0x3B, 0x60, 0xD3, 0xAC};  // address of parent board
LiquidCrystal_I2C lcd(0x27, 16, 2);
Bonezegei_DHT11 dht(DHT_PIN);

bool messageReceived = false;  // flag for if a message was received from parent board
char incomingMessage[18];  // Buffer to hold the incoming message

enum lcdView currLcdView = MENU;  // initial lcdView will be start menu
int sensorOnTopLcdLineID = 0;  // sensor that will be on top line of lcd when menu displayed
volatile int cursorLine = 0;  // indicator of which line of lcd is selected

// for tracking last user input times for deboucing
int lastYInputTime = 0;
int lastXInputTime = 0;
int lastSwInputTime = 0;

// timer
hw_timer_t *timer50Hz = NULL;

// task handles
TaskHandle_t readSensorsTaskHandle;
TaskHandle_t playSongTaskHandle;
TaskHandle_t lcdDisplayTaskHandle;
TaskHandle_t processSensorInfoTaskHandle;

// queue handle
QueueHandle_t sensorQueue;

// stores sensor info for all sensors
SensorInfo sensorValues[6] = { 
  {NOISE, "Noise level", 0},
  {TEMP, "Tempurature", 0},
  {HUMIDITY, "Humidity", 0},
  {MOTION, "Motion", 0},
  {WATER, "Water level", 0},
  {LIGHT, "Light level", 0}
};


// ============ Function Prototypes =============
void IRAM_ATTR onTimer50Hz(); 
void IRAM_ATTR onDataReceived(const uint8_t * mac, const uint8_t *incomingData, int len);
void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status);
bool debounce(int *lastInputTime);
void moveCursor();
void printSensorPreviews();
void printMenu();
void printSensorView();
void lcdDisplayTask(void *arg);
void playSongTask(void *arg);
void readSensorsTask(void *arg);
void processSensorInfoTask(void *arg);


// Name: onTimer50Hz
// Description: ISR for Timer. Triggers playSongTask to run
void IRAM_ATTR onTimer50Hz() {
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  vTaskNotifyGiveFromISR(playSongTaskHandle, &xHigherPriorityTaskWoken);
  if (xHigherPriorityTaskWoken) {
      portYIELD_FROM_ISR();
  }
}

// Name: onDataReceived
// Definition: ISR that gets called when a message is received. Updates the messageReceived flag and stores the message
void IRAM_ATTR onDataReceived(const uint8_t * mac, const uint8_t *incomingData, int len) {
  // Copy the incoming data into the buffer
  memcpy(incomingMessage, (char*)incomingData, len);
  incomingMessage[len] = '\0';  // Null-terminate the string
  messageReceived = true;  // Update messageReceived flag
}

// Name: onDataSent
// Description: Callback function for sending data to parent board
void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {}

// Name: debounce
// Description: Checks if the specified debounce interval has passed 
// since the last recorded input time. If sufficient time has elapsed,
// it updates the last input time and returns true, otherwise, returns false.
bool debounce(int *lastInputTime) {
  int currentTime = millis();
  if (currentTime - *lastInputTime > 500) {
    *lastInputTime = currentTime;  // update last input time
    return true;
  }
  return false;
}

// Name: moveCursor
// Description: moves cursor on lcd to the line its not on.
// From line 0 to 1, or 1 to 0
void moveCursor() {
  // delete current cursor
  lcd.setCursor(0, cursorLine);
  lcd.print(" ");
  // calculate new cursor line
  cursorLine = (cursorLine + 1) % 2;  // F(0) = 1; F(1) = 0
  // place new cursor
  lcd.setCursor(0, cursorLine);
  lcd.print(">");
}

// Name: printSensorPreviews
// Description: prints two sensor names to the lcd
void printSensorPreviews() {
  SensorInfo sensorInfo1 = sensorValues[sensorOnTopLcdLineID];  // get first line sensor
  SensorInfo sensorInfo2 = sensorValues[(sensorOnTopLcdLineID + 1) % NUM_SENSORS];  // get second line sensor

  // print sensor names
  lcd.setCursor(1, 0);
  lcd.print(sensorInfo1.name);

  lcd.setCursor(1, 1);
  lcd.print(sensorInfo2.name);
}

// Name: printMenu
// Description: prints menu screen to lcd and handles navigation logic
void printMenu() {
  printSensorPreviews();
  lcd.setCursor(0, cursorLine);
  lcd.print(">");
  if (!digitalRead(SW_PIN) && debounce(&lastSwInputTime)) {
    // go to the view of that sensor
    lcd.clear();
    sensorOnTopLcdLineID = (sensorOnTopLcdLineID + cursorLine) % NUM_SENSORS;
    currLcdView = SENSOR_VIEW;
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
    currLcdView = MENU;
  }
}

// Name: lcdDisplayTask
// Description: FreeRTOS task that hanldles lcd display
void lcdDisplayTask(void *arg) {
  while (true) {
    
    if (currLcdView == MENU) {
      printMenu();
    } else if (currLcdView == SENSOR_VIEW) {
      printSensorView();
    }
    
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

// Name: playSongTask
// Description: FreeRTOS task that plays song if inscructed to by parent board
void playSongTask(void *arg) {
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (messageReceived) {
      // check which song parent board sent
      if (!strcmp(incomingMessage, "Little Star")) {
        playSong(BUZZER_PIN, 1);
      } else if (!strcmp(incomingMessage, "Mary's Lamb")) {
        playSong(BUZZER_PIN, 2);
      } else if (!strcmp(incomingMessage, "Wheels on Bus")) {
        playSong(BUZZER_PIN, 3);
      } else {
        Serial.println("Invalid song selection.");
      }
      messageReceived = false;  // reset flag
    }
  }
}

// Name: readSensorsTask
// Description: FreeRTOS task that updates sensor values and sends to queue
void readSensorsTask(void *arg) {
  while (true) {
    // Read sensor data and sent to queue
    sensorValues[LIGHT].value = analogRead(LIGHT_PIN);
    xQueueSend(sensorQueue, &sensorValues[LIGHT], portMAX_DELAY);

    dht.getData();  // update temp and humidity

    sensorValues[TEMP].value = dht.getTemperature(true);
    xQueueSend(sensorQueue, &sensorValues[TEMP], portMAX_DELAY);

    sensorValues[HUMIDITY].value = dht.getHumidity();
    xQueueSend(sensorQueue, &sensorValues[HUMIDITY], portMAX_DELAY);

    sensorValues[WATER].value = analogRead(WATER_PIN);
    xQueueSend(sensorQueue, &sensorValues[WATER], portMAX_DELAY);

    sensorValues[MOTION].value = digitalRead(MOTION_PIN);
    xQueueSend(sensorQueue, &sensorValues[MOTION], portMAX_DELAY);

    sensorValues[NOISE].value = analogRead(SOUND_ANA_PIN);
    xQueueSend(sensorQueue, &sensorValues[NOISE], portMAX_DELAY);

    vTaskDelay(300 / portTICK_PERIOD_MS);
  }
}

// Name: processSensorInfoTask
// Description: FreeRTOS task that processes data from queue and and sends it to parent board
void processSensorInfoTask(void *arg) {
  SensorInfo sensorInfo;
  String outgoingMessage;

  while (true) {
    if (xQueueReceive(sensorQueue, &sensorInfo, portMAX_DELAY)) {
      // Construct the outgoing message
      outgoingMessage = String(sensorInfo.id) + ":" + String(sensorInfo.value);
      esp_now_send(broadcastAddress, (uint8_t *)outgoingMessage.c_str(), outgoingMessage.length());
    }
    vTaskDelay(50 / portTICK_PERIOD_MS);
  }
}

void setup() {
  Serial.begin(9600); // Initialize serial communication

  WiFi.mode(WIFI_STA); // Set WiFi mode to station

  dht.begin();  // Initialize the temp and humidity reader

  // initialize lcd
  lcd.init(); 
  lcd.clear();
  lcd.setCursor(0, 0);  
  lcd.backlight();

  // sensor pins
  pinMode(LIGHT_PIN, INPUT);
  pinMode(WATER_PIN, INPUT);
  pinMode(MOTION_PIN, INPUT);
  pinMode(SOUND_ANA_PIN, INPUT);
  pinMode(DHT_PIN, INPUT); 

  // joystick pins
  pinMode(X_PIN, INPUT); 
  pinMode(Y_PIN, INPUT); 
  pinMode(SW_PIN, INPUT_PULLUP); 

  // buzzer pin
  pinMode(BUZZER_PIN, OUTPUT); 


  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
      return; // Return if ESP-NOW initialization fails
  }
  delay(2000); // Delay to ensure stable setup

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

  // set up esp-now for receiving
  esp_now_register_recv_cb(onDataReceived);

  // Create the queue
  sensorQueue = xQueueCreate(10, sizeof(SensorInfo));
  if (sensorQueue == NULL) {
    Serial.println("Failed to create the queue");
  }


  // pin tasks 
  xTaskCreatePinnedToCore(readSensorsTask, "Read Sensors Task", 4096, NULL, 1, &readSensorsTaskHandle, 0);
  xTaskCreatePinnedToCore(playSongTask, "Play Song Task", 8192, NULL, 1, &playSongTaskHandle, 1);
  xTaskCreatePinnedToCore(lcdDisplayTask, "LCD Display Task", 4096, NULL, 1, &lcdDisplayTaskHandle, 0);
  xTaskCreatePinnedToCore(processSensorInfoTask, "Process Sensor Data Task", 4096, NULL, 1, &processSensorInfoTaskHandle, 0);

  // timer
  timer50Hz = timerBegin(0, 80, true); // 80 prescaler, counts at 1 MHz
  timerAttachInterrupt(timer50Hz, &onTimer50Hz, true);
  timerAlarmWrite(timer50Hz, 20000, true); // 20 ms for 50 Hz
  timerAlarmEnable(timer50Hz);
}

void loop() {}
