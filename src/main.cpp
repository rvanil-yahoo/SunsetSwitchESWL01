#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <time.h>
#include <math.h>

// ---------- WiFi — credentials managed by WiFiManager (no hardcoded secrets) ----------
// On first boot: connect to "SunsetSwitch-Setup" AP and enter credentials via browser.
// Credentials are saved to flash and reused automatically on subsequent boots.

// ---------- Hardware ----------
#define RELAY_PIN    13     // GPIO13 — HIGH = relay ON (nighttime)
#define LED_PIN       5     // GPIO5  — status LED, mirrors relay state
#define TOUCH_PIN    14     // GPIO14 — ADAM02S capacitive sensor, HIGH = touched

// ---------- Schedule offsets ----------
#define MINUTES_BEFORE_SUNSET  60   // turn ON this many minutes before sunset
#define MINUTES_AFTER_SUNRISE  60   // turn OFF this many minutes after sunrise

// ---------- Touch override ----------
// A touch toggles the relay override; a second touch clears it (auto mode resumes).
bool     overrideActive = false;
bool     overrideState  = false;    // desired relay state when override is active
bool     lastTouchState = false;
unsigned long lastDebounceMs = 0;
#define DEBOUNCE_MS 300

// ---------- Location: Sammamish, WA ----------
const float LAT        =  47.6163f;
const float LON        = -122.0356f;
const int   UTC_OFFSET = -8;        // PST base; DST handled via TZ string

// ---------- Timing ----------
#define NTP_SYNC_INTERVAL_MS   (60UL * 60UL * 1000UL)  // re-sync NTP every hour
#define RELAY_CHECK_INTERVAL_MS (60UL * 1000UL)         // evaluate relay every minute

unsigned long lastNtpSync    = 0;
unsigned long lastRelayCheck = 0;

// -------------------------------------------------------
// Sunrise/sunset algorithm (NOAA simplified)
// Returns minutes from local midnight, or -1 on polar day/night.
// -------------------------------------------------------
static int calcSunTime(int year, int month, int day,
                       float lat, float lon,
                       int utcOffset, bool isDST, bool isRise) {
    const float DEG = M_PI / 180.0f;

    int n1 = (int)(275.0f * month / 9.0f);
    int n2 = (int)((month + 9.0f) / 12.0f);
    int n3 = 1 + (int)((year - 4 * (year / 4) + 2) / 3.0f);
    int N  = n1 - n2 * n3 + day - 30;

    float lngHour = lon / 15.0f;
    float t = N + ((isRise ? 6.0f : 18.0f) - lngHour) / 24.0f;

    float M = (0.9856f * t) - 3.289f;

    float L = M + (1.916f * sinf(M * DEG)) + (0.020f * sinf(2.0f * M * DEG)) + 282.634f;
    while (L >= 360.0f) L -= 360.0f;
    while (L <    0.0f) L += 360.0f;

    float RA = (1.0f / DEG) * atanf(0.91764f * tanf(L * DEG));
    while (RA >= 360.0f) RA -= 360.0f;
    while (RA <    0.0f) RA += 360.0f;
    RA = (RA + (floorf(L / 90.0f) * 90.0f - floorf(RA / 90.0f) * 90.0f)) / 15.0f;

    float sinDec = 0.39782f * sinf(L * DEG);
    float cosDec = cosf(asinf(sinDec));

    float cosH = (cosf(90.833f * DEG) - sinDec * sinf(lat * DEG))
                 / (cosDec * cosf(lat * DEG));
    if (cosH > 1.0f || cosH < -1.0f) return -1;

    float H = isRise ? (360.0f - (1.0f / DEG) * acosf(cosH))
                     : (         (1.0f / DEG) * acosf(cosH));
    H /= 15.0f;

    float T  = H + RA - (0.06571f * t) - 6.622f;
    float UT = T - lngHour;
    while (UT >= 24.0f) UT -= 24.0f;
    while (UT <   0.0f) UT += 24.0f;

    float local = UT + utcOffset + (isDST ? 1.0f : 0.0f);
    while (local >= 24.0f) local -= 24.0f;
    while (local <   0.0f) local += 24.0f;

    return (int)(local * 60.0f);
}

// -------------------------------------------------------
void connectWiFi() {
    WiFiManager wm;
    // If saved credentials fail, start "SunsetSwitch-Setup" AP for 3 minutes.
    // Open http://192.168.4.1 on your phone to enter WiFi credentials.
    wm.setConfigPortalTimeout(180);
    if (!wm.autoConnect("SunsetSwitch-Setup")) {
        Serial.println("WiFiManager timed out — rebooting");
        ESP.restart();
    }
    Serial.printf("WiFi connected — IP: %s\n", WiFi.localIP().toString().c_str());
}

// -------------------------------------------------------
void syncNTP() {
    // POSIX TZ handles PST/PDT transitions automatically
    configTime("PST8PDT,M3.2.0,M11.1.0", "pool.ntp.org", "time.nist.gov");
    Serial.print("Syncing NTP");
    time_t now = time(nullptr);
    while (now < 100000UL) {
        delay(500);
        Serial.print(".");
        now = time(nullptr);
    }
    Serial.println(" done");
    lastNtpSync = millis();
}

// -------------------------------------------------------
bool isNighttime() {
    time_t now = time(nullptr);
    struct tm *t = localtime(&now);

    int  year   = t->tm_year + 1900;
    int  month  = t->tm_mon + 1;
    int  day    = t->tm_mday;
    bool dst    = (t->tm_isdst > 0);
    int  nowMin = t->tm_hour * 60 + t->tm_min;

    int sunriseMin = calcSunTime(year, month, day, LAT, LON, UTC_OFFSET, dst, true);
    int sunsetMin  = calcSunTime(year, month, day, LAT, LON, UTC_OFFSET, dst, false);

    // Apply offsets: ON before sunset, OFF after sunrise
    int onMin  = sunsetMin  - MINUTES_BEFORE_SUNSET;
    int offMin = sunriseMin + MINUTES_AFTER_SUNRISE;

    Serial.printf("[%04d-%02d-%02d %02d:%02d DST=%d] sunrise=%02d:%02d sunset=%02d:%02d  on=%02d:%02d off=%02d:%02d\n",
        year, month, day, t->tm_hour, t->tm_min, (int)dst,
        sunriseMin / 60, sunriseMin % 60,
        sunsetMin  / 60, sunsetMin  % 60,
        onMin  / 60, onMin  % 60,
        offMin / 60, offMin % 60);

    // ON from onMin (before sunset) through midnight, and from midnight to offMin (after sunrise)
    return (nowMin >= onMin || nowMin < offMin);
}

// -------------------------------------------------------
// Called on each touch event (after debounce).
// First touch: override ON or OFF (flips current state).
// Second touch: clear override, resume auto schedule.
void handleTouch() {
    if (!overrideActive) {
        // Read current relay state and flip it
        overrideState  = !digitalRead(RELAY_PIN);
        overrideActive = true;
        Serial.printf("Touch override ON — relay forced %s\n", overrideState ? "ON" : "OFF");
    } else {
        overrideActive = false;
        Serial.println("Touch override cleared — resuming auto schedule");
    }
    bool state = overrideActive ? overrideState : (bool)digitalRead(RELAY_PIN);
    digitalWrite(RELAY_PIN, state ? HIGH : LOW);
    digitalWrite(LED_PIN,   state ? HIGH : LOW);
}

// -------------------------------------------------------
void updateRelay() {
    if (overrideActive) {
        lastRelayCheck = millis();
        return;     // manual override in effect; skip auto logic
    }
    bool night = isNighttime();
    digitalWrite(RELAY_PIN, night ? HIGH : LOW);
    digitalWrite(LED_PIN,   night ? HIGH : LOW);
    Serial.printf("Relay/LED -> %s\n", night ? "ON (night)" : "OFF (day)");
    lastRelayCheck = millis();
}

// -------------------------------------------------------
void setup() {
    Serial.begin(115200);
    pinMode(RELAY_PIN, OUTPUT);
    pinMode(LED_PIN,   OUTPUT);
    pinMode(TOUCH_PIN, INPUT);
    digitalWrite(RELAY_PIN, LOW);   // safe default until time is known
    digitalWrite(LED_PIN,   LOW);

    connectWiFi();
    syncNTP();
    updateRelay();
}

// -------------------------------------------------------
void loop() {
    unsigned long now = millis();

    // Capacitive touch detection with debounce
    bool touched = digitalRead(TOUCH_PIN);
    if (touched && !lastTouchState && (now - lastDebounceMs > DEBOUNCE_MS)) {
        lastDebounceMs = now;
        handleTouch();
    }
    lastTouchState = touched;

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi lost — reconnecting...");
        WiFi.reconnect();
        delay(5000);
    }

    if (now - lastNtpSync >= NTP_SYNC_INTERVAL_MS) {
        syncNTP();
    }

    if (now - lastRelayCheck >= RELAY_CHECK_INTERVAL_MS) {
        updateRelay();
    }

    delay(10);  // short delay to keep touch responsive
}

