// NTPClient_AO.cpp -- NTP client implementation
//
// NTP communication flow:
//   1. Client sends a 48-byte UDP packet to port 123 on the NTP server
//   2. Server responds with a 48-byte packet containing timestamps
//   3. Client extracts the "transmit timestamp" (bytes 40-43) which is
//      seconds since Jan 1, 1900
//   4. Client subtracts the 70-year offset to get Unix epoch time
//
// Between NTP queries, get_epoch_time() interpolates using millis() so
// the clock doesn't freeze between updates.

#include "NTPClient_AO.h"

#ifdef DEBUG_NTPClient
  // F() macro stores string in flash instead of RAM -- important on ESP32
  // where string literals in RAM consume limited SRAM
  #define DBG(X) Serial.println(F(X))
#else
  // Compiles to nothing; the (void)0 prevents "empty statement" warnings
  #define DBG(X) (void)0
#endif

// ---- Constructors ----
// All constructors store a pointer to the caller's UDP object.
// The UDP object must outlive this NTPClient (typically both are global).

NTPClient::NTPClient(UDP& udp) {
  this->_udp = &udp;
}

NTPClient::NTPClient(UDP& udp, long time_offset) {
  this->_udp = &udp;
  this->_time_offset = time_offset;
}

NTPClient::NTPClient(UDP& udp, const char* pool_server_name) {
  this->_udp = &udp;
  this->_pool_server_name = pool_server_name;
}

NTPClient::NTPClient(UDP& udp, const char* pool_server_name, long time_offset) {
  this->_udp = &udp;
  this->_time_offset = time_offset;
  this->_pool_server_name = pool_server_name;
}

NTPClient::NTPClient(UDP& udp, const char* pool_server_name, long time_offset, unsigned long update_interval) {
  this->_udp = &udp;
  this->_time_offset = time_offset;
  this->_pool_server_name = pool_server_name;
  this->_update_interval = update_interval;
}

void NTPClient::begin() {
  this->begin(NTP_DEFAULT_LOCAL_PORT);
}

void NTPClient::begin(int port) {
  this->_port = port;
  // Bind a local UDP socket to receive NTP responses
  this->_udp->begin(this->_port);
  this->_udp_setup = true;
}

// ---- NTP Query and Response Parsing ----

bool NTPClient::force_update() {
  DBG("Update from NTP Server...");

  // Discard any stale packets sitting in the UDP receive buffer from
  // previous queries that may have arrived late
  while (this->_udp->parsePacket() != 0)
    this->_udp->flush();

  if (!this->send_ntp_packet()) {
    DBG("NTP err: Could not send packet");
    return false;
  }

  // Poll for response with 10ms intervals, up to ~1 second total.
  // This is blocking -- acceptable here because NTP sync is infrequent
  // and the clock display tolerates brief pauses.
  byte timeout = 0;
  int cb = 0;
  do {
    delay(10);
    cb = this->_udp->parsePacket();
    if (timeout > 100) {
      DBG("NTP Timeout!");
      return false; // timeout after 1000 ms
    }
    timeout++;
  } while (cb == 0);

  // Back-calculate the exact millis() when the NTP time was valid,
  // compensating for the polling delay we just introduced
  this->_last_update = millis() - (10 * (timeout + 1));

  byte packet_buffer[NTP_PACKET_SIZE];
  memset(packet_buffer, 0, sizeof(packet_buffer));

  if (this->_udp->read(packet_buffer, NTP_PACKET_SIZE) != NTP_PACKET_SIZE) {
    DBG("NTP err: Incorrect data size");
    return false;
  }

  #ifdef DEBUG_NTPClient
    Serial.print("NTP Data:");
    char s1[4];
    for (int i = 0; i < NTP_PACKET_SIZE; i++) {
      sprintf(s1, " %02X", packet_buffer[i]);
      Serial.print(s1);
    }
    Serial.println(".");
  #endif

  // ---- NTP Response Validation ----
  // The first byte of an NTP packet encodes three fields:
  //   Bits 7-6: LI  (Leap Indicator)   -- 11 = clock unsynchronized
  //   Bits 5-3: VN  (Version Number)    -- should be >= 4
  //   Bits 2-0: Mode                    -- 4 = server response

  // Reject if LI == 11 (server's clock is not synchronized)
  if ((packet_buffer[0] & 0b11000000) == 0b11000000) {
    #ifdef DEBUG_NTPClient
      Serial.println("err: NTP UnSync");
    #endif
    return false;
  }

  // Reject NTP versions older than v4 (current standard is v4, RFC 5905)
  if ((packet_buffer[0] & 0b00111000) >> 3 < 0b100) {
    #ifdef DEBUG_NTPClient
      Serial.println("err: Incorrect NTP Version");
    #endif
    return false;
  }

  // Reject if mode != 4 (server). Guards against receiving our own
  // request echoed back, or multicast/broadcast NTP packets.
  if ((packet_buffer[0] & 0b00000111) != 0b100) {
    #ifdef DEBUG_NTPClient
      Serial.println("err: NTP mode is not Server");
    #endif
    return false;
  }

  // Byte 1: Stratum. Valid range is 1 (primary/GPS) to 15 (max hops).
  // Stratum 0 is "kiss-of-death" (server telling us to go away),
  // and 16+ means unsynchronized.
  if ((packet_buffer[1] < 1) || (packet_buffer[1] > 15)) {
    #ifdef DEBUG_NTPClient
      Serial.println("err: Incorrect NTP Stratum");
    #endif
    return false;
  }

  // Bytes 16-23: Reference Timestamp -- when the server's clock was last
  // set. All zeros means the server has never been synchronized.
  if (packet_buffer[16] == 0 && packet_buffer[17] == 0 &&
      packet_buffer[18] == 0 && packet_buffer[19] == 0 &&
      packet_buffer[20] == 0 && packet_buffer[21] == 0 &&
      packet_buffer[22] == 0 && packet_buffer[23] == 0) {
    #ifdef DEBUG_NTPClient
      Serial.println("err: Incorrect NTP Ref Timestamp");
    #endif
    return false;
  }

  // ---- Extract Transmit Timestamp ----
  // Bytes 40-43 contain the transmit timestamp (seconds since 1900-01-01).
  // NTP uses big-endian (network byte order), so we manually reconstruct
  // the 32-bit value from two 16-bit words.
  unsigned long high_word = word(packet_buffer[40], packet_buffer[41]);
  unsigned long low_word = word(packet_buffer[42], packet_buffer[43]);
  unsigned long secs_since_1900 = high_word << 16 | low_word;

  // Convert from NTP epoch (1900) to Unix epoch (1970) by subtracting 70 years
  this->_current_epoch = secs_since_1900 - SEVENZYYEARS;

  return true;
}

// Non-blocking update: only queries NTP when the update interval has elapsed.
// Returns true if time is considered valid (either fresh or still within interval).
bool NTPClient::update() {
  if ((millis() - this->_last_update >= this->_update_interval)
      || this->_last_update == 0) {
    // Lazy initialization: start UDP if not already done
    if (!this->_udp_setup) this->begin();
    return this->force_update();
  }
  // Between updates, get_epoch_time() interpolates using millis()
  return true;
}

// ---- Time Accessors ----
// These interpolate between NTP updates by adding elapsed millis() since
// the last NTP response. This gives smooth second-level time without
// requiring constant server queries.

unsigned long NTPClient::get_epoch_time() const {
  return this->_time_offset +
         this->_current_epoch +
         ((millis() - this->_last_update) / 1000);
}

int NTPClient::get_day() const {
  // Unix epoch (Jan 1, 1970) was a Thursday (day 4).
  // Adding 4 and mod 7 maps to 0=Sunday convention.
  return (((this->get_epoch_time() / 86400L) + 4) % 7); // 0 is Sunday
}

int NTPClient::get_hours() const {
  return ((this->get_epoch_time() % 86400L) / 3600);
}

int NTPClient::get_minutes() const {
  return ((this->get_epoch_time() % 3600) / 60);
}

int NTPClient::get_seconds() const {
  return (this->get_epoch_time() % 60);
}

String NTPClient::get_formatted_time() const {
  unsigned long raw_time = this->get_epoch_time();
  char buf[9];
  snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu",
    (raw_time % 86400L) / 3600, (raw_time % 3600) / 60, raw_time % 60);
  return String(buf);
}

void NTPClient::end() {
  this->_udp->stop();
  this->_udp_setup = false;
}

void NTPClient::set_time_offset(int time_offset) {
  this->_time_offset = time_offset;
}

void NTPClient::set_update_interval(unsigned long update_interval) {
  this->_update_interval = update_interval;
}

void NTPClient::set_pool_server_name(const char* pool_server_name) {
  this->_pool_server_name = pool_server_name;
}

// ---- NTP Request Packet Construction ----
// Builds and sends a minimal NTP client request per RFC 5905.

bool NTPClient::send_ntp_packet() {
  byte packet_buffer[NTP_PACKET_SIZE];
  // Zero the buffer -- most NTP fields are unused in a client request
  memset(packet_buffer, 0, NTP_PACKET_SIZE);

  // Byte 0 encodes three fields:
  //   LI  = 11 (unknown/unsync -- normal for a client request)
  //   VN  = 100 (NTP version 4)
  //   Mode = 011 (client)
  //   Combined: 11 100 011 = 0xE3
  packet_buffer[0] = 0b11100011;   // LI, Version, Mode
  packet_buffer[1] = 0;            // Stratum (0 = unspecified for client)
  packet_buffer[2] = 6;            // Polling interval: 2^6 = 64 seconds
  packet_buffer[3] = 0xEC;         // Peer clock precision: 2^-20 ~= 1 microsecond

  // Bytes 4-11: Root Delay and Root Dispersion left as zero (client request)

  // Bytes 12-15: Reference Identifier -- "1N14" is a conventional marker
  // Some servers check this to identify the client implementation
  packet_buffer[12] = 49;   // '1'
  packet_buffer[13] = 0x4E; // 'N'
  packet_buffer[14] = 49;   // '1'
  packet_buffer[15] = 52;   // '4'

  // NTP servers always listen on UDP port 123
  bool return_value = this->_udp->beginPacket(this->_pool_server_name, 123);

  if (return_value) {
    return_value = (this->_udp->write(packet_buffer, NTP_PACKET_SIZE) == NTP_PACKET_SIZE);
    // endPacket() triggers the actual network send
    return_value = this->_udp->endPacket() && return_value;
  }
  return return_value;
}
