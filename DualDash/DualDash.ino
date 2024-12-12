#include <WiFi.h>
#include <esp_now.h>
#include <TFT_eSPI.h>  // Graphics and font library for ST7735 driver chip
#include <SPI.h>
#include <Wire.h>

// NFC reader Code
#include <PN532_I2C.h>
#include <PN532.h>
#include <NfcAdapter.h>

PN532_I2C pn532i2c(Wire);
PN532 nfc(pn532i2c);
// #endif

// TFT setup
TFT_eSPI tft = TFT_eSPI();
#define TFT_BLACK 0x0000
#define TFT_WHITE 0xFFFF

// SemaphoreHandle_t mutex;  // Declare a mutex handle

// Constants
#define SHORT_PRESS_TIME 500  // 500 milliseconds
#define LONG_PRESS_TIME  3000 // 3000 milliseconds
// Hardware pins
#define BUTTON_LEFT 0
#define BUTTON_RIGHT 35
#define BUTTON_PIN 2       // Button for shooting
#define LED_PIN 15         // LED to flash at milestone
#define POTENTIOMETER_PIN 13         // Potentiometer to adjust game speed

struct Avatar {
    uint8_t tagID[7];  // Unique NFC tag ID
    int health;    // Health of the avatar
};

Avatar avatars[] = {
    {{0, 0, 0, 0, 0, 0, 0}, 100},  // Avatar 1 with 100 health
    {{0, 0, 0, 0, 0, 0, 0}, 100},  // Avatar 2 with 100 health
    {{0, 0, 0, 0, 0, 0, 0}, 100}   // Avatar 3 with 100 health
};

// Button states
int lastLeftState = LOW;  // the previous state from the input pin
int lastRightState = LOW;
int lastButtonState = LOW;
unsigned long pressedTime  = 0;
unsigned long releasedTime = 0;

uint8_t zeroArr[7] = {0};

struct AttackMessage {
  uint8_t avatarID;    // ID of the attacked avatar
  uint8_t attackPower; // Power of the incoming attack
};

// Game variables
int currAvatarHealth = avatars[0].health; // Player's total energy
int energy = 100;                         // Player's energy
bool buttonPressed = false;               // Button state
int currAvatarInd = -1;                   // Current Avatar Selected

int attackPowerArr[3] = {5, 20, 40};
int energyUsage[3] = {1, 15, 30};
int attackEnergyPairInd = 0;

int attackPower = attackPowerArr[attackEnergyPairInd];  // Current attack power

enum ScreenState {
  GAME_SCREEN,
  END_SCREEN
};

enum WinState {
  PLAYER_NONE,
  WINNING_PLAYER,
  LOSING_PLAYER,
};

ScreenState currentScreen = GAME_SCREEN;
WinState endScreenState = PLAYER_NONE;
volatile bool connected = false;
int prevTime;

// Draw & Control Handle Related Functions
// For Screen
void drawGameScreen();
void handleGameScreen();
// void drawControls();
void drawWinScreen();
void handleWinScreen();

void sendAttackRequest() {
  String cmd1 = "D: A" + String(attackPower);  // Example: Attack command to the other player
  energy = max(0, energy - energyUsage[attackEnergyPairInd]);
  broadcast(cmd1);  // Send the attack command to the other player
  Serial.println("Sent an attack request!");
  tft.fillScreen(TFT_BLACK);
}

void sendBoostRequest() {
  return;
}

void receiveCallback(const esp_now_recv_info_t *info, const uint8_t *data, int dataLen) {
  // if (xSemaphoreTake(mutex, portMAX_DELAY)) {  // Take the mutex
    char buffer[ESP_NOW_MAX_DATA_LEN + 1];
    int msgLen = min(ESP_NOW_MAX_DATA_LEN, dataLen);
    strncpy(buffer, (const char *)data, msgLen);

    buffer[msgLen] = 0;
    String recvd = String(buffer);
    
    char macStr[18];

    if (recvd[0] == 'D') {  // This is a command to reduce health or modify stats
      // If the received command is related to an attack, apply the attack
      if (recvd.substring(3, 4) == "A") {
        Serial.println("Received an attack!");
        // Assume the other player is attacking, reduce their health
        avatars[currAvatarInd].health -= recvd.substring(4).toInt();  // Decrease health by 10
        if (avatars[currAvatarInd].health  < 0) avatars[currAvatarInd].health  = 0;  // Ensure health doesn't go below 0
        tft.fillScreen(TFT_BLACK);
      } else if (recvd.substring(3) == "B") {
        // Handle another attack type
        // attackStrength += 1;  // Example logic for another type of attack
        // redrawControls();  // Redraw to show updated attack strength
        tft.fillScreen(TFT_BLACK);
      }
    }
    // xSemaphoreGive(mutex);
  // }

      if (recvd.startsWith("D: GAME_OVER")) {
        Serial.println("Game Over message received!");
        currentScreen = END_SCREEN;

        if (recvd == "D: GAME_OVER_LOSE") {
          // If we receive LOSE from the other side, that means WE won.
          endScreenState = WINNING_PLAYER;
          currentScreen = END_SCREEN;
          drawWinScreen(); // shows "You Win!"
        } else if (recvd == "D: GAME_OVER_WIN") {
          // If we receive WIN from the other side, that means WE lost.
          endScreenState = LOSING_PLAYER;
          currentScreen = END_SCREEN;
          drawWinScreen(); // shows "You Lose..."
        }
      }
  delay(100);
}

void sentCallback(const uint8_t *macAddr, esp_now_send_status_t status) {
  // Optional: Handle send status
  Serial.println("ESP32 broadcasted a message!");
  return;
}

void broadcast(const String &message)
{
  uint8_t broadcastAddress[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
  esp_now_peer_info_t peerInfo = {};
  memcpy(&peerInfo.peer_addr, broadcastAddress, 6);
  if (!esp_now_is_peer_exist(broadcastAddress)) {
    esp_now_add_peer(&peerInfo);
  }
  esp_now_send(broadcastAddress, (const uint8_t *)message.c_str(), message.length());
}


void espnowSetup() {
// Set ESP32 in STA mode
  delay(500);
  WiFi.mode(WIFI_STA);
  delay(500);
  Serial.println("ESP-NOW Broadcast Demo");

  // Disconnect from WiFi
  WiFi.disconnect();

  // Initialize ESP-NOW
  if (esp_now_init() == ESP_OK) {
    Serial.println("ESP-NOW Init Success");
    esp_now_register_recv_cb(receiveCallback);
    esp_now_register_send_cb(sentCallback);
  } else {
    Serial.println("ESP-NOW Init Failed");
    delay(3000);
    ESP.restart();
  }
}

void textSetup() {
  // Initialize TFT display
  tft.init();  // Init ST7789 with 240x240 resolution
  tft.setRotation(1); // Adjust as needed
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
}

void buttonSetup() {
  // Initialize hardware pins
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUTTON_LEFT, INPUT_PULLUP);
  pinMode(BUTTON_RIGHT, INPUT_PULLUP);
}
void setup() {
  Serial.begin(115200);

  prevTime = millis();
  textSetup();
  buttonSetup();
  espnowSetup();

}

void readNFCTag() {
  boolean success;
  // Buffer to store the UID
  uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };
  // UID size (4 or 7 bytes depending on card type)
  uint8_t uidLength;

  // while (!connected) {
  connected = connect();
  // }

  if (connected == false) {
    return;
  }

  // Wait for an ISO14443A type cards (Mifare, etc.).  When one is found
  // 'uid' will be populated with the UID, and uidLength will indicate
  // if the uid is 4 bytes (Mifare Classic) or 7 bytes (Mifare Ultralight)
  success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, &uid[0], &uidLength);

  // If the card is detected, print the UID
  if (success)
  {
    Serial.println("Card Detected");
    Serial.print("Size of UID: "); Serial.print(uidLength, DEC);
    Serial.println(" bytes");
    Serial.print("UID: ");
    for (uint8_t i = 0; i < uidLength; i++)
    {
      Serial.print(" 0x"); Serial.print(uid[i], HEX);
    }
    Serial.println("");
    Serial.println("");

    bool tagFilled = false;
    for (int i = 0; i < 3; i++) {
      if (memcmp(avatars[i].tagID, uid, 7) == 0) {
        currAvatarHealth = avatars[i].health;
        currAvatarInd = i;
        Serial.println("Tag is already filled in the tag set");
        tagFilled = true;
        tft.fillScreen(TFT_BLACK);
        break;
      }
      else if (memcmp(avatars[i].tagID, zeroArr, 7) == 0) {
        memcpy(avatars[i].tagID, uid, 7);
        currAvatarHealth = avatars[i].health;
        currAvatarInd = i;
        Serial.println("Tag is being filled in the tag set!");
        tagFilled = true;
        tft.fillScreen(TFT_BLACK);
        break;
      }
    }

    if (!tagFilled) {
      Serial.println("The tag set is fully occupied!");
    }
    
    connected = false;
  }
  else
  {
    // PN532 probably timed out waiting for a card
    Serial.println("Timed out waiting for a card");
  }
}

bool connect() {
  
  nfc.begin();

  // Connected, show version
  uint32_t versiondata = nfc.getFirmwareVersion();
  if (!versiondata)
  {
    Serial.println("PN53x card not found!");
    return false;
  }

  //port
  Serial.print("Found chip PN5"); Serial.println((versiondata >> 24) & 0xFF, HEX);
  Serial.print("Firmware version: "); Serial.print((versiondata >> 16) & 0xFF, DEC);
  Serial.print('.'); Serial.println((versiondata >> 8) & 0xFF, DEC);

  // Set the max number of retry attempts to read from a card
  // This prevents us from waiting forever for a card, which is
  // the default behaviour of the PN532.
  nfc.setPassiveActivationRetries(0x11);

  // configure board to read RFID tags
  nfc.SAMConfig();

  Serial.println("Waiting for card (ISO14443A Mifare)...");
  Serial.println("");

  return true;
}

void detectInputs(){
  // Potentiometer isn't working so can't test this code
  // out even though it theoretically works :(
  // int potValue = analogRead(POTENTIOMETER_PIN);
  // int atkVal = map(potValue, 0, 4095, 1, maxAttackPower);
  // Serial.println("Potentiometer value is: " + String(potValue));
  // Serial.println("Attack value is " + String(atkVal));
  
  // if (attackPower != atkVal) {
  //   attackPower = atkVal;
  //   tft.fillScreen(TFT_BLACK);
  //   Serial.println("Potentiometer Value has changed.");
  // }

  int currentLeftState = digitalRead(BUTTON_LEFT);
  // Detect left button press
  if (lastLeftState == HIGH && currentLeftState == LOW)       // button is pressed
    pressedTime = millis();
  else if (lastLeftState == LOW && currentLeftState == HIGH) { // button is released
    releasedTime = millis();
    long pressDuration = releasedTime - pressedTime;

    if ( pressDuration < SHORT_PRESS_TIME && currAvatarInd != -1) {
      // TODO Send Attack Request
      Serial.println("Left Button Short Press");
      sendAttackRequest();
    }

    if ( pressDuration > LONG_PRESS_TIME ){
      lastLeftState = currentLeftState;
      Serial.println("Left Button Long Press");
      return;
    }
  }

  lastLeftState = currentLeftState;

  int currentRightState = digitalRead(BUTTON_RIGHT);

  // Detect right button press
  if (lastRightState == HIGH && currentRightState == LOW)       // button is pressed
    pressedTime = millis();
  else if (lastRightState == LOW && currentRightState == HIGH) { // button is released
    releasedTime = millis();
    long pressDuration = releasedTime - pressedTime;

    if ( pressDuration < SHORT_PRESS_TIME ) {
      lastRightState = currentRightState;
      Serial.println("Right Button Short Press");
      attackEnergyPairInd = (attackEnergyPairInd + 1) % 3;
      attackPower = attackPowerArr[attackEnergyPairInd];
      tft.fillScreen(TFT_BLACK);
      return;
    }

    if ( pressDuration > LONG_PRESS_TIME ){
      currAvatarInd = (currAvatarInd + 1) % 3;
      tft.fillScreen(TFT_BLACK);
      Serial.println("Right Button Long Press");
    }
  }
  lastRightState = currentRightState;

  int buttonState = digitalRead(BUTTON_PIN);
  // Detect right button press
  if (lastButtonState == HIGH && buttonState == LOW)       // button is pressed
    pressedTime = millis();
  else if (lastButtonState == LOW && buttonState == HIGH) { // button is released
    releasedTime = millis();
    long pressDuration = releasedTime - pressedTime;

    if ( pressDuration < SHORT_PRESS_TIME ) {
      Serial.println("Button Pin Short Press");
      sendAttackRequest();
    }
    else if ( pressDuration > LONG_PRESS_TIME) {
      lastButtonState = buttonState;
      Serial.println("Button Pin Long Press");
      attackEnergyPairInd = (attackEnergyPairInd + 1) % 3;
      attackPower = attackPowerArr[attackEnergyPairInd];
      tft.fillScreen(TFT_BLACK);
      return;
    }
  }
  lastButtonState = buttonState;
}

void handleGameScreen() {
  drawGameScreen();
  detectInputs();
  int currTime = millis();
  if (currTime - prevTime > 3000) {
    readNFCTag();
    prevTime = currTime;
  }
}

void drawGameScreen() {
    int16_t cursorX = 10;
    int16_t cursorY = 10;
    tft.setCursor(cursorX, cursorY);
    tft.print("Energy: ");
    tft.println(energy);

    cursorY += 30;

    tft.setCursor(cursorX, cursorY);
    tft.print("Attack Power: ");
    tft.println(attackPower);

    cursorY += 30;

    tft.setCursor(cursorX, cursorY);
    if (currAvatarInd != -1 ) { 
      tft.print("Current Avatar: A" + String(currAvatarInd) + "\n");
      cursorY += 30;
      tft.setCursor(cursorX, cursorY);
      tft.print("Avatar Health: " + String(avatars[currAvatarInd].health));
    }
    else {
      tft.print("No Avatar Selected\n");
    }
}

void checkGameOver() {
  bool avatarLost = false;
  for (int i = 0; i < 3; i++) {
    if (avatars[i].health <= 0) {
      avatarLost = true;
      break;
    }
  }

  if (avatarLost || energy <= 0) {
    endScreenState = LOSING_PLAYER;
    sendGameOver();
    currentScreen = END_SCREEN;
    String message;
    message = "D: GAME_OVER_LOSE";
    broadcast(message);
    drawWinScreen();
  }

}

void sendGameOver() {
  String message;
  if (endScreenState == LOSING_PLAYER) {
    // I lost, so the other must have won.
    message = "D: GAME_OVER_LOSE"; 
  } else if (endScreenState == WINNING_PLAYER) {
    message = "D: GAME_OVER_WIN";
  } else {
    message = "D: GAME_OVER_NONE";
  }
  broadcast(message);
  Serial.println("Game Over message sent: " + message);
}

void drawWinScreen() {
  tft.fillScreen(TFT_BLACK); 
  tft.setTextSize(3);
  int16_t cursorX = 50;
  int16_t cursorY = 55;
  tft.setCursor(cursorX, cursorY);
  
  switch(endScreenState) {
    case WINNING_PLAYER:
      tft.setTextColor(TFT_GREEN); 
      tft.println("You Win!");
      break;
    case LOSING_PLAYER:
      tft.setTextColor(TFT_RED); 
      tft.println("You Lose..");
      break;
    case PLAYER_NONE:
      tft.println("Undefined");
      break;

    cursorY += 50;
    tft.setCursor(cursorX, cursorY);
    tft.println("Press Reset to Restart"); 
  }
}

void loop() {
  switch (currentScreen) {
    case GAME_SCREEN:
      // Game logic here
      handleGameScreen();
      checkGameOver();
      // DONE: Handle Game Over Logic Here If Player's Avatar HP is <= 0 or Energy <= 0 
      // DONE: If game over, change currentScreen variable to END_SCREEN DONE
      // DONE: Game Over Logic should broadcast a message saying that Player 
      // DONE: Lost, so that other Player know they Won.
      // DONE: Do this in a function called checkGameOver() with functions
      // DONE: like drawGameOver(), and (optionally; not needed in time for presentation
      // DONE: ) sendGameOver() contained inside.
      break;
    case END_SCREEN:
      // DONE: handleWinScreen(); // To be written
      break;
  }
  delay(100);
}
