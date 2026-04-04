// Clock.h -- Time management for the EleksTube IPS clock
//
// This module orchestrates a three-tier timekeeping architecture:
//
//   NTP (internet) --> RTC (battery-backed hardware) --> TimeLib --> Display
//
// When WiFi is available, NTP provides authoritative UTC time and also
// updates the hardware RTC as a backup. When WiFi is unavailable, the
// RTC provides time directly. TimeLib (Paul Stoffregen's library) acts
// as the central time authority in firmware via its setSyncProvider()
// callback pattern.
//
// Timezone handling uses POSIX TZ strings (e.g., "EST5EDT,M3.2.0,M11.1.0")
// which encode both UTC offset AND DST transition rules. This is set via
// setenv("TZ",...) and tzset(), then the ESP32's libc handles localtime_r()
// conversions automatically -- no manual DST logic needed.

#ifndef CLOCK_H
#define CLOCK_H

#include <stdint.h>
#include "GLOBAL_DEFINES.h"
#include <TimeLib.h>
#include <WiFi.h>
#include "NTPClient_AO.h"
#include "StoredConfig.h"
#include "TFTs.h"

class Clock {
public:
  Clock() : loop_time(0), local_time(0), time_valid(false), config(NULL) {}

  // WiFi must be started before calling begin() so NTP can reach the server
  void begin(StoredConfig::Config::Clock *config_);
  void loop();

  // Static callback for TimeLib's setSyncProvider() mechanism.
  // TimeLib calls this periodically to get authoritative UTC time.
  // Must be static because setSyncProvider takes a plain function pointer.
  static time_t syncProvider();

  void setTwelveHour(bool th)             { config->twelve_hour = th; }
  bool getTwelveHour()                    { return config->twelve_hour; }
  void toggleTwelveHour()                 { config->twelve_hour = !config->twelve_hour; }

  void setBlankHoursZero(bool bhz)        { config->blank_hours_zero = bhz; }
  bool getBlankHoursZero()                { return config->blank_hours_zero; }
  void toggleBlankHoursZero()             { config->blank_hours_zero = !config->blank_hours_zero; }

  void setActiveGraphicIdx(int8_t idx)    { config->selected_graphic = idx; }
  int8_t getActiveGraphicIdx()            { return config->selected_graphic; }
  void adjustClockGraphicsIdx(int8_t adj) {
    config->selected_graphic += adj;
    if (config->selected_graphic > tfts.NumberOfClockFaces) { config->selected_graphic = tfts.NumberOfClockFaces; }
    if (config->selected_graphic < 1) { config->selected_graphic = 1; }
  }

  // getHour() respects 12/24-hour preference; getHour24() always returns 24-hour
  uint8_t getHour()        { return config->twelve_hour ? hourFormat12(local_time) : hour(local_time); }
  uint8_t getHour24()      { return hour(local_time); }
  uint8_t getMinute()      { return minute(local_time); }
  uint8_t getSecond()      { return second(local_time); }

  // Digit extraction for driving the six individual TFT displays
  uint8_t getHoursTens();
  uint8_t getHoursOnes()   { return getHour() % 10; }
  uint8_t getMinutesTens() { return getMinute() / 10; }
  uint8_t getMinutesOnes() { return getMinute() % 10; }
  uint8_t getSecondsTens() { return getSecond() / 10; }
  uint8_t getSecondsOnes() { return getSecond() % 10; }

  // Force NTP sync now (called from web UI or button press)
  bool forceNTPSync();

  // Set by loop() when a time jump > 2 seconds is detected (NTP correction,
  // timezone change, etc.). The main loop checks this and force-redraws all
  // six displays so that hours/minutes update immediately, not just seconds.
  bool force_redraw = false;

private:
  time_t loop_time, local_time;
  bool time_valid;
  StoredConfig::Config::Clock *config;

  // Static members because syncProvider() is a static callback and needs
  // access to the NTP client and timing state
  static WiFiUDP ntpUDP;
  static NTPClient ntpTimeClient;
  static uint32_t millis_last_ntp;
  // Re-sync with NTP every hour to correct for RTC drift (~2 ppm typical)
  const static uint32_t refresh_ntp_every_ms = 3600000;
};

#endif // CLOCK_H
