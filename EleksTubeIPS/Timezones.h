// Timezones.h — Timezone database with POSIX TZ strings and IANA identifiers
//
// This table lets the clock convert UTC (from NTP) to local time with
// automatic DST transitions, without needing the full IANA tz database
// (which is too large for an ESP32's flash).
//
// Each entry has three fields:
//   label     — Human-readable name shown in the clock's web config UI
//   posix_tz  — POSIX TZ string that the ESP32's newlib uses for time conversion
//   iana      — IANA timezone ID used for browser-based auto-detection
//               (JavaScript's Intl.DateTimeFormat().resolvedOptions().timeZone
//               returns an IANA ID, which we match to find the right entry)
//
// -----------------------------------------------------------------------
// POSIX TZ string format (decoded by example):
//
//   "EST5EDT,M3.2.0,M11.1.0"
//    ^^^                        Standard time abbreviation (EST)
//       ^                       UTC offset in hours (5 = UTC-5). NOTE: POSIX
//                                uses the opposite sign convention from ISO 8601
//                                (positive = west of GMT, negative = east).
//        ^^^                    DST abbreviation (EDT)
//            ^^^^^^^^           DST start rule: M3.2.0 = Month 3, week 2, day 0
//                                (March, 2nd Sunday). Default transition at 02:00.
//                     ^^^^^^^^  DST end rule: M11.1.0 = November, 1st Sunday.
//
//   Fractional offsets use colons: "IST-5:30" = UTC+5:30 (India)
//   Angle-bracket names for non-standard abbreviations: "<+0545>-5:45" (Nepal)
//   Julian day rules use J notation: "J79/24,J263/24" (Iran — fixed calendar days)
//   Zones without DST omit the second part: "MST7" (Arizona), "JST-9" (Japan)
//
// The /N suffix on transition rules specifies the wall-clock hour of the switch
// (default is /2, i.e., 2:00 AM). Example: "M10.5.0/3" = last Sunday of
// October at 03:00 local time.
// -----------------------------------------------------------------------
//
// PROGMEM stores the table in flash instead of RAM — important because the
// ESP32 has limited SRAM and this table is only read during timezone selection.

#ifndef TIMEZONES_H
#define TIMEZONES_H

#include <Arduino.h>

struct TZEntry {
  const char* label;      // Display name for the UI
  const char* posix_tz;   // POSIX TZ string for newlib's setenv("TZ", ...)
  const char* iana;        // IANA ID for browser auto-detection matching
};

const TZEntry tz_entries[] PROGMEM = {
  // --- UTC and Americas (west to east) ---
  {"UTC",                           "UTC0",                                     "Etc/UTC"},
  {"Hawaii",                        "HST10",                                    "Pacific/Honolulu"},
  {"Alaska",                        "AKST9AKDT,M3.2.0,M11.1.0",                "America/Anchorage"},
  {"US/Canada Pacific",             "PST8PDT,M3.2.0,M11.1.0",                  "America/Los_Angeles"},
  {"US/Canada Mountain",            "MST7MDT,M3.2.0,M11.1.0",                  "America/Denver"},
  {"Arizona (no DST)",              "MST7",                                     "America/Phoenix"},
  {"US/Canada Central",             "CST6CDT,M3.2.0,M11.1.0",                  "America/Chicago"},
  {"US/Canada Eastern",             "EST5EDT,M3.2.0,M11.1.0",                  "America/New_York"},
  {"Canada Atlantic",               "AST4ADT,M3.2.0,M11.1.0",                  "America/Halifax"},
  {"Newfoundland",                  "NST3:30NDT,M3.2.0,M11.1.0",               "America/St_Johns"},
  {"Brazil (Brasilia)",             "<-03>3",                                   "America/Sao_Paulo"},
  {"Argentina",                     "<-03>3",                                   "America/Argentina/Buenos_Aires"},

  // --- Atlantic islands ---
  {"South Georgia",                 "<-02>2",                                   "Atlantic/South_Georgia"},
  {"Azores",                        "<-01>1<+00>,M3.5.0/0,M10.5.0/1",          "Atlantic/Azores"},
  {"Cape Verde",                    "<-01>1",                                   "Atlantic/Cape_Verde"},

  // --- Europe and Africa ---
  {"UK / Ireland",                  "GMT0BST,M3.5.0/1,M10.5.0",                "Europe/London"},
  {"Iceland",                       "GMT0",                                     "Atlantic/Reykjavik"},
  {"Portugal",                      "WET0WEST,M3.5.0/1,M10.5.0",               "Europe/Lisbon"},
  {"Western Europe",                "CET-1CEST,M3.5.0,M10.5.0/3",              "Europe/Paris"},
  {"Central Europe",                "CET-1CEST,M3.5.0,M10.5.0/3",              "Europe/Berlin"},
  {"Eastern Europe",                "EET-2EEST,M3.5.0/3,M10.5.0/4",            "Europe/Bucharest"},
  {"Finland / Baltic",              "EET-2EEST,M3.5.0/3,M10.5.0/4",            "Europe/Helsinki"},
  {"Greece",                        "EET-2EEST,M3.5.0/3,M10.5.0/4",            "Europe/Athens"},
  {"Turkey",                        "<+03>-3",                                  "Europe/Istanbul"},
  {"Moscow / East Africa",          "MSK-3",                                    "Europe/Moscow"},

  // --- Middle East and Central Asia ---
  {"Iran",                          "<+0330>-3:30<+0430>,J79/24,J263/24",       "Asia/Tehran"},
  {"Gulf / UAE / Oman",             "<+04>-4",                                  "Asia/Dubai"},
  {"Afghanistan",                   "<+0430>-4:30",                             "Asia/Kabul"},
  {"Pakistan / Uzbekistan",         "PKT-5",                                    "Asia/Karachi"},

  // --- South Asia (note the unusual fractional offsets) ---
  {"India / Sri Lanka",             "IST-5:30",                                 "Asia/Kolkata"},
  {"Nepal",                         "<+0545>-5:45",                             "Asia/Kathmandu"},
  {"Bangladesh",                    "<+06>-6",                                  "Asia/Dhaka"},
  {"Myanmar",                       "<+0630>-6:30",                             "Asia/Yangon"},

  // --- East and Southeast Asia ---
  {"Thailand / Vietnam",            "<+07>-7",                                  "Asia/Bangkok"},
  {"China / Hong Kong / Singapore", "CST-8",                                    "Asia/Shanghai"},
  {"Australia Western",             "AWST-8",                                   "Australia/Perth"},
  {"Japan / Korea",                 "JST-9",                                    "Asia/Tokyo"},

  // --- Australia and Oceania ---
  {"Australia Central (SA)",        "ACST-9:30ACDT,M10.1.0,M4.1.0/3",          "Australia/Adelaide"},
  {"Australia Central (NT)",        "ACST-9:30",                                "Australia/Darwin"},
  {"Australia Eastern",             "AEST-10AEDT,M10.1.0,M4.1.0/3",            "Australia/Sydney"},
  {"Australia (Queensland)",        "AEST-10",                                  "Australia/Brisbane"},
  {"Papua New Guinea",              "<+10>-10",                                 "Pacific/Port_Moresby"},
  {"Solomon Islands",               "<+11>-11",                                 "Pacific/Guadalcanal"},
  {"New Zealand",                   "NZST-12NZDT,M9.5.0,M4.1.0/3",             "Pacific/Auckland"},
  {"Fiji",                          "<+12>-12<+13>,M11.2.0,M1.2.3/99",          "Pacific/Fiji"},
  {"Tonga",                         "<+13>-13",                                 "Pacific/Tongatapu"},
};

// Compute array length at compile time — avoids hardcoding a count that
// could get out of sync when entries are added or removed
const int tz_entry_count = sizeof(tz_entries) / sizeof(tz_entries[0]);

#endif // TIMEZONES_H
