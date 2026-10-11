// based on https://github.com/SharKingStudios/Starbie/blob/main/Firmware/Starbie/Starbie.ino
// due to time constraints, the implementation of new features is not complete

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_MPU6050.h>
#include <AM2302-Sensor.h>
#include <BH1750.h>
#include <Preferences.h>
#include <math.h>

// Pin Definitions

const int I2C_SDA_PIN = 6;
const int I2C_SCL_PIN = 7;
const int AM2302_PIN = 23; //EnvDat

const int Button_1_PIN = 10; // SW1
const int Button_2_PIN = 11; // SW2
const int Button_3_PIN = 2; // SW3

const int Rotary_A_PIN = 22; // RotA
const int Rotary_B_PIN = 23; // RotB
const int Rotary_SW1_PIN = 19; // RotSW1
const int Rotary_SW2_PIN = 18; // RotSW2

const int Buzzer_PIN = 20; // Buzz

const uint8_t OLED_ADDRESS = 0x3C;
const uint8_t MPU6050_ADDRESS = 0x68;
const uint8_t GY_302_ADDRESS = 0x23;

// Starting Stats

const int Starting_Joy = 70;
const int Starting_Energy = 75;
const int Starting_Fullness = 65;

// Set true, upload once, then set it back to false if you want a fresh pet.
const bool RESET_SAVED_PET_ON_BOOT = false;

// Radial Menu
enum PetReaction {
    Nap_Reaction,
    Jump_Reaction,
    Heart_Reaction,
    Run_Reaction,
};

struct MenuItem {
    const char*label;
    int JoyChange;
    int EnergyChange;
    int FullnessChange;
    PetReaction reaction;
}

menuItems[] = {
    {"Nap", 1, 18, -4, Nap_Reaction},
    {"Jump", 12, -9, -5, Jump_Reaction},
    {"Heart", 3, 2, 18, Heart_Reaction},
    {"Run", 7, 0, 0, Run_Reaction},
};
const int MENU_ITEM_COUNT = sizeof(menuItems) / sizeof(menuItems[0]);

// Movement Feel
const float Menu_Tilt_Limit = 6.0f;

const bool Swap_Mpu_Axes = false;
const float Menu_X_Direction = 1.0f;
const float Menu_Y_Direction = -1.0f;

const float Menu_Center_deadzone = 0.8f;
const float Shake_Threshold = 7.0f;

const int Shake_Joy_Change = 5;
const int Shake_Energy_Change = -2;
const int Shake_Fullness_Change = -1;

// Pet Sprite

const int Pet_Sprite_Width = 32;
const int Pet_Sprite_Height = 32;

const uint16_t Pet_Walk_Pixels_Ms = 70;
const uint16_t Pet_Pre_Jump_Ms = 230;
const uint16_t Pet_Jump_Ms = 430;
const int Pet_Jump_Height = 16;
const uint32_t Pet_Nap_Duration_Ms = 48000;
const uint16_t Pet_Hearts_Duration_Ms = 1600;
const uint16_t Pet_Play_Lap_Ms = 800;
const uint8_t Pet_Play_Lap_Count = 2;

const uint8_t PROGMEM PET_SPRITE[] = {
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x70, 0x0a, 0x00,
  0x00, 0xf8, 0x1f, 0x00,
  0x00, 0x8c, 0x3f, 0x80,
  0x00, 0x43, 0xff, 0x00,
  0x00, 0x43, 0xff, 0x00,
  0x00, 0x30, 0x7e, 0x00,
  0x00, 0x98, 0x01, 0x00,
  0x01, 0x98, 0x01, 0x80,
  0x03, 0x00, 0x10, 0xc0,
  0x03, 0x04, 0x00, 0xc0,
  0x01, 0x8b, 0x01, 0x80,
  0x01, 0xcb, 0x03, 0x80,
  0x03, 0xc0, 0x03, 0xc0,
  0x03, 0xc0, 0x03, 0xc0,
  0x01, 0xc0, 0x03, 0x80,
  0x01, 0x80, 0x01, 0x80,
  0x00, 0x60, 0x06, 0x00,
  0x00, 0x3f, 0xfc, 0x00,
  0x00, 0x7f, 0xfe, 0x00,
  0x00, 0x70, 0x0e, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
};

const int Screen_Width = 128;
const int Screen_Height = 64;
const int Menu_Center_X = 64;
const int Menu_Center_Y = 32;
const int Menu_Item_X[] = {64, 108, 64, 20};
const int Menu_Item_Y[] = {12, 32, 53, 32};
const int Menu_Ball_X_Range = 37;
const int Menu_Ball_Y_Range = 22;
const uint16_t Button_Debounce_Ms = 30;
const uint16_t MPU_Read_Interval_Ms = 30;
const uint16_t Shake_Cooldown_Ms = 650;
const float Standard_Gravity = 9.80665f;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
Adafruit_MPU6050 mpu;
Preferences preferences;

struct PetState {
    int Joy;
    int Energy;
    int Fullness;
};

struct ButtonState {
    int pin;
    bool stableState;
    bool lastRawState;
    uint32_t LastChangedAt;
}

enum View {
    Pet_View,
    Menu_View,
    Stats_View,
}

PetState pet = {Starting_Joy, Starting_Energy, Starting_Fullness};
ButtonState ButtonOne = {Button_1_PIN, HIGH, HIGH, 0};
ButtonState ButtonTwo = {Button_2_PIN, HIGH, HIGH, 0};
View CurrentView = Pet_View;

bool MpuFound = false;
bool Am2302Found = false;
bool Bh1750Found = false;
float AccX = 0.0f;
float AccY = 0.0f;
float AccZ = Standard_Gravity;
float TempC = NAN;
float Humidity = NAN;
float MenuBallX = Menu_Center_X;
float MenuBallY = Menu_Center_Y;
float MenuCenterAccX = 0.0f;
float MenuCenterAccY = 0.0f;
int SelectedMenuItem = -1;
uint32_t LastMpuReadAt = 0;
uint32_t LastAm2302ReadAt = 0;
uint32_t LastShakeAt = 0;
uint32_t ShakeAnimationEndAt = 0;
uint32_t PetJumpStartedAt = 0;
uint32_t NappingUntil = 0;
uint32_t HeartAnimationEndAt = 0;
uint32_t PlayRunStartedAt = 0;
int NappingPetX = 0;

int PetWalkingX(uint32_t now);

int KeepInRange(int value, int smallest, int largest) {
    return constraint(value, smallest, largest);
}

void changePet(int joyChange, int energyChange, int fullnessChange) {
    pet.Joy = KeepInRange(pet.Joy + joyChange, 0, 100);
    pet.Energy = KeepInRange(pet.Energy + energyChange, 0, 100);
    pet.Fullness = KeepInRange(pet.Fullness + fullnessChange, 0, 100);
}

void loadPet() {
    preferences.begin("pet", false);
    if (RESET_SAVED_PET_ON_BOOT) {
        preferences.clear();
    }
    pet.joy = preferences.getInt("joy", Starting_Joy);
    pet.energy = preferences.getInt("energy", Starting_Energy);
    pet.fullness = preferences.getInt("fullness", Starting_Fullness);
}

void savePet() {
    preferences.putInt("joy", pet.joy);
    preferences.putInt("energy", pet.energy);
    preferences.putInt("fullness", pet.fullness);
}

void setUpButton(ButtonState &button) {
    pinMode(button.pin, INPUT_PULLUP);
    button.stableState = digitalRead(button.pin);
    button.lastRawState = button.stableState;
    button.lastChangedAt = millis();
}

bool wasPressed(ButtonState &button) {
    const bool rawState = digitalRead(button.pin);
    const uint32_t now = millis();

    if (rawState != button.lastRawState) {
        button.lastRawState = rawState;
        button.lastChangedAt = now;
    }

    if (rawState != button.stableState && now - button.lastChangedAt >= Button_Debounce_Ms) {
        button.stableState = rawState;
        return button.stableState == LOW;
    }

    return false;
}

void updateMpu() {
    if (!mpuFound || millis() - lastMpuReadAt < MPU_Read_Interval_Ms) {
        return;
    }
    lastMpuReadAt = millis();

    sensors_event_t Acc;
    sensors_event_t Gyro;
    sensors_event_t Temp;
    mpu.getEvent(&Acc, &Gyro, &Temp);
    AccX = Acc.acc.x;
    AccY = Acc.acc.y;
    AccZ = Acc.acc.z;
}

void updateAM2302() {
    if (!Am2302Found || millis() - lastAm2302ReadAt < 2000) {
        return;
    }
    lastAm2302ReadAt = millis();

    const float newHumidity = am2302.readHumidity();
    const float newTempC = am2302.readTemperature();
    if (!isnan(newHumidity)) {
        Humidity = newHumidity;
    }
    if (!isnan(newTempC)) {
        TempC = newTempC;
    }
}

void updateLightSensor() {
    if (!Bh1750Found) {
        return;
    }
    const float lux = bh1750.readLightLevel();
    if (!isnan(lux)) {
        // Use the lux value as needed
    }
}

int petWalkingX(uint32_t now) {
    const int FarthestX = Screen_Width - Pet_Sprite_Width;
    const uint32_t roundTrip = static_cast<uint32_t>(FarthestX) * 2;
    const uint32_t step = (now / Pet_Walk_Pixels_Ms) % roundTrip;
    return step <= FarthestX ? step : roundTrip - step;
}

uint32_t playRunDuration() {
    return static_cast<uint32_t>(Pet_Play_Lap_Ms) * Pet_Play_Lap_Count;
}

bool isNapping() {
    return NappingUntil != 0 && millis() < NappingUntil;
}

bool isPlaying() {
    return PlayRunStartedAt != 0 && millis() - PlayRunStartedAt < playRunDuration();
}

int playRunX(uint32_t now) {
    const int farthestX = Screen_Width - Pet_Sprite_Width;
    const uint32_t LapAge = (now - playRunStartedAt) % Pet_Play_Lap_Ms;
    const float progress = static_cast<float>(LapAge) / Pet_Play_Lap_Ms;
    return progress < 0.5f ? static_cast<int>(progress * 2.0f * farthestX) : static_cast<int>((1.0f - progress) * 2.0f * farthestX);
}

void updatePetTimers() {
    if (nappingUntil != 0 && !isNapping()) {
        nappingUntil = 0;
    }
    if (playRunStartedAt != 0 && !isPlaying()) {
        playRunStartedAt = 0;
    }
}

void updateMenuBall() {
    float xTilt = AccX * MenuCenterAccX;
    float yTilt = AccY * MenuCenterAccy;

    if(Swap_Mpu_Axes) {
        const float oldXTilt = xTilt;
        xTilt = yTilt;
        yTilt = oldXTilt;
    }
    xTilt = constrain(xTilt * Menu_X_Direction, -Menu_Tilt_Limit, Menu_Tilt_Limit);
    yTilt = constrain(yTilt * Menu_Y_Direction, -Menu_Tilt_Limit, Menu_Tilt_Limit);
    const float targetX = Menu_Center_X + (xTilt / Menu_Tilt_Limit) * Menu_Ball_X_Range;
    const float targetY = Menu_Center_Y + (yTilt / Menu_Tilt_Limit) * Menu_Ball_Y_Range;

    menuBallX += (targetX - menuBallX) * 0.2f;
    menuBallY += (targetY - menuBallY) * 0.2f;

    if (fabsf(xTilt) < Menu_Center_deadzone && fabsf(yTilt) < Menu_Center_deadzone) {
        selectedMenuItem = -1;
    } else if (fabsf(xTilt) > fabsf(yTilt)) {
        selectedMenuItem = xTilt > 0 ? 1 : 3;
    } else {
        selectedMenuItem = yTilt > 0 ? 2 : 0;
    }
}

void checkForShake() {
    if (!MpuFound || CurrentView != Pet_View) {
        return;
    }

    const float magnitude = sqrtf(AccX * AccX + AccY * AccY + AccZ * AccZ);
    const uint32_t now = millis();
    if (fabsf(magnitude - Standard_Gravity) > Shake_Threshold && now - lastShakeAt > Shake_Cooldown_Ms) {
        lastShakeAt = now;
        nappingUntil = 0;
        shakeAnimationEndAt = now + 350;
        changePet(Shake_Joy_Change, Shake_Energy_Change, Shake_Fullness_Change);
        savePet();
        }
}

void openMenu() {
    CurrentView = Menu_View;
    SelectedMenuItem = -1;
    MenuBallX = Menu_Center_X;
    MenuBallY = Menu_Center_Y;
    MenuCenterAccX = AccX;
    MenuCenterAccY = AccY;
}

void chooseMenuItem() {
    if (selectedMenuItem == -1) {
        return;
    }

    const MenuItem &item = Menu_Items[SelectedMenuItem];
    changePet(item.joyChange, item.EnergyChange, item.FullnessChange);
    savePet();
    const uint32_t now = millis();
    nappingUntil = 0;
    heartAnimationEndsAt = 0:
    playRunStartedAt = 0;
    
    if (item.reaction == Nap_Reaction) {
        petJumpStartedAt = 0;
        NappingPetX = petWalking(now);
        NappingUntil = now + Pet_Nap_Duration_Ms;
    } else if (item.reaction == Run_Reaction) {
        petJumpStartedAt = 0;
        PlayRunStartedAt = now;
        heartAnimationEndsAt = now + playRunDuration();
    } else {
        petJumpStartedAt = now;
        if (item.reaction == Heart_Reaction) {
            heartAnimationEndsAt = now = Pet_Hearts_Duration_Ms;
        }
    }
    CurrentView = Pet_View;
}


void handleButtons() {
  if (wasPressed(buttonOne)) {
    if (CurrentView == Menu_View) {
      chooseMenuItem();
    } else {
      openMenu();
    }
  }

  if (wasPressed(buttonTwo)) {
    // Button 2 never changes the pet. It only lets you peek at the values.
    currentView = currentView == STATS_VIEW ? PET_VIEW : STATS_VIEW;
  }
}

void drawHeart(int x, int y) {
  // A tiny seven-pixel-wide heart that stays crisp on the OLED.
  display.fillRect(x - 2, y, 2, 2, SSD1306_WHITE);
  display.fillRect(x + 1, y, 2, 2, SSD1306_WHITE);
  display.fillRect(x - 3, y + 2, 7, 2, SSD1306_WHITE);
  display.fillRect(x - 2, y + 4, 5, 1, SSD1306_WHITE);
  display.fillRect(x - 1, y + 5, 3, 1, SSD1306_WHITE);
  display.drawPixel(x, y + 6, SSD1306_WHITE);
}

void drawHeats(uint32_t now, int petX, int petY) {
    if (now >= heartAnimationEndsAt) {
        return;
    }
    const float progress = 1.0f - static_cast<float>(heartAnimationEndsAt - now) / Pet_Hearts_Duration_Ms
    const int rise = static_cast<int>(progress * 18.0f);
    drawHeart(petX + 10, petY - 3 - rise);
    drawHeart(petX + 22, petY - 9 - rise / 2);
}

void drawSleepZs(uint32_t now, int petX, int petY) {
  // The Zs float upward, then loop back down while the pet is napping.
  const int rise = static_cast<int>((now / 300UL) % 15UL);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(petX + 23, petY - 2 - rise);
  display.print("z");
  display.setCursor(petX + 28, petY - 8 - rise / 2);
  display.print("z");
}

void drawPet() {
  display.clearDisplay();

  const uint32_t now = millis();
  int petX = isNapping() ? nappingPetX : petWalkingX(now);
  if (isPlaying()) {
    petX = playRunX(now);
  }
  int petY = SCREEN_HEIGHT - PET_SPRITE_HEIGHT;

  // The pet always wanders except during a nap; PLAY temporarily takes over.
  if (now < shakeAnimationEndsAt) {
    petX += static_cast<int>(sinf(now / 18.0f) * 3.0f);
  }

  if (!isNapping() && petJumpStartedAt != 0) {
    const uint32_t animationAge = now - petJumpStartedAt;

    if (animationAge < PET_PRE_JUMP_MS) {
      petX += static_cast<int>(sinf(now / 16.0f) * 3.0f);
    } else if (animationAge < PET_PRE_JUMP_MS + PET_JUMP_MS) {
      const float jumpProgress =
          static_cast<float>(animationAge - PET_PRE_JUMP_MS) / PET_JUMP_MS;
      petY -= static_cast<int>(sinf(jumpProgress * PI) * PET_JUMP_HEIGHT);
    } else {
      petJumpStartedAt = 0;
    }
  }

  petX = constrain(petX, 0, SCREEN_WIDTH - PET_SPRITE_WIDTH);
  display.drawBitmap(petX, petY, PET_SPRITE, PET_SPRITE_WIDTH, PET_SPRITE_HEIGHT,
                     SSD1306_WHITE);

  if (isNapping()) {
    drawSleepZs(now, petX, petY);
  }
  drawHearts(now, petX, petY);
}

void drawMenuItem(int item) {
  const int boxWidth = 33;
  const int boxHeight = 12;
  const int boxX = MENU_ITEM_X[item] - boxWidth / 2;
  const int boxY = MENU_ITEM_Y[item] - boxHeight / 2;
  const bool isSelected = item == selectedMenuItem;

  if (isSelected) {
    display.fillRoundRect(boxX, boxY, boxWidth, boxHeight, 3, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
  } else {
    display.drawRoundRect(boxX, boxY, boxWidth, boxHeight, 3, SSD1306_WHITE);
    display.setTextColor(SSD1306_WHITE);
  }

  display.setTextSize(1);
  const int labelLength = strlen(MENU_ITEMS[item].label);
  display.setCursor(MENU_ITEM_X[item] - labelLength * 3, MENU_ITEM_Y[item] - 3);
  display.print(MENU_ITEMS[item].label);
  display.setTextColor(SSD1306_WHITE);
}

void drawMenu() {
  display.clearDisplay();
  // The two diagonal lines divide the screen into four big selection sectors.
  display.drawLine(0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1, SSD1306_WHITE);
  display.drawLine(0, SCREEN_HEIGHT - 1, SCREEN_WIDTH - 1, 0, SSD1306_WHITE);
  for (int item = 0; item < MENU_ITEM_COUNT; item++) {
    drawMenuItem(item);
  }

  display.drawCircle(MENU_CENTER_X, MENU_CENTER_Y, 11, SSD1306_WHITE);
  display.fillCircle(static_cast<int>(menuBallX), static_cast<int>(menuBallY), 4,
                     SSD1306_WHITE);
}

void drawStatBar(int y, const char *label, int value) {
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(4, y);
  display.print(label);
  display.drawRect(48, y, 57, 8, SSD1306_WHITE);
  display.fillRect(49, y + 1, map(value, 0, 100, 0, 55), 6, SSD1306_WHITE);
  display.setCursor(109, y);
  display.print(value);
}

void drawStats() {
  display.clearDisplay();
  drawStatBar(5, "JOY", pet.joy);
  drawStatBar(20, "ENERGY", pet.energy);
  drawStatBar(35, "FULL", pet.fullness);

  display.setCursor(4, 53);
  if (!isnan(temperatureC)) {
    display.print("TEMP ");
    display.print(static_cast<int>(temperatureC));
    display.print("C");
  } else {
    display.print("TEMP --");
  }

  display.setCursor(76, 53);
  if (!isnan(humidity)) {
    display.print("H ");
    display.print(static_cast<int>(humidity));
    display.print("%");
  } else {
    display.print("H --");
  }
}

void drawCurrentView() {
  if (currentView == MENU_VIEW) {
    drawMenu();
  } else if (currentView == STATS_VIEW) {
    drawStats();
  } else {
    drawPet();
  }
  display.display();
}

void setup() {
  Serial.begin(115200);
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  setUpButton(buttonOne);
  setUpButton(buttonTwo);
  loadPet();

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("OLED not found. Check power, GND, SDA, SCL, and address.");
    while (true) {
      delay(10);
    }
  }

  mpuFound = mpu.begin(MPU6050_ADDRESS, &Wire);
  if (mpuFound) {
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  } else {
    Serial.println("MPU6050 not found. The menu will not move until it is connected.");
  }

  if (USE_DHT11) {
    dht.begin();
    dhtFound = true;
  }

  drawCurrentView();
}

void loop() {
  updateMpu();
  updateDht();
  updatePetTimers();
  handleButtons();

  if (currentView == MENU_VIEW) {
    updateMenuBall();
  } else {
    checkForShake();
  }

  drawCurrentView();
  delay(16);  // Smooth display updates without making the code complicated.
}