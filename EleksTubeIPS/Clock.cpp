// Clock.cpp -- Time management implementation
//
// Time flow: NTP --> RTC --> TimeLib --> POSIX localtime --> display digits
//
// The syncProvider() callback is the heart of this module. TimeLib calls it
// periodically (default every 5 minutes). It tries NTP first, falls back to
// the hardware RTC, and always keeps the ESP32 system clock in sync via
// settimeofday() so that POSIX timezone/DST conversions work correctly.

#include "Clock.h"
#include "WifiManager.h"
#include <sys/time.h>

// ---- Hardware RTC Abstraction ----
// Different clock hardware variants use different RTC chips:
//   - Si Hai clone: DS1302 (3-wire serial, lower accuracy)
//   - EleksTube/NovelLife: DS3231 via DS1307RTC library (I2C, +/-2 ppm)
// The RtcBegin/RtcGet/RtcSet functions abstract this difference so the
// rest of the code doesn't need to know which chip is installed.

#ifdef HARDWARE_SI_HAI_CLOCK
  #include <ThreeWire.h>
  #include <RtcDS1302.h>
  // DS1302 uses a non-standard 3-wire protocol (not SPI, not I2C)
  // requiring dedicated IO, SCLK, and CE (chip enable) pins
  ThreeWire myWire(DS1302_IO, DS1302_SCLK, DS1302_CE);
  RtcDS1302<ThreeWire> Rtc(myWire);

  void RtcBegin() {
    Rtc.Begin();
    // DS1302 can lose its time if the backup battery dies or is removed
    if (!Rtc.IsDateTimeValid()) {
      Serial.println("RTC lost confidence in the DateTime!");
    }
    // DS1302 has a write-protect register that must be cleared before
    // any writes (including time updates from NTP)
    if (Rtc.GetIsWriteProtected()) {
      Serial.println("RTC was write protected, enabling writing now");
      Rtc.SetIsWriteProtected(false);
    }
    if (!Rtc.GetIsRunning()) {
      Serial.println("RTC was not actively running, starting now");
      Rtc.SetIsRunning(true);
    }
  }

  uint32_t RtcGet() {
    return Rtc.GetDateTime();
  }

  void RtcSet(uint32_t tt) {
    Rtc.SetDateTime(tt);
  }
#else
  // DS3231 (via DS1307RTC library) needs no special initialization --
  // it auto-starts on power-up and has no write protection
  #include <DS1307RTC.h>

  void RtcBegin() {}

  uint32_t RtcGet() {
    return RTC.get();
  }

  void RtcSet(uint32_t tt) {
    RTC.set(tt);
  }
#endif

// ---- Initialization ----

void Clock::begin(StoredConfig::Config::Clock *config_) {
  config = config_;

  // If config has never been written (fresh flash), initialize with defaults.
  // The is_valid sentinel detects this; see StoredConfig.h for details.
  if (config->is_valid != StoredConfig::valid) {
    Serial.println("Loaded Clock config is invalid, using default.  This is normal on first boot.");
    setTwelveHour(false);
    setBlankHoursZero(false);
    config->time_zone_offset = 0;
    setActiveGraphicIdx(1);
    // Default to UTC; user sets their POSIX TZ string via the web interface
    strncpy(config->tz_string, "UTC0", sizeof(config->tz_string));
    config->night_time = NIGHT_TIME;
    config->day_time = DAY_TIME;
    config->night_mode_enabled = true;
    config->day_tft_intensity = 255;
    config->night_tft_intensity = TFT_DIMMED_INTENSITY;
    config->night_led_intensity = BACKLIGHT_DIMMED_INTENSITY;
    config->hue_shift = 0;
    config->is_valid = StoredConfig::valid;
  }

  // Apply POSIX timezone string to the C library.
  // setenv("TZ",...) + tzset() configures the ESP32's newlib so that
  // localtime_r() automatically handles UTC offset AND DST transitions.
  // Example TZ strings:
  //   "EST5EDT,M3.2.0,M11.1.0"  -- US Eastern with DST
  //   "AEST-10AEDT,M10.1.0,M4.1.0/3" -- Australian Eastern with DST
  //   "CST-8" -- China Standard Time (no DST)
  if (config->tz_string[0] != '\0') {
    setenv("TZ", config->tz_string, 1);
    tzset();
    Serial.print("Timezone set: ");
    Serial.println(config->tz_string);
  }

  RtcBegin();

  // Attempt initial NTP sync at boot
  ntpTimeClient.begin();
  ntpTimeClient.update();
  Serial.print("NTP time = ");
  Serial.println(ntpTimeClient.get_formatted_time());

  // Register our syncProvider as the time source for TimeLib.
  // TimeLib will call this function periodically (default every 300s)
  // to keep its internal clock synchronized.
  setSyncProvider(&Clock::syncProvider);
}

// ---- Main Loop ----

void Clock::loop() {
  if (timeStatus() == timeNotSet) {
    time_valid = false;
  } else {
    time_t prev_local = local_time;

    // now() returns UTC from TimeLib
    loop_time = now();

    if (config->tz_string[0] != '\0') {
      // Use the ESP32 system clock (set via settimeofday in syncProvider)
      // with POSIX TZ rules to compute local time including DST.
      // We can't just call localtime on loop_time directly because TimeLib
      // and the system clock are separate time sources. Instead, we compute
      // the UTC-to-local offset from the system clock and apply it to
      // TimeLib's UTC time.
      time_t sys_time = time(nullptr);
      struct tm local_tm, utc_tm;
      localtime_r(&sys_time, &local_tm);
      gmtime_r(&sys_time, &utc_tm);

      // Calculate the difference between local and UTC to get the
      // effective timezone offset (includes DST if active)
      int diff_sec = (local_tm.tm_hour - utc_tm.tm_hour) * 3600
                   + (local_tm.tm_min - utc_tm.tm_min) * 60
                   + (local_tm.tm_sec - utc_tm.tm_sec);

      // Handle day boundary: if local is just past midnight but UTC is
      // still the previous day (or vice versa), the raw difference wraps.
      // Clamp to +/-14 hours to cover the full legal range of UTC offsets
      // (UTC-12 to UTC+14, e.g. Pacific/Kiritimati is UTC+14).
      if (diff_sec > 50400) diff_sec -= 86400;
      if (diff_sec < -50400) diff_sec += 86400;

      local_time = loop_time + diff_sec;
    } else {
      // Fallback: use the legacy manual offset (no DST support)
      local_time = loop_time + config->time_zone_offset;
    }

    // Detect time jumps from NTP correction, timezone change, or RTC reset.
    // Normal ticking advances by exactly 0 or 1 second per loop. Anything
    // larger means the time source changed — force all displays to redraw
    // so hours and minutes update immediately, not just seconds.
    time_t jump = local_time - prev_local;
    if (prev_local != 0 && (jump > 2 || jump < -2)) {
      force_redraw = true;
      Serial.printf("Time jump detected: %ld seconds, forcing display refresh\n", (long)jump);
    }

    time_valid = true;
  }
}

// ---- TimeLib Sync Provider ----
// This is called by TimeLib to obtain authoritative UTC time.
// It implements a tiered fallback: NTP > RTC > (previous value).
// It also keeps the ESP32 system clock in sync so that POSIX
// timezone functions (localtime_r, etc.) work correctly.

time_t Clock::syncProvider() {
  Serial.println("syncProvider()");
  time_t ntp_now, rtc_now;
  rtc_now = RtcGet();

  // Only query NTP if enough time has elapsed (1 hour) or on first call.
  // This prevents hammering NTP servers and reduces network traffic.
  if (millis() - millis_last_ntp > refresh_ntp_every_ms) {
    if (WifiState == connected) {
      Serial.print("Getting NTP.");
      if (ntpTimeClient.update()) {
        Serial.print(".");
        ntp_now = ntpTimeClient.get_epoch_time();
        Serial.println("NTP query done.");
        Serial.print("NTP time = ");
        Serial.println(ntpTimeClient.get_formatted_time());
        Serial.println("NTP, RTC, Diff: ");
        Serial.println(ntp_now);
        Serial.println(rtc_now);
        Serial.println(ntp_now - rtc_now);

        // Keep the hardware RTC synchronized with NTP.
        // RTC crystals drift ~2 ppm, which is ~1 minute/year --
        // periodic NTP correction prevents visible clock error.
        if (ntp_now != rtc_now) {
          RtcSet(ntp_now);
          Serial.println("Updating RTC");
        }

        // Set the ESP32's internal POSIX system clock so that
        // localtime_r() and tzset() can compute local time with DST.
        // This is separate from TimeLib's clock.
        struct timeval tv = { .tv_sec = ntp_now, .tv_usec = 0 };
        settimeofday(&tv, NULL);
        millis_last_ntp = millis();
        Serial.println("Using NTP time.");
        return ntp_now;
      } else {
        // NTP failed (timeout, bad response, etc.) -- fall back to RTC.
        // Set a partial backoff so we retry in 5 minutes, not immediately.
        // Retry in 5 minutes by setting millis_last_ntp so that
        // (millis() - millis_last_ntp) exceeds refresh_ntp_every_ms
        // after 300,000 ms. The unsigned subtraction handles rollover
        // correctly — even if millis() < 3,300,000, the underflow
        // wraps and produces the desired 5-minute retry delay.
        millis_last_ntp = millis() - (refresh_ntp_every_ms - 300000UL);
        Serial.println("Invalid NTP response, using RTC time.");
        struct timeval tv2 = { .tv_sec = (time_t)rtc_now, .tv_usec = 0 };
        settimeofday(&tv2, NULL);
        return rtc_now;
      }
    }
    // No WiFi connection -- use RTC time. Back off 5 minutes before retrying.
    // Retry in 5 minutes by setting millis_last_ntp so that
        // (millis() - millis_last_ntp) exceeds refresh_ntp_every_ms
        // after 300,000 ms. The unsigned subtraction handles rollover
        // correctly — even if millis() < 3,300,000, the underflow
        // wraps and produces the desired 5-minute retry delay.
        millis_last_ntp = millis() - (refresh_ntp_every_ms - 300000UL);
    Serial.println("No WiFi, using RTC time.");
    struct timeval tv3 = { .tv_sec = (time_t)rtc_now, .tv_usec = 0 };
    settimeofday(&tv3, NULL);
    return rtc_now;
  }
  // Between NTP refresh intervals, just return RTC time without
  // touching the system clock (it's already set from last NTP sync)
  return rtc_now;
}

// ---- Display Helpers ----

uint8_t Clock::getHoursTens() {
  uint8_t hour_tens = getHour() / 10;
  // When blank_hours_zero is enabled, suppress the leading zero on hours
  // (e.g., display " 9:30" instead of "09:30"). TFTs::blanked is a
  // special sentinel value that tells the TFT driver to show nothing.
  if (config->blank_hours_zero && hour_tens == 0) {
    return TFTs::blanked;
  } else {
    return hour_tens;
  }
}

bool Clock::forceNTPSync() {
  if (WifiState != connected) return false;
  Serial.println("Manual NTP sync requested");
  if (ntpTimeClient.force_update()) {
    time_t ntp_now = ntpTimeClient.get_epoch_time();
    RtcSet(ntp_now);
    // Update the ESP32 system clock so localtime_r() reflects the new
    // time immediately (used by Clock::loop() for timezone conversion
    // and by handleGetStatus() for the displayed time).
    struct timeval tv = { .tv_sec = ntp_now, .tv_usec = 0 };
    settimeofday(&tv, NULL);
    // Update TimeLib directly so now() returns the correct time
    // immediately rather than waiting for the next sync interval.
    setTime(ntp_now);
    millis_last_ntp = millis();
    Serial.print("NTP sync OK: ");
    Serial.println(ntpTimeClient.get_formatted_time());
    return true;
  }
  Serial.println("NTP sync failed");
  return false;
}

// ---- Static Member Initialization ----
// These must be defined at file scope because they're static class members.
// The NTPClient takes a reference to ntpUDP, so ntpUDP must be defined first.
// Initialize to max so the unsigned subtraction millis() - millis_last_ntp
// immediately exceeds refresh_ntp_every_ms on the first call, triggering
// an NTP sync at boot without a special-case sentinel check.
uint32_t Clock::millis_last_ntp = UINT32_MAX;
WiFiUDP Clock::ntpUDP;
NTPClient Clock::ntpTimeClient(ntpUDP);
