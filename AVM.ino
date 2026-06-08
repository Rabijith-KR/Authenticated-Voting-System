#include <SPI.h>
#include <Wire.h>
#include <Adafruit_Fingerprint.h>
#include <EEPROM.h>
#include <MFRC522.h>
#include <LiquidCrystal_I2C.h>

#define SS_PIN 53
#define RST_PIN 49

#define FINGER_RX 19
#define FINGER_TX 18

#define VOTE_BUTTON_1 12
#define VOTE_BUTTON_2 9
#define VOTE_BUTTON_3 10
#define RESULT_BUTTON 11

#define BUZZER_PIN 7

LiquidCrystal_I2C lcd(0x27, 16, 2);

MFRC522 mfrc522(SS_PIN, RST_PIN);

Adafruit_Fingerprint finger = Adafruit_Fingerprint(&Serial1);

byte masterCardReg[4] = {0x43, 0xC8, 0x38, 0x22};
byte masterCardVote[4] = {0xC3, 0x69, 0x2D, 0x22};
byte masterCardDelete[4] = {0xD3, 0x1F, 0x6D, 0x1A};

bool registrationMode = false;
bool votingMode = false;

void setup() {

  Serial.begin(9600);

  SPI.begin();

  mfrc522.PCD_Init();

  finger.begin(57600);

  pinMode(VOTE_BUTTON_1, INPUT_PULLUP);
  pinMode(VOTE_BUTTON_2, INPUT_PULLUP);
  pinMode(VOTE_BUTTON_3, INPUT_PULLUP);

  pinMode(RESULT_BUTTON, INPUT_PULLUP);

  pinMode(BUZZER_PIN, OUTPUT);

  lcd.init();
  lcd.backlight();

  lcd.setCursor(0,0);
  lcd.print("Scan Master Card");
}

void loop() {

  if(!registrationMode && !votingMode) {

    if(digitalRead(RESULT_BUTTON) == LOW) {
      showResults();
      return;
    }
  }

  if(!mfrc522.PICC_IsNewCardPresent() ||
     !mfrc522.PICC_ReadCardSerial()) {
    return;
  }

  byte scannedCard[4];

  for(byte i=0;i<4;i++) {
    scannedCard[i] = mfrc522.uid.uidByte[i];
  }

  mfrc522.PICC_HaltA();

  if(registrationMode) {

    if(compareCards(scannedCard, masterCardReg)) {

      registrationMode = false;

      lcd.clear();
      lcd.print("Reg Mode Off");

      successBuzz();

      delay(2000);

      lcd.clear();
      lcd.print("Scan Master Card");

    } else if(!compareCards(scannedCard, masterCardVote) &&
              !compareCards(scannedCard, masterCardDelete)) {

      registerNewVoter(scannedCard);
    }

    return;
  }

  if(votingMode) {

    if(compareCards(scannedCard, masterCardVote)) {

      votingMode = false;

      lcd.clear();
      lcd.print("Voting Ended");

      successBuzz();

      delay(2000);

      lcd.clear();
      lcd.print("Scan Master Card");

    } else if(!compareCards(scannedCard, masterCardReg) &&
              !compareCards(scannedCard, masterCardDelete)) {

      verifyAndVote(scannedCard);
    }

    return;
  }

  if(compareCards(scannedCard, masterCardReg)) {

    registrationMode = true;
    votingMode = false;

    lcd.clear();
    lcd.print("Reg Mode Enabled");

    successBuzz();

    delay(2000);

    lcd.clear();
    lcd.print("Scan New Card");

  } else if(compareCards(scannedCard, masterCardVote)) {

    votingMode = true;
    registrationMode = false;

    lcd.clear();
    lcd.print("Vote Mode Enabled");

    successBuzz();

    delay(1000);

    lcd.clear();
    lcd.print("Scan Your Card");

  } else if(compareCards(scannedCard, masterCardDelete)) {

    lcd.clear();
    lcd.print("Scan Card to Del");

    delay(1500);

    while(true) {

      if(mfrc522.PICC_IsNewCardPresent() &&
         mfrc522.PICC_ReadCardSerial()) {

        byte cardToDelete[4];

        for(byte i=0;i<4;i++) {
          cardToDelete[i] = mfrc522.uid.uidByte[i];
        }

        mfrc522.PICC_HaltA();

        if(deleteCardData(cardToDelete)) {

          lcd.clear();
          lcd.print("Card Deleted");

          successBuzz();

        } else {

          lcd.clear();
          lcd.print("Card Not Found");
        }

        delay(2000);

        lcd.clear();
        lcd.print("Scan Master Card");

        break;
      }
    }
  }
}

bool compareCards(byte *card1, byte card2[]) {

  for(int i=0;i<4;i++) {

    if(card1[i] != card2[i])
      return false;
  }

  return true;
}

void registerNewVoter(byte cardID[]) {

  lcd.clear();
  lcd.print("Reg started");

  delay(2000);

  int id1 = enrollFingerprint();

  if(id1 == -1) {

    lcd.clear();
    lcd.print("Failed");

    errorBuzz();

    delay(3000);

    lcd.clear();
    lcd.print("Scan New Card");

    return;
  }

  lcd.clear();
  lcd.print("Scan 2nd Finger");

  delay(2000);

  int id2 = enrollFingerprint();

  if(id2 == -1) {

    lcd.clear();
    lcd.print("Failed");

    errorBuzz();

    delay(3000);

    lcd.clear();
    lcd.print("Scan New Card");

    return;
  }

  for(int i=0;i<100;i++) {

    int addr = i * 7;

    if(EEPROM.read(addr) == 0xFF) {

      for(int j=0;j<4;j++) {
        EEPROM.write(addr + j, cardID[j]);
      }

      EEPROM.write(addr + 4, id1);
      EEPROM.write(addr + 5, id2);
      EEPROM.write(addr + 6, 0);

      lcd.clear();
      lcd.print("Voter Registered");

      successBuzz();

      delay(3000);

      lcd.clear();
      lcd.print("Scan New Card");

      return;
    }
  }

  lcd.clear();
  lcd.print("Memory Full");

  errorBuzz();

  delay(2000);
}

void verifyAndVote(byte *cardID) {

  int storedID1, storedID2, voteStatusAddr;

  if(!findFingerprintByRFID(cardID,
                            storedID1,
                            storedID2,
                            voteStatusAddr)) {

    lcd.clear();
    lcd.print("Access Denied");

    errorBuzz();

    delay(1500);

    lcd.clear();
    lcd.print("Scan your card");

    return;
  }

  if(EEPROM.read(voteStatusAddr) == 1) {

    lcd.clear();
    lcd.print("Already Voted");

    errorBuzz();

    delay(1500);

    lcd.clear();
    lcd.print("Scan your card");

    return;
  }

  lcd.clear();
  lcd.print("Place Finger");

  for(int i=3;i>0;i--) {

    lcd.setCursor(0,1);
    lcd.print("    ");

    lcd.setCursor(0,1);
    lcd.print(i);

    delay(1000);
  }

  lcd.clear();
  lcd.print("Reading Finger...");

  delay(500);

  if(!verifyFingerprint(storedID1) &&
     !verifyFingerprint(storedID2)) {

    lcd.clear();
    lcd.print("Access Denied");

    errorBuzz();

    delay(1500);

    lcd.clear();
    lcd.print("Scan your card");

    return;
  }

  lcd.clear();
  lcd.print("Cast Your Vote");

  successBuzz();

  delay(2000);

  castVote();

  EEPROM.write(voteStatusAddr, 1);
}

int enrollFingerprint() {

  lcd.clear();
  lcd.print("Place Finger");

  int id = 1;

  while(finger.loadModel(id) == FINGERPRINT_OK) {
    id++;
  }

  while(finger.getImage() != FINGERPRINT_OK);

  if(finger.image2Tz(1) != FINGERPRINT_OK)
    return -1;

  lcd.clear();
  lcd.print("Remove Finger");

  delay(2000);

  while(finger.getImage() == FINGERPRINT_OK);

  lcd.clear();
  lcd.print("Place Again");

  while(finger.getImage() != FINGERPRINT_OK);

  if(finger.image2Tz(2) != FINGERPRINT_OK ||
     finger.createModel() != FINGERPRINT_OK ||
     finger.storeModel(id) != FINGERPRINT_OK) {

    return -1;
  }

  lcd.clear();
  lcd.print("Success!");

  successBuzz();

  delay(2000);

  return id;
}

bool verifyFingerprint(int id) {

  if(finger.getImage() != FINGERPRINT_OK)
    return false;

  if(finger.image2Tz(1) != FINGERPRINT_OK)
    return false;

  if(finger.fingerFastSearch() != FINGERPRINT_OK)
    return false;

  return finger.fingerID == id;
}

bool findFingerprintByRFID(byte *cardID,
                           int &id1,
                           int &id2,
                           int &voteStatusAddr) {

  for(int i=0;i<100;i++) {

    int addr = i * 7;

    bool match = true;

    for(int j=0;j<4;j++) {

      if(EEPROM.read(addr + j) != cardID[j]) {
        match = false;
        break;
      }
    }

    if(match) {

      id1 = EEPROM.read(addr + 4);
      id2 = EEPROM.read(addr + 5);

      voteStatusAddr = addr + 6;

      return true;
    }
  }

  return false;
}

bool deleteCardData(byte *cardID) {

  for(int i=0;i<100;i++) {

    int addr = i * 7;

    bool match = true;

    for(int j=0;j<4;j++) {

      if(EEPROM.read(addr + j) != cardID[j]) {
        match = false;
        break;
      }
    }

    if(match) {

      int id1 = EEPROM.read(addr + 4);
      int id2 = EEPROM.read(addr + 5);

      finger.deleteModel(id1);
      finger.deleteModel(id2);

      for(int k=0;k<7;k++) {
        EEPROM.write(addr + k, 0xFF);
      }

      return true;
    }
  }

  return false;
}

void castVote() {

  while(true) {

    if(digitalRead(VOTE_BUTTON_1) == LOW) {

      EEPROM.write(500, EEPROM.read(500) + 1);

      lcd.clear();
      lcd.print("Voted for 1");

      successBuzz();

      delay(1000);

      break;

    } else if(digitalRead(VOTE_BUTTON_2) == LOW) {

      EEPROM.write(501, EEPROM.read(501) + 1);

      lcd.clear();
      lcd.print("Voted for 2");

      successBuzz();

      delay(1000);

      break;

    } else if(digitalRead(VOTE_BUTTON_3) == LOW) {

      EEPROM.write(502, EEPROM.read(502) + 1);

      lcd.clear();
      lcd.print("Voted for 3");

      successBuzz();

      delay(1000);

      break;
    }
  }
}

void showResults() {

  lcd.clear();
  lcd.print("Results:");

  delay(1000);

  int vote1 = EEPROM.read(500);
  int vote2 = EEPROM.read(501);
  int vote3 = EEPROM.read(502);

  lcd.clear();

  lcd.setCursor(0,0);

  lcd.print("1:");
  lcd.print(vote1);

  lcd.print(" 2:");
  lcd.print(vote2);

  lcd.setCursor(0,1);

  lcd.print("3:");
  lcd.print(vote3);

  delay(5000);

  lcd.clear();
  lcd.print("Scan Master Card");

  EEPROM.write(500, 0);
  EEPROM.write(501, 0);
  EEPROM.write(502, 0);
}

void successBuzz() {

  digitalWrite(BUZZER_PIN, HIGH);

  delay(100);

  digitalWrite(BUZZER_PIN, LOW);
}

void errorBuzz() {

  for(int i=0;i<3;i++) {

    digitalWrite(BUZZER_PIN, HIGH);

    delay(100);

    digitalWrite(BUZZER_PIN, LOW);

    delay(100);
  }
}