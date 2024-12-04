#include <WiFi.h>
#include <esp_now.h>
#include <TFT_eSPI.h>  // Graphics and font library for ST7735 driver chip
#include <SPI.h>
// #include <Adafruit_PN532.h>
#include <Wire.h>

// Define the interface type
// #if 0
// #include <PN532_SPI.h>
// #include "PN532.h"
// PN532_SPI pn532spi(SPI, 10);
// PN532 nfc(pn532spi);

// #elif 0
// #include <PN532_HSU.h>
// #include <PN532.h>
// PN532_HSU pn532hsu(Serial1);
// PN532 nfc(pn532hsu);

// #else
// #include <Wire.h>
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

SemaphoreHandle_t mutex;  // Declare a mutex handle

// NFC reader configuration 
// #define SDA_PIN 21
// #define SCL_PIN 22
// Adafruit_PN532 nfc(SDA_PIN, SCL_PIN);

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

uint8_t zeroArr[7] = {0};

struct AttackMessage {
  uint8_t avatarID;    // ID of the attacked avatar
  uint8_t attackPower; // Power of the incoming attack
};

// Game variables
int currAvatarHealth = avatars[0].health;   // Player's total energy
int energy = 100;                       // Player's energy
int attackPower = 0;                    // Current attack power
// int avatarHealth = 100;                 // Health of the selected avatar
int maxAttackPower = 20;                // Maximum attack power allowed
bool buttonPressed = false;             // Button state
String currentTag = "None";             // Current NFC tag ID
int currAvatarInd = -1;

enum ScreenState {
  GAME_SCREEN,
  END_SCREEN
};

enum WinState {
  PLAYER_NONE,
  PLAYER_A,
  PLAYER_B,
};

ScreenState currentScreen = GAME_SCREEN;
WinState endScreenState = PLAYER_NONE;
volatile bool connected = false;

// Draw & Control Handle Related Functions
// For Screen
void drawGameScreen();
void handleGameScreen();
// void drawControls();
void drawWinScreen();
void handleWinScreen();


// int getAvatarIndex(String tagID) {
//     for (int i = 0; i < 3; i++) {
//         if (avatars[i].tagID == tagID) {
//             return i; // Return index of the active avatar
//         }
//     }
//     return -1; // No matching avatar
// }

void sendAttackRequest() {
  String cmd1 = "D: A";  // Example: Attack command to the other player
  broadcast(cmd1);  // Send the attack command to the other player
  // int potVal = analogRead(POTENTIOMETER_PIN);
  energy -= 1;
  updateDisplay();
}

void sendBoostRequest() {
  return;
}


void receiveCallback(const esp_now_recv_info_t *info, const uint8_t *data, int dataLen) {
  if (xSemaphoreTake(mutex, portMAX_DELAY)) {  // Take the mutex
    char buffer[ESP_NOW_MAX_DATA_LEN + 1];
    int msgLen = min(ESP_NOW_MAX_DATA_LEN, dataLen);
    strncpy(buffer, (const char *)data, msgLen);

    buffer[msgLen] = 0;
    String recvd = String(buffer);
    
    char macStr[18];

    if (recvd[0] == 'D') {  // This is a command to reduce health or modify stats
      // If the received command is related to an attack, apply the attack
      if (recvd.substring(3) == "A") {
        // Assume the other player is attacking, reduce their health
        currAvatarHealth -= 10;  // Decrease health by 10
        if (currAvatarHealth < 0) currAvatarHealth = 0;  // Ensure health doesn't go below 0
        
        // redrawControls();  // Redraw the controls to update health
        tft.fillScreen(TFT_BLACK);
        updateDisplay();
      } else if (recvd.substring(3) == "B") {
        // Handle another attack type
        // attackStrength += 1;  // Example logic for another type of attack
        // redrawControls();  // Redraw to show updated attack strength
        tft.fillScreen(TFT_BLACK);
        updateDisplay();
      }
    }
    // } else if (recvd[0] == 'P') {  // Update progress
    //   recvd.remove(0, 3);
    //   progress = recvd.toInt();
    //   redrawProgress = true;
    // }
    xSemaphoreGive(mutex);
  }
  delay(100);
}

void sentCallback(const uint8_t *macAddr, esp_now_send_status_t status) {
  // Optional: Handle send status
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

void nfcReaderSetup() {
  // Initialize NFC reader
  nfc.begin();
  uint32_t versiondata = nfc.getFirmwareVersion();
  if (!versiondata) {
    Serial.println("Didn't find PN53x board");
    while (1);
  }
  nfc.SAMConfig();
}

void buttonSetup() {
  // Initialize hardware pins
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUTTON_LEFT, INPUT_PULLUP);
  pinMode(BUTTON_RIGHT, INPUT_PULLUP);
}
void setup() {
  Serial.begin(115200);

  textSetup();
  buttonSetup();
  espnowSetup();
  nfcReaderSetup();

}

void readNFCTag() {
  boolean success;
  // Buffer to store the UID
  uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };
  // UID size (4 or 7 bytes depending on card type)
  uint8_t uidLength;

  while (!connected) {
    connected = connect();
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
        currentTag = "Avatar " + String(i);
        currAvatarInd = i;
        Serial.println("Tag is already filled in the tag set");
        tagFilled = true;
        break;
      }
      else if (memcmp(avatars[i].tagID, zeroArr, 7) == 0) {
        // avatars[i].tagID = uid;
        memcpy(avatars[i].tagID, uid, 7);
        currAvatarHealth = avatars[i].health;
        currentTag = "Avatar " + String(i);
        currAvatarInd = i;
        Serial.println("Tag is being filled in the tag set!");
        tagFilled = true;
        break;
      }
    }

    if (!tagFilled) {
      Serial.println("The tag set is fully occupied!");
    }
    
    delay(1000);
    connected = connect();
  }
  else
  {
    // PN532 probably timed out waiting for a card
    Serial.println("Timed out waiting for a card");
  }
}

// Function to execute an attack
void executeAttack() {
    if (energy >= attackPower) {
        // int avatarIndex = getAvatarIndex(currentTag);
        // if (avatarIndex != -1) {
        //     avatars[avatarIndex].health -= attackPower;
        //     if (avatars[avatarIndex].health < 0) {
        //         avatars[avatarIndex].health = 0; // Prevent negative health
        //     }
        //     energy -= attackPower;

        //     Serial.print("Avatar ");
        //     Serial.print(avatars[avatarIndex].tagID);
        //     Serial.print(" hit! Remaining Health: ");
        //     Serial.println(avatars[avatarIndex].health);
        // } else {
        //     Serial.println("No active avatar selected!");
        // }

        sendAttackRequest();
        
        // Feedback
        // digitalWrite(LED_PIN, HIGH);
        // delay(100);
        // digitalWrite(LED_PIN, LOW);
    } else {
        Serial.println("Not enough energy!");
    }
}

// Function to update the display
void updateDisplay() {
    tft.fillScreen(TFT_BLACK); // Clear the screen
    tft.setCursor(10, 10);
    tft.print("Energy: ");
    tft.println(energy);

    tft.setCursor(10, 40);
    tft.print("Attack Power: ");
    tft.println(attackPower);

    for (int i = 0; i < 3; i++) {
        tft.setCursor(10, 70 + (i * 30)); // Offset each avatar
        tft.print("Avatar ");
        tft.print(i + 1);
        tft.print(" Health: ");
        tft.println(avatars[i].health);
    }

    tft.setCursor(10, 160);
    tft.print("Active Tag: ");
    tft.println(currentTag);
}

bool checkGameOver() {
    bool playerLost = true;

    // Check if the player lost (all avatars' health <= 0)
    for (int i = 0; i < 3; i++) {
        if (avatars[i].health > 0) {
            playerLost = false;
            break;
        }
    }

    // If player lost
    if (playerLost) {
        // drawWinScreen("Opponent Wins!");
        // sendGameOver();  // Inform the opponent
        return true;
    }

    return false;
}

// Function to receive an attack from another player
void receiveAttack(int opponentAttackPower) {
  // Subtract the attack power from the player's health
  currAvatarHealth -= opponentAttackPower;
  
  // Make sure health does not go below 0
  if (currAvatarHealth < 0) {
    currAvatarHealth = 0;
  }
  
  // Print updated health for debugging purposes
  Serial.print("Player's Health: ");
  Serial.println(currAvatarHealth);
  
  // Check if player is defeated
  if (currAvatarHealth == 0) {
    // Trigger player defeat logic
    checkGameOver();
    Serial.println("Player is defeated!");
    // Call function to end the game or update display accordingly
  }
}

bool connect() {
  
  nfc.begin();

  // Connected, show version
  uint32_t versiondata = nfc.getFirmwareVersion();
  if (! versiondata)
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
  nfc.setPassiveActivationRetries(0xFF);

  // configure board to read RFID tags
  nfc.SAMConfig();

  Serial.println("Waiting for card (ISO14443A Mifare)...");
  Serial.println("");

  return true;
}

void handleGameScreen() {
  // int potValue = analogRead(POTENTIOMETER_PIN);
  // attackPower = map(potValue, 0, 4095, 1, maxAttackPower);

  // if (digitalRead(BUTTON_PIN) == LOW) {
  //   buttonPressed = true;
  //   executeAttack();
  // }

  readNFCTag();

  // updateDisplay();
}

void loop() {
  /*
  checkGameOver();

  // Read potentiometer value
  int potValue = analogRead(POTENTIOMETER_PIN);
  attackPower = map(potValue, 0, 4095, 1, maxAttackPower);

  // Check if button is pressed
  if (digitalRead(BUTTON_PIN) == LOW) {
    buttonPressed = true;
    executeAttack();
  }

  // Read NFC tag
  readNFCTag();

  // Update display
  updateDisplay();
  */

  // if (xSemaphoreTake(mutex, portMAX_DELAY)) {  // Take the mutex
    switch (currentScreen) {
      case GAME_SCREEN:
        // Game logic here
        handleGameScreen();
        break;
      case END_SCREEN:
        // handleWinScreen(); // To be written
        break;
    }
    // xSemaphoreGive(mutex);
  // }
  delay(100);
}