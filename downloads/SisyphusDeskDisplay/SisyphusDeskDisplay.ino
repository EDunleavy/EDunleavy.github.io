#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <math.h>
#include <time.h>

// ESP32 to 1.54-inch ST7789 wiring.
#define TFT_SCLK 18
#define TFT_MOSI 23
#define TFT_RST  17  // Board label may be TX2
#define TFT_DC   16  // Board label may be RX2
#define TFT_CS    5  // Board label may be D5
#define TFT_BLK  25  // Backlight control
#define WIFI_RESET_BUTTON 0

const int SCREEN_WIDTH = 240;
const int SCREEN_HEIGHT = 240;
const int STRIP_HEIGHT = 60;
const unsigned long FRAME_TIME_MS = 100;  // About 10 frames per second
const uint64_t DAY_MS = 24ULL * 60ULL * 60ULL * 1000ULL;
const uint64_t BIRD_BREAK_MS = 2ULL * 60ULL * 1000ULL;
const uint64_t BIRD_FLY_IN_MS = 4000ULL;
const uint64_t BIRD_FLY_OUT_MS = 3000ULL;
const uint64_t MILESTONE_CELEBRATION_MS = 8000ULL;
const float WALKING_SPEED = 19.0f;
const double SIMULATED_WALKING_MPH = 3.2;
const float WALKER_X = 96.0f;
const float WALKER_GROUND_Y = 190.0f;
const int BOULDER_RADIUS = 24;
const unsigned long WEATHER_REFRESH_MS = 15UL * 60UL * 1000UL;
const unsigned long DISTANCE_SAVE_MS = 15UL * 60UL * 1000UL;
const unsigned long MESSAGE_REFRESH_MS = 5UL * 60UL * 1000UL;

// Optional long-distance message source. Create a public text file on GitHub,
// open its Raw view, and paste that raw URL between the quotes. Leave this
// empty to show only the permanent Camus-inspired line.
const char* REMOTE_MESSAGE_URL = "";

// Chicago is used only if automatic public-IP location cannot be reached.
float weatherLatitude = 41.8781f;
float weatherLongitude = -87.6298f;
long timezoneOffsetSeconds = -6L * 60L * 60L;
String detectedCity = "Chicago";

Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_RST);
Preferences preferences;

// A 60-row buffer needs only 28.8 KB. Coordinates passed to it still use
// full-screen Y positions, then this class clips and shifts them per strip.
class StripCanvas16 : public GFXcanvas16 {
public:
  StripCanvas16(int16_t width, int16_t height)
      : GFXcanvas16(width, height), yOffset(0) {}

  void setYOffset(int16_t offset) { yOffset = offset; }

  void clear(uint16_t color) { GFXcanvas16::fillScreen(color); }

  void printGlobal(int16_t x, int16_t y, const String& text) {
    if (y < yOffset || y >= yOffset + STRIP_HEIGHT) return;

    int16_t savedOffset = yOffset;
    yOffset = 0;
    GFXcanvas16::setCursor(x, y - savedOffset);
    GFXcanvas16::print(text);
    yOffset = savedOffset;
  }

  void printGlobal(int16_t x, int16_t y, const char* text) {
    printGlobal(x, y, String(text));
  }

  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    GFXcanvas16::drawPixel(x, y - yOffset, color);
  }

  void drawFastHLine(int16_t x, int16_t y, int16_t width,
                     uint16_t color) override {
    int16_t localY = y - yOffset;
    if (localY < 0 || localY >= STRIP_HEIGHT) return;
    GFXcanvas16::drawFastHLine(x, localY, width, color);
  }

  void drawFastVLine(int16_t x, int16_t y, int16_t height,
                     uint16_t color) override {
    int16_t localY = y - yOffset;

    if (localY < 0) {
      height += localY;
      localY = 0;
    }
    if (localY >= STRIP_HEIGHT || height <= 0) return;
    if (localY + height > STRIP_HEIGHT) {
      height = STRIP_HEIGHT - localY;
    }

    GFXcanvas16::drawFastVLine(x, localY, height, color);
  }

private:
  int16_t yOffset;
};

StripCanvas16 frame(SCREEN_WIDTH, STRIP_HEIGHT);

unsigned long lastFrame = 0;
float walkerDistance = 0.0f;
float terrainOffset = 0.0f;
double totalMilesWalked = 0.0;
bool distanceNeedsSaving = false;
bool distanceStorageReady = false;
unsigned long lastDistanceSave = 0;
uint64_t previousAnimationMs = 0;

enum BoulderTier {
  BOULDER_STONE,
  BOULDER_BRONZE,
  BOULDER_SILVER,
  BOULDER_GOLD
};

BoulderTier currentBoulderTier = BOULDER_STONE;
BoulderTier targetBoulderTier = BOULDER_STONE;
bool milestoneCelebrating = false;
uint64_t milestoneCelebrationStartedMs = 0;

enum WeatherState {
  WEATHER_CLEAR,
  WEATHER_CLOUDY,
  WEATHER_FOG,
  WEATHER_RAIN,
  WEATHER_STORM,
  WEATHER_SNOW
};

// Explicit forward declarations also allow the sketch to be checked with a
// normal C++ compiler instead of relying only on Arduino's prototype pass.
uint64_t clockMs();
void setWeather(WeatherState newState, uint64_t now);

WeatherState weatherState = WEATHER_CLEAR;
uint64_t nextWeatherChangeMs = 0;
uint64_t nextLightningMs = 0;
uint64_t lightningEndsMs = 0;
float weatherIntensity = 0.0f;
float cloudAmount = 0.0f;
float rainAmount = 0.0f;
float snowAmount = 0.0f;
float fogAmount = 0.0f;
float stormAmount = 0.0f;
uint64_t nextShootingStarMs = 0;
uint64_t shootingStarStartedMs = 0;
unsigned long lastWeatherFetch = 0;
unsigned long lastMessageFetch = 0;
unsigned long lastWifiReconnect = 0;
unsigned long lastClockUpdate = 0;
int outdoorTemperature = 0;
int sunriseMinutes = 6 * 60;
int sunsetMinutes = 18 * 60;
bool weatherLoaded = false;
String currentTimeText = "--:--";
String remoteMessage = "";

enum BirdState {
  BIRD_AWAY,
  BIRD_FLYING_IN,
  BIRD_RESTING,
  BIRD_FLYING_OUT
};

BirdState birdState = BIRD_AWAY;
uint64_t birdStateStartedMs = 0;
uint64_t dayStartedMs = 0;
uint64_t birdVisitOffsets[2] = {0, 0};
uint8_t birdVisitsToday = 0;
float birdTakeoffX = 0.0f;
float birdTakeoffY = 0.0f;

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

const uint16_t DAY_SKY_TOP = rgb565(35, 111, 177);
const uint16_t DAY_SKY_BOTTOM = rgb565(143, 199, 224);
const uint16_t TWILIGHT_SKY_TOP = rgb565(17, 35, 68);
const uint16_t TWILIGHT_SKY_BOTTOM = rgb565(238, 132, 83);
const uint16_t NIGHT_SKY_TOP = rgb565(4, 8, 22);
const uint16_t NIGHT_SKY_BOTTOM = rgb565(22, 31, 55);
const uint16_t RAIN_SKY_TOP = rgb565(35, 48, 63);
const uint16_t RAIN_SKY_BOTTOM = rgb565(91, 99, 105);
const uint16_t STORM_SKY_TOP = rgb565(12, 17, 29);
const uint16_t STORM_SKY_BOTTOM = rgb565(49, 53, 62);
const uint16_t SUN = rgb565(255, 205, 102);
const uint16_t MOON_LIGHT = rgb565(232, 235, 218);
const uint16_t MOON_DARK = rgb565(34, 40, 52);
const uint16_t MOON_EDGE = rgb565(139, 151, 157);
const uint16_t CLOUD = rgb565(240, 225, 205);
const uint16_t STORM_CLOUD = rgb565(73, 78, 88);
const uint16_t RAIN_DROP = rgb565(111, 182, 224);
const uint16_t FOG_COLOR = rgb565(177, 187, 188);
const uint16_t LIGHTNING = rgb565(255, 247, 180);
const uint16_t DISTANT_HILL = rgb565(61, 62, 72);
const uint16_t HILL = rgb565(70, 66, 61);
const uint16_t HILL_EDGE = rgb565(151, 133, 105);
const uint16_t ROCK_MID = rgb565(91, 83, 74);
const uint16_t ROCK_DARK = rgb565(45, 43, 42);
const uint16_t ROCK_LIGHT = rgb565(125, 111, 91);
const uint16_t MAN_SKIN = rgb565(224, 166, 111);
const uint16_t MAN_SKIN_SHADOW = rgb565(174, 112, 78);
const uint16_t MAN_HAIR = rgb565(43, 30, 25);
const uint16_t MAN_TUNIC = rgb565(151, 63, 42);
const uint16_t MAN_TUNIC_LIGHT = rgb565(198, 93, 54);
const uint16_t MAN_PANTS = rgb565(57, 66, 67);
const uint16_t MAN_PANTS_SHADOW = rgb565(35, 39, 41);
const uint16_t MAN_BELT = rgb565(61, 38, 27);
const uint16_t MAN_BOOT = rgb565(38, 29, 25);
const uint16_t BOULDER = rgb565(79, 78, 82);
const uint16_t BOULDER_LIGHT = rgb565(139, 133, 126);
const uint16_t BOULDER_DARK = rgb565(48, 47, 52);
const uint16_t BRONZE = rgb565(176, 102, 45);
const uint16_t BRONZE_LIGHT = rgb565(245, 177, 84);
const uint16_t BRONZE_DARK = rgb565(88, 45, 18);
const uint16_t SILVER = rgb565(178, 186, 195);
const uint16_t SILVER_LIGHT = rgb565(240, 245, 248);
const uint16_t SILVER_DARK = rgb565(78, 85, 94);
const uint16_t GOLD = rgb565(205, 151, 34);
const uint16_t GOLD_LIGHT = rgb565(255, 226, 104);
const uint16_t GOLD_DARK = rgb565(102, 69, 9);
const uint16_t DUCK_HEAD = rgb565(12, 91, 61);
const uint16_t DUCK_BODY = rgb565(171, 167, 145);
const uint16_t DUCK_CHEST = rgb565(111, 56, 39);
const uint16_t DUCK_WING = rgb565(122, 105, 82);
const uint16_t DUCK_SPECULUM = rgb565(39, 76, 171);
const uint16_t DUCK_TAIL = rgb565(40, 42, 39);
const uint16_t DUCK_BEAK = rgb565(232, 181, 43);
const uint16_t DUCK_LEG = rgb565(221, 113, 38);
const uint16_t INFO_BAR = rgb565(7, 13, 23);
const uint16_t INFO_DIVIDER = rgb565(71, 100, 124);
const uint16_t SNOW = rgb565(235, 244, 248);
const char* QUOTE_TEXT = "one can imagine sisyphus as happy";

float mountainProfile(float worldX) {
  return (-0.45f * worldX)
         + (12.0f * sinf(worldX * 0.035f))
         + (6.0f * sinf(worldX * 0.081f))
         + (2.5f * sinf(worldX * 0.19f))
         + (1.2f * sinf(worldX * 0.43f));
}

float hillY(float screenX) {
  float centerProfile = mountainProfile(terrainOffset + WALKER_X);
  float pointProfile = mountainProfile(terrainOffset + screenX);
  return WALKER_GROUND_Y + pointProfile - centerProfile;
}

uint16_t blendColor(uint16_t first, uint16_t second, float amount) {
  amount = constrain(amount, 0.0f, 1.0f);
  uint8_t r1 = ((first >> 11) & 0x1F) << 3;
  uint8_t g1 = ((first >> 5) & 0x3F) << 2;
  uint8_t b1 = (first & 0x1F) << 3;
  uint8_t r2 = ((second >> 11) & 0x1F) << 3;
  uint8_t g2 = ((second >> 5) & 0x3F) << 2;
  uint8_t b2 = (second & 0x1F) << 3;

  return rgb565(r1 + ((r2 - r1) * amount),
                g1 + ((g2 - g1) * amount),
                b1 + ((b2 - b1) * amount));
}

BoulderTier boulderTierForMiles(double miles) {
  if (miles >= 100000.0) return BOULDER_GOLD;
  if (miles >= 10000.0) return BOULDER_SILVER;
  if (miles >= 1000.0) return BOULDER_BRONZE;
  return BOULDER_STONE;
}

void getBoulderPalette(BoulderTier tier, uint16_t& base,
                       uint16_t& light, uint16_t& dark) {
  if (tier == BOULDER_GOLD) {
    base = GOLD;
    light = GOLD_LIGHT;
    dark = GOLD_DARK;
  } else if (tier == BOULDER_SILVER) {
    base = SILVER;
    light = SILVER_LIGHT;
    dark = SILVER_DARK;
  } else if (tier == BOULDER_BRONZE) {
    base = BRONZE;
    light = BRONZE_LIGHT;
    dark = BRONZE_DARK;
  } else {
    base = BOULDER;
    light = BOULDER_LIGHT;
    dark = BOULDER_DARK;
  }
}

void startMilestoneCelebration(BoulderTier newTier, uint64_t now) {
  if (newTier <= currentBoulderTier || milestoneCelebrating) return;

  targetBoulderTier = newTier;
  milestoneCelebrating = true;
  milestoneCelebrationStartedMs = now;

  // Save immediately so an earned material cannot be lost during a restart.
  if (distanceStorageReady) {
    preferences.putDouble("miles", totalMilesWalked);
    distanceNeedsSaving = false;
    lastDistanceSave = millis();
  }
}

void startStageCelebration(uint64_t now) {
  if (milestoneCelebrating) return;

  targetBoulderTier = currentBoulderTier;
  milestoneCelebrating = true;
  milestoneCelebrationStartedMs = now;

  if (distanceStorageReady) {
    preferences.putDouble("miles", totalMilesWalked);
    distanceNeedsSaving = false;
    lastDistanceSave = millis();
  }
}

void updateMilestoneCelebration(uint64_t now) {
  if (!milestoneCelebrating) return;
  if (now - milestoneCelebrationStartedMs < MILESTONE_CELEBRATION_MS) return;

  currentBoulderTier = targetBoulderTier;
  milestoneCelebrating = false;
}

void drawStatus(const char* line1, const char* line2 = "") {
  tft.fillScreen(INFO_BAR);
  tft.setTextWrap(false);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);
  tft.setCursor(18, 88);
  tft.print(line1);

  if (strlen(line2) > 0) {
    tft.setTextColor(ST77XX_CYAN);
    tft.setTextSize(1);
    tft.setCursor(18, 118);
    tft.print(line2);
  }
}

String httpGetText(const String& url) {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(8000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  if (!http.begin(client, url)) return "";

  int status = http.GET();
  String payload = status == HTTP_CODE_OK ? http.getString() : "";
  http.end();
  return payload;
}

bool remoteMessageIsConfigured() {
  return strlen(REMOTE_MESSAGE_URL) > 12 &&
         String(REMOTE_MESSAGE_URL).startsWith("https://");
}

bool fetchRemoteMessage() {
  lastMessageFetch = millis();
  if (WiFi.status() != WL_CONNECTED || !remoteMessageIsConfigured()) {
    return false;
  }

  String message = httpGetText(String(REMOTE_MESSAGE_URL));
  message.replace("\r", " ");
  message.replace("\n", " ");
  message.trim();

  while (message.indexOf("  ") >= 0) message.replace("  ", " ");
  if (message.length() == 0) return false;
  if (message.length() > 120) message.remove(120);

  remoteMessage = message;
  Serial.print("Remote message updated: ");
  Serial.println(remoteMessage);
  return true;
}

int parseIsoTimeMinutes(const char* isoTime) {
  if (isoTime == nullptr) return -1;

  const char* timePart = strrchr(isoTime, 'T');
  if (timePart == nullptr || strlen(timePart) < 6) return -1;

  return ((timePart[1] - '0') * 10 + (timePart[2] - '0')) * 60
         + ((timePart[4] - '0') * 10 + (timePart[5] - '0'));
}

WeatherState weatherCodeToState(int code) {
  if (code == 0) return WEATHER_CLEAR;
  if (code >= 1 && code <= 3) {
    return WEATHER_CLOUDY;
  }
  if (code == 45 || code == 48) return WEATHER_FOG;
  if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) {
    return WEATHER_RAIN;
  }
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) {
    return WEATHER_SNOW;
  }
  if (code >= 95 && code <= 99) return WEATHER_STORM;
  return WEATHER_CLOUDY;
}

bool fetchRealWeather() {
  if (WiFi.status() != WL_CONNECTED) return false;

  String url = "https://api.open-meteo.com/v1/forecast?latitude=";
  url += String(weatherLatitude, 4);
  url += "&longitude=";
  url += String(weatherLongitude, 4);
  url += "&current=temperature_2m,weather_code,is_day";
  url += "&daily=sunrise,sunset&temperature_unit=fahrenheit";
  url += "&timezone=auto&forecast_days=1";

  String payload = httpGetText(url);
  lastWeatherFetch = millis();
  if (payload.length() == 0) {
    Serial.println("Weather request failed; keeping the last conditions.");
    return false;
  }

  DynamicJsonDocument doc(6144);
  DeserializationError error = deserializeJson(doc, payload);
  if (error) {
    Serial.print("Weather JSON error: ");
    Serial.println(error.c_str());
    return false;
  }

  float temperature = doc["current"]["temperature_2m"] | 0.0f;
  int weatherCode = doc["current"]["weather_code"] | -1;
  const char* sunrise = doc["daily"]["sunrise"][0] | "";
  const char* sunset = doc["daily"]["sunset"][0] | "";
  int newSunriseMinutes = parseIsoTimeMinutes(sunrise);
  int newSunsetMinutes = parseIsoTimeMinutes(sunset);

  if (weatherCode < 0) return false;

  outdoorTemperature = (int)roundf(temperature);
  if (newSunriseMinutes >= 0) sunriseMinutes = newSunriseMinutes;
  if (newSunsetMinutes >= 0) sunsetMinutes = newSunsetMinutes;
  weatherLoaded = true;
  setWeather(weatherCodeToState(weatherCode), clockMs());

  Serial.print("Weather updated: ");
  Serial.print(outdoorTemperature);
  Serial.print(" F, code ");
  Serial.println(weatherCode);
  return true;
}

bool fetchWifiLocation() {
  if (WiFi.status() != WL_CONNECTED) return false;

  String payload = httpGetText("https://ipwho.is/");
  if (payload.length() == 0) {
    Serial.println("Location request failed; using the fallback location.");
    return false;
  }

  DynamicJsonDocument doc(6144);
  DeserializationError error = deserializeJson(doc, payload);
  if (error || !(doc["success"] | false)) {
    Serial.println("Location response could not be read.");
    return false;
  }

  weatherLatitude = doc["latitude"] | weatherLatitude;
  weatherLongitude = doc["longitude"] | weatherLongitude;
  timezoneOffsetSeconds = doc["timezone"]["offset"] | timezoneOffsetSeconds;
  const char* city = doc["city"] | "Nearby city";
  detectedCity = city;

  Serial.print("Approximate WiFi location: ");
  Serial.print(detectedCity);
  Serial.print(" (");
  Serial.print(weatherLatitude, 4);
  Serial.print(", ");
  Serial.print(weatherLongitude, 4);
  Serial.println(")");
  return true;
}

void updateClockText() {
  struct tm timeInfo;
  if (!getLocalTime(&timeInfo, 20)) {
    currentTimeText = "--:--:--";
    return;
  }

  char buffer[12];
  strftime(buffer, sizeof(buffer), "%I:%M:%S %p", &timeInfo);
  currentTimeText = String(buffer);
  if (currentTimeText.startsWith("0")) currentTimeText.remove(0, 1);
}

int getCurrentMinutes() {
  struct tm timeInfo;
  if (!getLocalTime(&timeInfo, 20)) return 12 * 60;
  return timeInfo.tm_hour * 60 + timeInfo.tm_min;
}

float getCurrentMoonPhase() {
  // NASA lists a new moon at 2000-01-06 18:14 UTC.
  const double referenceNewMoon = 947182440.0;
  const double synodicMonthSeconds = 29.5305888531 * 86400.0;
  time_t now = time(nullptr);

  if (now < 1000000000) return 0.5f;

  double cyclePosition = fmod((double)now - referenceNewMoon,
                              synodicMonthSeconds);
  if (cyclePosition < 0.0) cyclePosition += synodicMonthSeconds;
  return cyclePosition / synodicMonthSeconds;
}

void drawCurrentMoon(int centerX, int centerY, int radius,
                     uint16_t skyColor) {
  float phase = getCurrentMoonPhase();
  float phaseAngle = phase * PI * 2.0f;
  float lightX = sinf(phaseAngle);
  float lightZ = -cosf(phaseAngle);

  frame.fillCircle(centerX, centerY, radius, MOON_DARK);

  for (int y = -radius; y <= radius; y++) {
    for (int x = -radius; x <= radius; x++) {
      float normalizedX = (float)x / radius;
      float normalizedY = (float)y / radius;
      float distanceSquared = normalizedX * normalizedX
                              + normalizedY * normalizedY;
      if (distanceSquared > 1.0f) continue;

      float surfaceZ = sqrtf(1.0f - distanceSquared);
      float illumination = normalizedX * lightX + surfaceZ * lightZ;
      if (illumination > 0.0f) {
        uint16_t shade = blendColor(MOON_EDGE, MOON_LIGHT,
                                    min(1.0f, 0.45f + illumination * 0.7f));
        frame.drawPixel(centerX + x, centerY + y, shade);
      }
    }
  }

  frame.drawCircle(centerX, centerY, radius,
                   blendColor(MOON_EDGE, skyColor, 0.25f));
}

void connectWifi() {
  WiFiManager wifiManager;
  wifiManager.setConfigPortalTimeout(300);

  drawStatus("Hold BOOT now", "Reset WiFi: 2-second window");
  bool resetRequested = false;
  unsigned long resetWindowStarted = millis();
  while (millis() - resetWindowStarted < 2000UL) {
    if (digitalRead(WIFI_RESET_BUTTON) == LOW) resetRequested = true;
    delay(20);
  }

  if (resetRequested) {
    drawStatus("WiFi reset", "Release BOOT button");
    wifiManager.resetSettings();
    delay(1800);
  }

  drawStatus("WiFi setup", "Join: DeskDisplay-Setup");
  bool connected = wifiManager.autoConnect("DeskDisplay-Setup", "12345678");

  if (connected) {
    drawStatus("WiFi connected", "Getting time and weather");
  } else {
    drawStatus("Offline mode", "Animation will still run");
  }
  delay(1200);
}

void setWeather(WeatherState newState, uint64_t now) {
  weatherState = newState;
  if (newState == WEATHER_STORM) {
    nextLightningMs = now + random(1500L, 5000L);
  }
}

void updateWeather(uint64_t now) {
  float targetIntensity = 0.0f;
  float targetCloud = 0.0f;
  float targetRain = 0.0f;
  float targetSnow = 0.0f;
  float targetFog = 0.0f;
  float targetStorm = 0.0f;

  if (weatherState == WEATHER_CLOUDY) targetIntensity = 0.38f;
  if (weatherState == WEATHER_CLOUDY) targetCloud = 0.72f;
  if (weatherState == WEATHER_FOG) {
    targetIntensity = 0.48f;
    targetCloud = 0.30f;
    targetFog = 1.0f;
  }
  if (weatherState == WEATHER_RAIN) {
    targetIntensity = 0.68f;
    targetCloud = 0.88f;
    targetRain = 0.78f;
  }
  if (weatherState == WEATHER_STORM) {
    targetIntensity = 1.0f;
    targetCloud = 1.0f;
    targetRain = 1.0f;
    targetStorm = 1.0f;
  }
  if (weatherState == WEATHER_SNOW) {
    targetIntensity = 0.58f;
    targetCloud = 0.74f;
    targetSnow = 0.86f;
  }

  const float transitionStep = 0.008f;
  auto easeToward = [transitionStep](float current, float target) {
    if (current < target) return min(current + transitionStep, target);
    if (current > target) return max(current - transitionStep, target);
    return current;
  };

  weatherIntensity = easeToward(weatherIntensity, targetIntensity);
  cloudAmount = easeToward(cloudAmount, targetCloud);
  rainAmount = easeToward(rainAmount, targetRain);
  snowAmount = easeToward(snowAmount, targetSnow);
  fogAmount = easeToward(fogAmount, targetFog);
  stormAmount = easeToward(stormAmount, targetStorm);

  if (stormAmount > 0.65f && now >= nextLightningMs) {
    lightningEndsMs = now + random(90L, 180L);
    nextLightningMs = now + random(2500L, 9000L);
  }

  if (nextShootingStarMs == 0) {
    nextShootingStarMs = now + random(120000L, 480000L);
  } else if (now >= nextShootingStarMs) {
    shootingStarStartedMs = now;
    nextShootingStarMs = now + random(120000L, 480000L);
  }
}

void drawThickLine(int x0, int y0, int x1, int y1, uint16_t color, int thickness) {
  float dx = x1 - x0;
  float dy = y1 - y0;
  float length = sqrtf((dx * dx) + (dy * dy));

  if (length < 1.0f) {
    frame.fillCircle(x0, y0, thickness / 2, color);
    return;
  }

  float offsetX = -dy / length;
  float offsetY = dx / length;
  int half = thickness / 2;

  for (int offset = -half; offset <= half; offset++) {
    frame.drawLine(x0 + (offsetX * offset),
                   y0 + (offsetY * offset),
                   x1 + (offsetX * offset),
                   y1 + (offsetY * offset),
                   color);
  }
}

void drawBackground(float elapsedSeconds, uint64_t now) {
  int currentMinutes = getCurrentMinutes();
  uint16_t timeSkyTop = DAY_SKY_TOP;
  uint16_t timeSkyBottom = DAY_SKY_BOTTOM;
  const int twilightMinutes = 45;

  if (currentMinutes < sunriseMinutes - twilightMinutes ||
      currentMinutes > sunsetMinutes + twilightMinutes) {
    timeSkyTop = NIGHT_SKY_TOP;
    timeSkyBottom = NIGHT_SKY_BOTTOM;
  } else if (currentMinutes < sunriseMinutes) {
    float amount = (float)(currentMinutes - (sunriseMinutes - twilightMinutes)) /
                   twilightMinutes;
    timeSkyTop = blendColor(NIGHT_SKY_TOP, TWILIGHT_SKY_TOP, amount);
    timeSkyBottom = blendColor(NIGHT_SKY_BOTTOM, TWILIGHT_SKY_BOTTOM, amount);
  } else if (currentMinutes < sunriseMinutes + twilightMinutes) {
    float amount = (float)(currentMinutes - sunriseMinutes) / twilightMinutes;
    timeSkyTop = blendColor(TWILIGHT_SKY_TOP, DAY_SKY_TOP, amount);
    timeSkyBottom = blendColor(TWILIGHT_SKY_BOTTOM, DAY_SKY_BOTTOM, amount);
  } else if (currentMinutes > sunsetMinutes) {
    float amount = (float)(currentMinutes - sunsetMinutes) / twilightMinutes;
    timeSkyTop = blendColor(TWILIGHT_SKY_TOP, NIGHT_SKY_TOP, amount);
    timeSkyBottom = blendColor(TWILIGHT_SKY_BOTTOM, NIGHT_SKY_BOTTOM, amount);
  } else if (currentMinutes > sunsetMinutes - twilightMinutes) {
    float amount = (float)(currentMinutes - (sunsetMinutes - twilightMinutes)) /
                   twilightMinutes;
    timeSkyTop = blendColor(DAY_SKY_TOP, TWILIGHT_SKY_TOP, amount);
    timeSkyBottom = blendColor(DAY_SKY_BOTTOM, TWILIGHT_SKY_BOTTOM, amount);
  }

  uint16_t weatherTop = blendColor(RAIN_SKY_TOP, STORM_SKY_TOP, stormAmount);
  uint16_t weatherBottom = blendColor(RAIN_SKY_BOTTOM, STORM_SKY_BOTTOM,
                                      stormAmount);
  uint16_t activeSkyTop = blendColor(timeSkyTop, weatherTop, weatherIntensity);
  uint16_t activeSkyBottom = blendColor(timeSkyBottom, weatherBottom,
                                        weatherIntensity);
  bool lightningActive = now < lightningEndsMs;

  if (lightningActive) {
    activeSkyTop = blendColor(activeSkyTop, ST77XX_WHITE, 0.62f);
    activeSkyBottom = blendColor(activeSkyBottom, ST77XX_WHITE, 0.48f);
  }

  for (int y = 0; y < SCREEN_HEIGHT; y++) {
    float amount = (float)y / SCREEN_HEIGHT;
    frame.drawFastHLine(0, y, SCREEN_WIDTH,
                        blendColor(activeSkyTop, activeSkyBottom, amount));
  }

  bool daytime = currentMinutes >= sunriseMinutes &&
                 currentMinutes <= sunsetMinutes;
  if (daytime && weatherIntensity < 0.78f) {
    float daylightProgress = (float)(currentMinutes - sunriseMinutes) /
                             max(1, sunsetMinutes - sunriseMinutes);
    int sunX = 22 + (int)(daylightProgress * 196.0f);
    int sunY = 77 - (int)(sinf(daylightProgress * PI) * 43.0f);
    uint16_t dimmedSun = blendColor(SUN, STORM_CLOUD,
                                    weatherIntensity * 0.75f);
    frame.fillCircle(sunX, sunY, 14, dimmedSun);
    frame.fillCircle(sunX - 4, sunY - 4, 4,
                     blendColor(rgb565(255, 224, 143), STORM_CLOUD,
                                weatherIntensity * 0.75f));
  } else if (!daytime && weatherIntensity < 0.82f) {
    drawCurrentMoon(196, 52, 11, activeSkyTop);
    for (int star = 0; star < 10; star++) {
      int starX = (star * 53 + 19) % SCREEN_WIDTH;
      int starY = 32 + ((star * 29) % 63);
      float twinkle = 0.35f + 0.65f *
                      fabsf(sinf(elapsedSeconds * (1.2f + star * 0.11f)
                                 + star * 1.7f));
      uint16_t starColor = blendColor(activeSkyTop, ST77XX_WHITE,
                                      twinkle * (1.0f - weatherIntensity));
      frame.drawPixel(starX, starY, starColor);
      if (twinkle > 0.88f && star % 3 == 0) {
        frame.drawPixel(starX + 1, starY, starColor);
        frame.drawPixel(starX, starY + 1, starColor);
      }
    }

    if (shootingStarStartedMs > 0 &&
        now - shootingStarStartedMs < 1200ULL) {
      float progress = (float)(now - shootingStarStartedMs) / 1200.0f;
      int starX = 30 + (int)(progress * 105.0f);
      int starY = 45 + (int)(progress * 48.0f);
      drawThickLine(starX - 18, starY - 8, starX, starY,
                    rgb565(205, 220, 226), 2);
      frame.fillCircle(starX, starY, 2, ST77XX_WHITE);
    }
  }

  if (cloudAmount > 0.03f) {
    int cloudX = ((int)(elapsedSeconds * 5.0f) % 320) - 65;
    uint16_t cloudColor = blendColor(CLOUD, STORM_CLOUD, weatherIntensity);
    float cloudScale = 0.35f + cloudAmount * 0.65f;
    frame.fillCircle(cloudX, 57, max(2, (int)(10 * cloudScale)), cloudColor);
    frame.fillCircle(cloudX + 13, 51, max(3, (int)(14 * cloudScale)), cloudColor);
    frame.fillCircle(cloudX + 29, 58, max(2, (int)(11 * cloudScale)), cloudColor);
    frame.fillRoundRect(cloudX - 5, 57,
                        max(10, (int)(46 * cloudScale)),
                        max(4, (int)(11 * cloudScale)), 4, cloudColor);

    if (cloudAmount > 0.42f) {
      int secondCloudX = (cloudX + 125) % 320;
      frame.fillCircle(secondCloudX, 41, (int)(13 * cloudScale), cloudColor);
      frame.fillCircle(secondCloudX + 17, 37, (int)(17 * cloudScale), cloudColor);
      frame.fillCircle(secondCloudX + 36, 44, (int)(12 * cloudScale), cloudColor);
      frame.fillRoundRect(secondCloudX - 5, 43,
                          (int)(54 * cloudScale),
                          max(4, (int)(12 * cloudScale)), 5, cloudColor);
    }
  }

  if (lightningActive) {
    drawThickLine(176, 18, 165, 48, LIGHTNING, 3);
    drawThickLine(165, 48, 177, 48, LIGHTNING, 3);
    drawThickLine(177, 48, 157, 88, LIGHTNING, 3);
  }

  // Distant peaks move slowly to create depth behind the foreground slope.
  int distantShift = ((int)(terrainOffset * 0.18f)) % 180;
  for (int peakX = -180 - distantShift; peakX < SCREEN_WIDTH + 180;
       peakX += 180) {
    // End the mountain below the visible screen so no flat baseline appears.
    frame.fillTriangle(peakX, SCREEN_HEIGHT + 8, peakX + 82, 86,
                       peakX + 180, SCREEN_HEIGHT + 8, DISTANT_HILL);
  }

  // Build an irregular scrolling mountain one vertical slice at a time.
  for (int x = 0; x < SCREEN_WIDTH; x++) {
    int y = constrain((int)hillY(x), 0, SCREEN_HEIGHT);
    frame.drawFastVLine(x, y, SCREEN_HEIGHT - y, HILL);
  }

  // Layered seams make the foreground read as exposed mountain rock.
  for (int layer = 0; layer < 3; layer++) {
    int previousX = 0;
    int previousY = (int)hillY(0) + 20 + (layer * 27)
                    + (int)(4.0f * sinf((terrainOffset * 0.035f) + layer));

    for (int x = 3; x < SCREEN_WIDTH; x += 3) {
      float worldX = terrainOffset + x;
      int y = (int)hillY(x) + 20 + (layer * 27)
              + (int)(4.0f * sinf((worldX * 0.035f) + layer));
      frame.drawLine(previousX, previousY, x, y,
                     layer == 1 ? ROCK_LIGHT : ROCK_MID);
      previousX = x;
      previousY = y;
    }
  }

  for (int x = 0; x < SCREEN_WIDTH; x++) {
    int y = constrain((int)hillY(x), 1, SCREEN_HEIGHT - 2);
    frame.drawFastVLine(x, y - 2, 4, HILL_EDGE);
    int snowPattern = ((x + (int)terrainOffset) * 37) % 100;
    if (snowAmount > 0.05f && snowPattern < (int)(snowAmount * 100.0f)) {
      frame.drawFastVLine(x, y - 3, snowAmount > 0.55f ? 3 : 2, SNOW);
    }
  }

  // Angular stones and cracks scroll as part of the mountain surface.
  const int rockSpacing = 43;
  int rockShift = ((int)terrainOffset) % rockSpacing;
  for (int x = -rockSpacing - rockShift;
       x < SCREEN_WIDTH + rockSpacing; x += rockSpacing) {
    int worldPatch = ((int)terrainOffset + x) / rockSpacing;
    int surfaceY = (int)hillY(x);
    int crackY = surfaceY + 22 + (abs(worldPatch) % 4) * 13;

    frame.drawLine(x, crackY, x + 5, crackY + 7, ROCK_DARK);
    frame.drawLine(x + 5, crackY + 7, x + 2, crackY + 14, ROCK_DARK);
    frame.drawLine(x + 5, crackY + 7, x + 11, crackY + 10, ROCK_DARK);

    int stoneX = x + 18;
    int stoneY = (int)hillY(stoneX) - 1;
    frame.fillTriangle(stoneX - 5, stoneY,
                       stoneX + 1, stoneY - 6,
                       stoneX + 7, stoneY,
                       ROCK_LIGHT);
    frame.drawLine(stoneX - 5, stoneY,
                   stoneX + 7, stoneY, ROCK_DARK);
  }
}

void drawWeatherOverlay(uint64_t now) {
  if (snowAmount > 0.02f) {
    uint32_t snowPhase = now / 70ULL;
    int flakeCount = (int)(44.0f * snowAmount);
    for (int flake = 0; flake < flakeCount; flake++) {
      int y = ((flake * 53) + (snowPhase * (1 + flake % 2))) % 250 - 5;
      int drift = (int)(7.0f * sinf((snowPhase + flake * 19) * 0.045f));
      int x = ((flake * 71) % 250) - 5 + drift;
      int radius = flake % 5 == 0 ? 2 : 1;
      frame.fillCircle(x, y, radius, SNOW);
    }
  }

  if (rainAmount > 0.02f) {
    int dropCount = (int)(54.0f * rainAmount);
    uint32_t rainPhase = now / 35ULL;

    for (int drop = 0; drop < dropCount; drop++) {
      int x = ((drop * 47) + (rainPhase * 2)) % 280 - 20;
      int y = ((drop * 67) + (rainPhase * 8)) % 270 - 15;
      int length = 5 + (int)(stormAmount * 4.0f);
      frame.drawLine(x, y, x - 3, y + length, RAIN_DROP);
    }

    if (rainAmount > 0.52f) {
      int splashCount = 2 + (int)(rainAmount * 7.0f);
      for (int splash = 0; splash < splashCount; splash++) {
        int x = ((splash * 73) + (rainPhase * 3)) % 230 + 5;
        int y = (int)hillY(x) - 1;
        frame.drawLine(x, y, x - 3, y - 2, RAIN_DROP);
        frame.drawLine(x, y, x + 4, y - 2, RAIN_DROP);
      }
    }
  }

  if (fogAmount > 0.04f) {
    int fogBands = 1 + (int)(fogAmount * 7.0f);
    int fogShift = (int)(now / 90ULL) % 100;
    uint16_t fogShade = blendColor(RAIN_SKY_BOTTOM, FOG_COLOR,
                                   0.55f + fogAmount * 0.35f);
    for (int band = 0; band < fogBands; band++) {
      int y = 55 + band * 22;
      int x = ((band * 67 + fogShift * (band % 2 ? 1 : -1)) % 150) - 45;
      frame.drawFastHLine(x, y, 150, fogShade);
      frame.drawFastHLine(x + 38, y + 2, 125, fogShade);
    }
  }
}

void drawInfoBar() {
  frame.fillRect(0, 0, SCREEN_WIDTH, 36, INFO_BAR);
  frame.drawFastHLine(0, 35, SCREEN_WIDTH, INFO_DIVIDER);
  frame.setTextWrap(false);
  frame.setTextSize(2);
  frame.setTextColor(ST77XX_WHITE);
  frame.printGlobal(4, 10, currentTimeText);

  String temperatureText = weatherLoaded
                               ? String(outdoorTemperature) + "F"
                               : "--F";
  frame.setTextSize(3);
  int temperatureX = SCREEN_WIDTH - 4 - temperatureText.length() * 18;
  frame.setTextColor(weatherState == WEATHER_SNOW
                         ? ST77XX_CYAN : ST77XX_YELLOW);
  frame.printGlobal(temperatureX, 6, temperatureText);
}

void drawQuoteBar(uint64_t now) {
  frame.fillRect(0, 220, SCREEN_WIDTH, 20, INFO_BAR);
  frame.drawFastHLine(0, 220, SCREEN_WIDTH, INFO_DIVIDER);
  frame.setTextWrap(false);
  frame.setTextSize(1);
  frame.setTextColor(ST77XX_WHITE);

  // Alternate every 15 seconds. Long remote messages scroll smoothly while
  // the permanent line remains centered during its half of the cycle.
  bool showRemote = remoteMessage.length() > 0 &&
                    ((now / 15000ULL) % 2ULL == 1ULL);
  if (!showRemote) {
    int quoteX = (SCREEN_WIDTH - strlen(QUOTE_TEXT) * 6) / 2;
    frame.printGlobal(quoteX, 227, QUOTE_TEXT);
    return;
  }

  int textWidth = remoteMessage.length() * 6;
  int messageX = (SCREEN_WIDTH - textWidth) / 2;
  if (textWidth > SCREEN_WIDTH - 8) {
    int travel = textWidth + SCREEN_WIDTH;
    messageX = SCREEN_WIDTH - ((now / 55ULL) % travel);
  }
  frame.setTextColor(ST77XX_YELLOW);
  frame.printGlobal(messageX, 227, remoteMessage);
}

void drawDistanceGauge() {
  const int gaugeX = 40;
  const int gaugeY = 199;
  const int gaugeWidth = 160;

  frame.fillRoundRect(gaugeX, gaugeY, gaugeWidth, 20, 5, INFO_BAR);
  frame.drawRoundRect(gaugeX, gaugeY, gaugeWidth, 20, 5, INFO_DIVIDER);

  String mileageText = "MILES " + String(totalMilesWalked, 2);
  int mileageX = (SCREEN_WIDTH - mileageText.length() * 6) / 2;
  frame.setTextWrap(false);
  frame.setTextSize(1);
  frame.setTextColor(ST77XX_WHITE);
  frame.printGlobal(mileageX, 205, mileageText);
}

void drawBoulder(float centerX, float centerY, uint64_t now) {
  int x = (int)centerX;
  int y = (int)centerY;
  const int8_t roughX[8] = {-22, -14, 3, 20, 24, 14, -3, -21};
  const int8_t roughY[8] = {-8, -21, -24, -14, 3, 20, 23, 13};
  const int8_t smoothX[8] = {-22, -15, 1, 18, 23, 16, 0, -19};
  const int8_t smoothY[8] = {-7, -19, -23, -17, 0, 17, 23, 16};
  const int8_t* rockX = totalMilesWalked >= 100.0 ? smoothX : roughX;
  const int8_t* rockY = totalMilesWalked >= 100.0 ? smoothY : roughY;

  uint16_t rockColor;
  uint16_t rockLight;
  uint16_t rockDark;
  getBoulderPalette(currentBoulderTier, rockColor, rockLight, rockDark);

  if (milestoneCelebrating) {
    uint16_t targetColor;
    uint16_t targetLight;
    uint16_t targetDark;
    getBoulderPalette(targetBoulderTier, targetColor, targetLight, targetDark);
    float progress = constrain(
        (float)(now - milestoneCelebrationStartedMs) /
        MILESTONE_CELEBRATION_MS, 0.0f, 1.0f);
    float eased = progress * progress * (3.0f - 2.0f * progress);
    rockColor = blendColor(rockColor, targetColor, eased);
    rockLight = blendColor(rockLight, targetLight, eased);
    rockDark = blendColor(rockDark, targetDark, eased);

    float flash = max(0.0f, sinf(progress * PI * 8.0f)) *
                  (1.0f - progress * 0.65f);
    rockColor = blendColor(rockColor, ST77XX_WHITE, flash * 0.38f);
    rockLight = blendColor(rockLight, ST77XX_WHITE, flash * 0.70f);
  }

  bool metallic = currentBoulderTier != BOULDER_STONE ||
                  (milestoneCelebrating &&
                   targetBoulderTier != BOULDER_STONE);
  bool polishedBronze = totalMilesWalked >= 5000.0 &&
                        totalMilesWalked < 10000.0;
  bool polishedSilver = totalMilesWalked >= 25000.0 &&
                        totalMilesWalked < 100000.0;
  bool glowingSilver = totalMilesWalked >= 50000.0 &&
                       totalMilesWalked < 100000.0;
  bool goldVeinedSilver = totalMilesWalked >= 75000.0 &&
                          totalMilesWalked < 100000.0;

  if (polishedBronze) {
    rockColor = blendColor(rockColor, BRONZE_LIGHT, 0.18f);
  }
  if (polishedSilver) {
    rockColor = blendColor(rockColor, SILVER_LIGHT, 0.12f);
  }

  // Offset polygon first creates a natural shadow under the uneven rock.
  for (int point = 0; point < 8; point++) {
    int next = (point + 1) % 8;
    frame.fillTriangle(x + 2, y + 3,
                       x + rockX[point] + 2, y + rockY[point] + 3,
                       x + rockX[next] + 2, y + rockY[next] + 3,
                       rockDark);
  }

  for (int point = 0; point < 8; point++) {
    int next = (point + 1) % 8;
    frame.fillTriangle(x, y,
                       x + rockX[point], y + rockY[point],
                       x + rockX[next], y + rockY[next],
                       rockColor);
    frame.drawLine(x + rockX[point], y + rockY[point],
                   x + rockX[next], y + rockY[next],
                   rockLight);
  }

  frame.fillTriangle(x - 14, y - 10,
                     x - 8, y - 18,
                     x - 2, y - 14,
                     rockLight);

  if (currentBoulderTier == BOULDER_STONE &&
      totalMilesWalked >= 10.0) {
    frame.drawLine(x - 12, y + 4, x - 7, y + 7, rockDark);
    frame.drawLine(x + 6, y - 10, x + 11, y - 7, rockDark);
  }

  if (currentBoulderTier == BOULDER_STONE &&
      totalMilesWalked >= 500.0) {
    frame.drawLine(x - 7, y + 11, x - 2, y + 4, BRONZE_LIGHT);
    frame.drawLine(x - 2, y + 4, x + 1, y - 5, BRONZE_LIGHT);
    frame.drawLine(x - 2, y + 4, x + 5, y + 7, BRONZE_LIGHT);
  }

  if (metallic) {
    float shimmer = 0.5f + 0.5f * sinf(millis() / 330.0f);
    uint16_t glint = blendColor(rockLight, ST77XX_WHITE, shimmer);
    frame.drawFastHLine(x - 12, y - 14, 7, glint);
    frame.drawFastVLine(x - 9, y - 17, 7, glint);
    if (shimmer > 0.70f) frame.drawPixel(x - 4, y - 9, glint);
    if (polishedBronze && shimmer > 0.45f) {
      frame.drawFastHLine(x + 5, y + 8, 5, glint);
      frame.drawFastVLine(x + 7, y + 6, 5, glint);
    }
    if (polishedSilver && shimmer > 0.38f) {
      frame.drawFastHLine(x + 5, y + 8, 6, glint);
      frame.drawFastVLine(x + 8, y + 5, 7, glint);
    }
  }

  if (glowingSilver) {
    float glowPulse = 0.35f + 0.30f * sinf(millis() / 500.0f);
    uint16_t glow = blendColor(SILVER_DARK, SILVER_LIGHT, glowPulse);
    frame.drawCircle(x, y, BOULDER_RADIUS + 3, glow);
    frame.drawPixel(x - 27, y - 5, SILVER_LIGHT);
    frame.drawPixel(x + 26, y + 7, SILVER_LIGHT);
  }

  if (goldVeinedSilver) {
    frame.drawLine(x - 9, y + 13, x - 4, y + 6, GOLD_LIGHT);
    frame.drawLine(x - 4, y + 6, x + 1, y - 4, GOLD_LIGHT);
    frame.drawLine(x - 4, y + 6, x + 5, y + 10, GOLD_LIGHT);
    frame.drawPixel(x + 2, y - 5, ST77XX_YELLOW);
  }

  if (milestoneCelebrating) {
    float progress = (float)(now - milestoneCelebrationStartedMs) /
                     MILESTONE_CELEBRATION_MS;
    for (int sparkle = 0; sparkle < 10; sparkle++) {
      float angle = progress * PI * 6.0f + sparkle * PI * 0.2f;
      float radius = 30.0f + (sparkle % 3) * 5.0f;
      int sparkleX = x + (int)(cosf(angle) * radius);
      int sparkleY = y + (int)(sinf(angle) * radius * 0.72f);
      uint16_t sparkleColor;
      if (totalMilesWalked >= 75000.0) {
        sparkleColor = sparkle % 2 == 0 ? GOLD_LIGHT : ST77XX_YELLOW;
      } else {
        sparkleColor = sparkle % 2 == 0 ? ST77XX_WHITE : rockLight;
      }
      frame.fillCircle(sparkleX, sparkleY, sparkle % 3 == 0 ? 2 : 1,
                       sparkleColor);
    }
  }

  if (snowAmount > 0.08f) {
    frame.drawLine(x - 14, y - 21, x + 3, y - 24, SNOW);
    frame.drawLine(x + 3, y - 24, x + 20, y - 14, SNOW);
    if (snowAmount > 0.55f) {
      frame.drawLine(x - 13, y - 20, x + 3, y - 23, SNOW);
    }
  }
}

uint64_t clockMs() {
  return (uint64_t)(esp_timer_get_time() / 1000ULL);
}

void scheduleNewBirdDay(uint64_t now) {
  dayStartedMs = now;
  birdVisitsToday = 0;

  // One visit is placed randomly in each half of the 24-hour period.
  // This guarantees that the bird appears no more than twice per day.
  birdVisitOffsets[0] = random(30L * 60L * 1000L,
                               12L * 60L * 60L * 1000L);
  birdVisitOffsets[1] = random(12L * 60L * 60L * 1000L,
                               23L * 60L * 60L * 1000L);

  Serial.print("Bird visit 1 scheduled in ");
  Serial.print(birdVisitOffsets[0] / 3600000.0f, 2);
  Serial.println(" hours");
  Serial.print("Bird visit 2 scheduled in ");
  Serial.print(birdVisitOffsets[1] / 3600000.0f, 2);
  Serial.println(" hours");
}

void startBirdVisit(uint64_t now) {
  if (birdState != BIRD_AWAY || birdVisitsToday >= 2) {
    return;
  }

  birdVisitsToday++;
  birdState = BIRD_FLYING_IN;
  birdStateStartedMs = now;
}

void updateBirdState(uint64_t now, float walkerX, float boulderX, float boulderY) {
  if (now - dayStartedMs >= DAY_MS) {
    scheduleNewBirdDay(now);
  }

  if (birdState == BIRD_AWAY && birdVisitsToday < 2) {
    uint64_t timeIntoDay = now - dayStartedMs;

    // Wait for a safe part of the hill so the landing is visible onscreen.
    if (timeIntoDay >= birdVisitOffsets[birdVisitsToday] &&
        walkerX > -10.0f && walkerX < 120.0f) {
      startBirdVisit(now);
    }
  }

  if (birdState == BIRD_FLYING_IN &&
      now - birdStateStartedMs >= BIRD_FLY_IN_MS) {
    birdState = BIRD_RESTING;
    birdStateStartedMs = now;
  } else if (birdState == BIRD_RESTING &&
             now - birdStateStartedMs >= BIRD_BREAK_MS) {
    birdTakeoffX = boulderX;
    birdTakeoffY = boulderY - BOULDER_RADIUS - 4.0f;
    birdState = BIRD_FLYING_OUT;
    birdStateStartedMs = now;
  } else if (birdState == BIRD_FLYING_OUT &&
             now - birdStateStartedMs >= BIRD_FLY_OUT_MS) {
    birdState = BIRD_AWAY;
    birdStateStartedMs = now;
  }
}

void drawBird(float x, float y, bool flying, float wingPhase,
              bool shaking = false) {
  int birdX = (int)x;
  int birdY = (int)y;
  int wingLift = (flying || shaking)
                     ? (int)(sinf(wingPhase) * 7.0f) : 2;

  // Male mallard: green head, white collar, brown chest, and blue wing patch.
  frame.fillCircle(birdX, birdY, 8, DUCK_BODY);
  frame.fillCircle(birdX - 5, birdY - 2, 5, DUCK_CHEST);
  frame.drawFastVLine(birdX - 6, birdY - 7, 8, ST77XX_WHITE);
  frame.fillCircle(birdX - 9, birdY - 6, 6, DUCK_HEAD);
  frame.fillCircle(birdX - 10, birdY - 7, 1, ST77XX_YELLOW);
  frame.drawPixel(birdX - 10, birdY - 7, ST77XX_BLACK);

  frame.fillTriangle(birdX - 14, birdY - 6,
                     birdX - 23, birdY - 3,
                     birdX - 14, birdY - 1,
                     DUCK_BEAK);
  frame.fillTriangle(birdX + 6, birdY - 2,
                     birdX + 17, birdY - 5,
                     birdX + 13, birdY + 4,
                     DUCK_TAIL);

  if (flying || shaking) {
    int wingY = birdY - 8 - wingLift;
    frame.fillTriangle(birdX - 1, birdY,
                       birdX + 5, wingY,
                       birdX + 14, wingY - 2,
                       DUCK_WING);
    frame.fillTriangle(birdX + 10, wingY - 1,
                       birdX + 16, wingY - 3,
                       birdX + 13, wingY + 3,
                       DUCK_TAIL);
    frame.drawLine(birdX + 4, wingY,
                   birdX + 11, wingY - 1, DUCK_SPECULUM);
  } else {
    frame.fillTriangle(birdX - 1, birdY - 4,
                       birdX + 8, birdY - 1,
                       birdX + 4, birdY + 5,
                       DUCK_WING);
    frame.drawLine(birdX + 4, birdY + 3,
                   birdX + 8, birdY + 1, DUCK_SPECULUM);
  }

  if (!flying) {
    frame.drawLine(birdX - 3, birdY + 7, birdX - 4, birdY + 11, DUCK_LEG);
    frame.drawLine(birdX + 2, birdY + 7, birdX + 3, birdY + 11, DUCK_LEG);
  }

  if (shaking) {
    int sprayPhase = (int)(wingPhase * 3.0f);
    frame.drawPixel(birdX - 15 - (sprayPhase % 4), birdY - 2, RAIN_DROP);
    frame.drawPixel(birdX + 12 + (sprayPhase % 5), birdY - 5, RAIN_DROP);
    frame.drawPixel(birdX + 9, birdY + 7 + (sprayPhase % 3), RAIN_DROP);
  }
}

void drawBirdForState(uint64_t now, float boulderX, float boulderY) {
  float birdX;
  float birdY;
  float wingPhase = now / 90.0f;

  if (birdState == BIRD_FLYING_IN) {
    float progress = (float)(now - birdStateStartedMs) / BIRD_FLY_IN_MS;
    progress = constrain(progress, 0.0f, 1.0f);
    float eased = progress * progress * (3.0f - (2.0f * progress));
    birdX = 270.0f + ((boulderX - 270.0f) * eased);
    birdY = 35.0f + (((boulderY - BOULDER_RADIUS - 4.0f) - 35.0f) * eased)
            - (sinf(progress * PI) * 35.0f);
    drawBird(birdX, birdY, true, wingPhase);
  } else if (birdState == BIRD_RESTING) {
    birdX = boulderX;
    birdY = boulderY - BOULDER_RADIUS - 4.0f;
    bool shaking = rainAmount > 0.35f &&
                   now - birdStateStartedMs < 1800ULL;
    if (shaking) {
      birdX += sinf(now / 35.0f) * 2.0f;
    }
    drawBird(birdX, birdY, false, wingPhase, shaking);
  } else if (birdState == BIRD_FLYING_OUT) {
    float progress = (float)(now - birdStateStartedMs) / BIRD_FLY_OUT_MS;
    progress = constrain(progress, 0.0f, 1.0f);
    float eased = progress * progress * (3.0f - (2.0f * progress));
    birdX = birdTakeoffX + ((-35.0f - birdTakeoffX) * eased);
    birdY = birdTakeoffY + ((25.0f - birdTakeoffY) * eased)
            - (sinf(progress * PI) * 28.0f);
    drawBird(birdX, birdY, true, wingPhase);
  }
}

void drawCelebrationMallard(uint64_t now, float boulderX, float boulderY) {
  if (!milestoneCelebrating) return;

  float progress = constrain(
      (float)(now - milestoneCelebrationStartedMs) /
      MILESTONE_CELEBRATION_MS, 0.0f, 1.0f);
  float angle = -PI + progress * PI * 4.0f;
  float duckX = boulderX + cosf(angle) * 53.0f;
  float duckY = boulderY - 42.0f + sinf(angle) * 19.0f;
  drawBird(duckX, duckY, true, now / 70.0f);
}

void drawWalker(float x, float walkingPhase) {
  int groundX = (int)x;
  int groundY = (int)hillY(x);
  float stride = sinf(walkingPhase);

  int hipX = groundX;
  int hipY = groundY - 24;
  int shoulderX = groundX + 7;
  int shoulderY = groundY - 47;
  int headX = shoulderX + 3;
  int headY = shoulderY - 10;

  // Back leg and front leg alternate as the figure climbs.
  int backFootX = groundX - 10 + (int)(stride * 7);
  int frontFootX = groundX + 11 - (int)(stride * 7);
  int backFootY = (int)hillY(backFootX) - 1;
  int frontFootY = (int)hillY(frontFootX) - 1;

  int backKneeX = groundX - 4 + (int)(stride * 5);
  int backKneeY = groundY - 12;
  int frontKneeX = groundX + 6 - (int)(stride * 5);
  int frontKneeY = groundY - 13;

  int backAnkleX = backFootX + 1;
  int backAnkleY = backFootY - 6;
  int frontAnkleX = frontFootX - 1;
  int frontAnkleY = frontFootY - 6;

  // Rear leg is darker, giving the tiny figure visible depth.
  drawThickLine(hipX - 2, hipY, backKneeX, backKneeY,
                MAN_PANTS_SHADOW, 7);
  frame.fillCircle(backKneeX, backKneeY, 4, MAN_PANTS_SHADOW);
  drawThickLine(backKneeX, backKneeY, backAnkleX, backAnkleY,
                MAN_PANTS_SHADOW, 6);
  drawThickLine(backAnkleX, backAnkleY, backFootX, backFootY,
                MAN_BOOT, 5);
  drawThickLine(backFootX - 2, backFootY,
                backFootX + 6, (int)hillY(backFootX + 6) - 1,
                MAN_BOOT, 4);

  // Front leg catches more light and bends independently while climbing.
  drawThickLine(hipX + 2, hipY, frontKneeX, frontKneeY,
                MAN_PANTS, 7);
  frame.fillCircle(frontKneeX, frontKneeY, 4, MAN_PANTS);
  drawThickLine(frontKneeX, frontKneeY, frontAnkleX, frontAnkleY,
                MAN_PANTS, 6);
  drawThickLine(frontAnkleX, frontAnkleY, frontFootX, frontFootY,
                MAN_BOOT, 5);
  drawThickLine(frontFootX - 2, frontFootY,
                frontFootX + 7, (int)hillY(frontFootX + 7) - 1,
                MAN_BOOT, 4);

  // Both bent arms stay braced against the moving boulder.
  int handX = groundX + 26;
  int handY = (int)hillY(x + 36) - 25;
  int rearElbowX = shoulderX + 9;
  int rearElbowY = shoulderY + 15;
  drawThickLine(shoulderX - 2, shoulderY + 7, rearElbowX, rearElbowY,
                MAN_SKIN_SHADOW, 5);
  drawThickLine(rearElbowX, rearElbowY, handX - 1, handY + 6,
                MAN_SKIN_SHADOW, 5);

  // Filled torso and belt replace the old single stick-like body line.
  int leftShoulderX = shoulderX - 6;
  int leftShoulderY = shoulderY + 2;
  int rightShoulderX = shoulderX + 6;
  int rightShoulderY = shoulderY + 4;
  frame.fillTriangle(leftShoulderX, leftShoulderY,
                     rightShoulderX, rightShoulderY,
                     hipX + 5, hipY, MAN_TUNIC);
  frame.fillTriangle(leftShoulderX, leftShoulderY,
                     hipX + 5, hipY,
                     hipX - 5, hipY, MAN_TUNIC);
  frame.drawLine(leftShoulderX + 2, leftShoulderY + 2,
                 hipX - 2, hipY - 3, MAN_TUNIC_LIGHT);
  drawThickLine(hipX - 5, hipY - 2, hipX + 5, hipY - 2,
                MAN_BELT, 3);

  // Neck, hair, ear, eye, nose, and jaw remain readable at this scale.
  drawThickLine(shoulderX + 1, shoulderY + 2,
                headX - 1, headY + 6, MAN_SKIN, 5);
  frame.fillCircle(headX, headY, 7, MAN_HAIR);
  frame.fillCircle(headX + 1, headY + 1, 6, MAN_SKIN);
  frame.fillCircle(headX - 4, headY + 1, 2, MAN_SKIN_SHADOW);
  frame.fillTriangle(headX + 6, headY - 1,
                     headX + 10, headY + 1,
                     headX + 6, headY + 2, MAN_SKIN);
  frame.drawPixel(headX + 4, headY - 1, MAN_HAIR);
  frame.drawLine(headX + 2, headY + 6,
                 headX + 6, headY + 4, MAN_HAIR);

  // Front arm is brighter and sits above the torso for clear separation.
  int frontElbowX = shoulderX + 12;
  int frontElbowY = shoulderY + 10;
  frame.fillCircle(shoulderX + 3, shoulderY + 5, 4, MAN_TUNIC_LIGHT);
  drawThickLine(shoulderX + 3, shoulderY + 5,
                frontElbowX, frontElbowY, MAN_SKIN, 5);
  frame.fillCircle(frontElbowX, frontElbowY, 3, MAN_SKIN_SHADOW);
  drawThickLine(frontElbowX, frontElbowY, handX, handY,
                MAN_SKIN, 5);
  frame.fillCircle(handX - 1, handY + 6, 3, MAN_SKIN_SHADOW);
  frame.fillCircle(handX, handY, 3, MAN_SKIN);
}

void renderFrame(uint64_t now) {
  float elapsedSeconds = now / 1000.0f;
  float deltaSeconds = (now - previousAnimationMs) / 1000.0f;
  previousAnimationMs = now;

  // Ignore unusually large timing jumps after startup or debugging pauses.
  deltaSeconds = constrain(deltaSeconds, 0.0f, 0.2f);

  updateMilestoneCelebration(now);

  if (birdState != BIRD_RESTING && !milestoneCelebrating) {
    double previousMiles = totalMilesWalked;
    walkerDistance += WALKING_SPEED * deltaSeconds;
    totalMilesWalked += SIMULATED_WALKING_MPH * deltaSeconds / 3600.0;
    distanceNeedsSaving = true;

    BoulderTier earnedTier = boulderTierForMiles(totalMilesWalked);
    if (earnedTier > currentBoulderTier) {
      startMilestoneCelebration(earnedTier, now);
    } else if ((previousMiles < 25000.0 && totalMilesWalked >= 25000.0) ||
               (previousMiles < 75000.0 && totalMilesWalked >= 75000.0)) {
      startStageCelebration(now);
    }
  }

  terrainOffset = walkerDistance;

  // The climber stays centered while the world advances beneath him.
  float walkerX = WALKER_X;
  float walkingPhase = walkerDistance * 0.37f;

  float boulderX = walkerX + 39.0f;
  float boulderY = hillY(boulderX) - BOULDER_RADIUS;

  updateBirdState(now, walkerX, boulderX, boulderY);
  updateWeather(now);

  for (int stripY = 0; stripY < SCREEN_HEIGHT; stripY += STRIP_HEIGHT) {
    frame.setYOffset(stripY);
    frame.clear(ST77XX_BLACK);

    drawBackground(elapsedSeconds, now);
    drawWalker(walkerX, walkingPhase);
    drawBoulder(boulderX, boulderY, now);
    if (milestoneCelebrating) {
      drawCelebrationMallard(now, boulderX, boulderY);
    } else {
      drawBirdForState(now, boulderX, boulderY);
    }
    drawWeatherOverlay(now);
    drawInfoBar();
    drawDistanceGauge();
    drawQuoteBar(now);

    tft.drawRGBBitmap(0, stripY, frame.getBuffer(),
                      SCREEN_WIDTH, STRIP_HEIGHT);
  }

}

void setup() {
  Serial.begin(115200);
  pinMode(WIFI_RESET_BUTTON, INPUT_PULLUP);

  pinMode(TFT_BLK, OUTPUT);
  digitalWrite(TFT_BLK, HIGH);

  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
  tft.init(SCREEN_WIDTH, SCREEN_HEIGHT);
  tft.setRotation(2);  // Rotate the mounted display 180 degrees.

  tft.fillScreen(ST77XX_BLUE);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);
  tft.setCursor(22, 96);
  tft.println("ANIMATION");
  tft.setCursor(34, 122);
  tft.println("STARTING");
  delay(2000);

  if (frame.getBuffer() == nullptr) {
    tft.fillScreen(ST77XX_RED);
    Serial.println("ERROR: Strip framebuffer could not be allocated");
    while (true) delay(1000);
  }

  randomSeed(esp_random());
  distanceStorageReady = preferences.begin("sisyphus", false);
  if (distanceStorageReady) {
    totalMilesWalked = preferences.getDouble("miles", 0.0);
    Serial.print("Lifetime walking distance: ");
    Serial.print(totalMilesWalked, 3);
    Serial.println(" miles");
  } else {
    Serial.println("Distance storage unavailable; mileage will still display.");
  }
  currentBoulderTier = boulderTierForMiles(totalMilesWalked);
  targetBoulderTier = currentBoulderTier;
  lastDistanceSave = millis();

  uint64_t now = clockMs();
  previousAnimationMs = now;
  scheduleNewBirdDay(now);
  setWeather(WEATHER_CLEAR, now);

  connectWifi();
  if (WiFi.status() == WL_CONNECTED) {
    drawStatus("Finding location", "Using the WiFi internet address");
    fetchWifiLocation();
    configTime(timezoneOffsetSeconds, 0, "pool.ntp.org", "time.nist.gov");
    drawStatus("Location found", detectedCity.c_str());
    delay(1200);
    drawStatus("Getting weather");
    updateClockText();
    fetchRealWeather();
    fetchRemoteMessage();
  }

  Serial.println("Send the letter b to trigger a bird visit for testing.");
  Serial.println("A manual visit counts toward today's two-visit limit.");
  Serial.println("Weather test: c=clear, f=fog, r=rain, s=storm, n=snow.");
  Serial.println("Send t to trigger a shooting star for testing.");
  if (!remoteMessageIsConfigured()) {
    Serial.println("Remote message URL is blank; showing the fixed line only.");
  }
}

void loop() {
  unsigned long frameClock = millis();

  if (Serial.available() > 0) {
    char command = Serial.read();
    if ((command == 'b' || command == 'B') && birdState == BIRD_AWAY) {
      startBirdVisit(clockMs());
    } else if (command == 'c' || command == 'C') {
      setWeather(WEATHER_CLEAR, clockMs());
    } else if (command == 'f' || command == 'F') {
      setWeather(WEATHER_FOG, clockMs());
    } else if (command == 'r' || command == 'R') {
      setWeather(WEATHER_RAIN, clockMs());
    } else if (command == 's' || command == 'S') {
      setWeather(WEATHER_STORM, clockMs());
    } else if (command == 'n' || command == 'N') {
      setWeather(WEATHER_SNOW, clockMs());
    } else if (command == 't' || command == 'T') {
      shootingStarStartedMs = clockMs();
    }
  }

  if (frameClock - lastClockUpdate >= 1000UL) {
    lastClockUpdate = frameClock;
    updateClockText();
  }

  if (distanceStorageReady && distanceNeedsSaving &&
      frameClock - lastDistanceSave >= DISTANCE_SAVE_MS) {
    preferences.putDouble("miles", totalMilesWalked);
    distanceNeedsSaving = false;
    lastDistanceSave = frameClock;
  }

  if (WiFi.status() == WL_CONNECTED) {
    if (frameClock - lastWeatherFetch >= WEATHER_REFRESH_MS) {
      fetchRealWeather();
    }
    if (remoteMessageIsConfigured() &&
        frameClock - lastMessageFetch >= MESSAGE_REFRESH_MS) {
      fetchRemoteMessage();
    }
  } else if (frameClock - lastWifiReconnect >= 30000UL) {
    lastWifiReconnect = frameClock;
    WiFi.reconnect();
  }

  if (frameClock - lastFrame >= FRAME_TIME_MS) {
    lastFrame = frameClock;
    renderFrame(clockMs());
  }
}
