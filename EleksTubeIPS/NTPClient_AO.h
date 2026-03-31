// NTPClient_AO.h -- NTP (Network Time Protocol) client for ESP32
//
// NTP synchronizes clocks over the internet by querying time servers that
// maintain highly accurate clocks (typically GPS or atomic). This client
// sends a 48-byte UDP request to an NTP server and parses the response to
// extract UTC epoch time. It includes response validation to reject spoofed
// or corrupt packets -- checks that many simple NTP libraries skip.
//
// Architecture: The caller provides a UDP transport (typically WiFiUDP on
// ESP32). The client handles packet formatting, timing, and epoch extraction.
// Time between NTP queries is interpolated using millis() to avoid hammering
// the server while still providing second-level accuracy.

#ifndef NTP_CLIENT_H_
#define NTP_CLIENT_H_

#include "Arduino.h"
#include <Udp.h>

// NTP epoch starts Jan 1, 1900; Unix epoch starts Jan 1, 1970.
// This constant is the number of seconds in those 70 years (including
// 17 leap years), used to convert NTP timestamps to Unix timestamps.
#define SEVENZYYEARS 2208988800UL

// NTP packets are always exactly 48 bytes (fixed by the protocol spec, RFC 5905)
#define NTP_PACKET_SIZE 48

// Local UDP port for receiving NTP responses (arbitrary; not the server port)
#define NTP_DEFAULT_LOCAL_PORT 1337

#ifdef DEBUG_OUTPUT
  #define DEBUG_NTPClient
#endif

class NTPClient {
public:
  // Multiple constructors allow flexible configuration.
  // The UDP reference is stored as a pointer, so the caller must keep it alive.
  NTPClient(UDP& udp);
  NTPClient(UDP& udp, long time_offset);
  NTPClient(UDP& udp, const char* pool_server_name);
  NTPClient(UDP& udp, const char* pool_server_name, long time_offset);
  NTPClient(UDP& udp, const char* pool_server_name, long time_offset, unsigned long update_interval);

  void set_pool_server_name(const char* pool_server_name);
  void begin();
  void begin(int port);

  // Call in main loop; updates from NTP server per _updateInterval
  bool update();

  // Forces an immediate NTP update (blocking -- waits up to ~1s for response)
  bool force_update();

  int get_day() const;
  int get_hours() const;
  int get_minutes() const;
  int get_seconds() const;

  void set_time_offset(int time_offset);
  void set_update_interval(unsigned long update_interval);

  // Returns time as "hh:mm:ss"
  String get_formatted_time() const;

  // Returns Unix epoch seconds (seconds since Jan 1, 1970 00:00:00 UTC)
  unsigned long get_epoch_time() const;

  void end();

private:
  UDP*          _udp;
  bool          _udp_setup       = false;

  // pool.ntp.org is a worldwide cluster of NTP servers that automatically
  // routes to a geographically nearby server via DNS round-robin
  const char*   _pool_server_name = "pool.ntp.org";
  int           _port             = NTP_DEFAULT_LOCAL_PORT;
  // Manual offset in seconds, applied on top of UTC (deprecated in favor of POSIX TZ)
  long          _time_offset      = 0;

  unsigned long _update_interval  = 60000;  // ms between NTP queries
  unsigned long _current_epoch    = 0;      // last-received Unix epoch (seconds)
  unsigned long _last_update      = 0;      // millis() when _current_epoch was set

  bool send_ntp_packet();
};

#endif // NTP_CLIENT_H_
