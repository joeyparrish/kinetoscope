// Kinetoscope: A Sega Genesis Video Player
//
// Copyright (c) 2024 Joey Parrish
//
// See MIT License in LICENSE.txt

// Firmware that runs on the microcontroller inside the cartridge.
// The microcontroller accepts commands from the player in the Sega ROM, and
// can stream video from the Internet to the cartridge's shared banks of SRAM.

// This is the interface to HTTP requests.

// The Arduino HTTP Client library is a great general-purpose client library,
// but it reads and parses headers one byte at a time.  Here, we always do bulk
// reads, so we can read and parse headers faster and achieve a much higher
// throughput (more than 2x faster in my tests over a fast, wired connection).
// The down side to this HTTP client implementation is that the parsing is very
// hacky and limited.  For our purposes, though, we only care about the content
// length and no other header.  So it should be fine.

#include <Arduino.h>
#include <HardwareSerial.h>

#include "http.h"

#define DEFAULT_PORT 80

//#define MAX_READ 16384
//#define MAX_READ 8192
#define MAX_READ 4096

#define CONTENT_LENGTH_HEADER "Content-Length: "
#define CONTENT_LENGTH_HEADER_LENGTH 16
#define HTTP_RESPONSE_HEADER_LENGTH 9  // "HTTP/1.1 "
#define MIN_RESPONSE_LENGTH (HTTP_RESPONSE_HEADER_LENGTH + 3)

struct HeaderData {
  // Parsed from the response headers.
  int status_code;
  int body_length;

  // Some body bytes may have been read in the header buffer.
  char* body_start;
  int body_start_length;
};

static Client* client = NULL;
static char* current_server = NULL;
static int current_port = 0;
static char range_value[128];
static char request_buffer[1024];
static char response_buffer[1024];

void http_init(Client* network_client) {
   client = network_client;
}

// We will use persistent connections as much as possible to speed up requests.
static inline bool need_new_connection(const char* server, int port) {
  if (!current_server) {
    return true;
  }

  if (strcmp(server, current_server)) {
    return true;
  }

  if (port != current_port) {
    return true;
  }

  if (!client->connected()) {
    return true;
  }

  return false;
}

static void close_connection() {
  client->stop();

  if (current_server) {
    free(current_server);
    current_server = NULL;
  }
}

static inline void connect_if_needed(const char* server, int port) {
  if (!need_new_connection(server, port)) {
#ifdef DEBUG
    Serial.print("Reusing connection to ");
    Serial.println(server);
#endif
    return;
  }

#ifdef DEBUG
  Serial.print("Creating new connection to ");
  Serial.println(server);
#endif

  close_connection();

  client->connect(server, port);

  current_server = strdup(server);
  current_port = port;
}

static inline void write_request(const char* server, uint16_t port,
                                 const char* path, int start_byte, int size) {
  // Compute the Range header.
  snprintf(range_value, sizeof(range_value), "bytes=%d-%d",
           start_byte, start_byte + size - 1);

  int request_size = snprintf(
      request_buffer, sizeof(request_buffer),
      "GET %s HTTP/1.1\r\n"
      "Host: %s\r\n"
      "User-Agent: Kinetoscope/1.0\r\n"
      "Connection: keep-alive\r\n"
      "Range: %s\r\n"
      "\r\n",
      path, server, range_value);
  client->write((const uint8_t*)request_buffer, request_size);
}

static inline bool read_response_headers(HeaderData* header_data) {
  int num_read = -1;
  while (num_read <= 0 && client->connected()) {
    num_read = client->read((uint8_t*)response_buffer, sizeof(response_buffer));
  }

  if (num_read < MIN_RESPONSE_LENGTH) {
    Serial.println("Failed!  Did not find status code!");
    return false;
  }

  char status_code_buf[4];
  memcpy(status_code_buf, response_buffer + HTTP_RESPONSE_HEADER_LENGTH, 3);
  status_code_buf[3] = '\0';
  header_data->status_code = strtol(status_code_buf, NULL, 10);

  if (header_data->status_code <= 0 || header_data->status_code < 100) {
    Serial.print("Failed!  Invalid status code ");
    Serial.println(header_data->status_code);
    return false;
  }

  header_data->body_length = -1;
  header_data->body_start = NULL;
  header_data->body_start_length = -1;

  int this_header_starts = -1;
  int body_bytes_in_first_buffer = 0;

  // Scan through the buffer looking for the body length and the end of the
  // headers.
  for (int i = 0; i < num_read - 3; ++i) {
    if (response_buffer[i    ] == '\r' &&
        response_buffer[i + 1] == '\n' &&
        response_buffer[i + 2] == '\r' &&
        response_buffer[i + 3] == '\n') {
      // End of headers.  Save the location and length of the body bytes we
      // have in buffer, then quit the loop.
      header_data->body_start = response_buffer + i + 4;
      header_data->body_start_length = num_read - (i + 4);
      break;
    } else if (response_buffer[i] == '\r' &&
               response_buffer[i + 1] == '\n') {
      if (this_header_starts > 0) {
        // Parse one more header, looking for Content-Length.
        if (!strncasecmp(
            response_buffer + this_header_starts,
            CONTENT_LENGTH_HEADER,
            CONTENT_LENGTH_HEADER_LENGTH)) {
          const char* value_starts =
              response_buffer + this_header_starts +
              CONTENT_LENGTH_HEADER_LENGTH;
          header_data->body_length = strtol(value_starts, NULL, 10);
        }
      }

      this_header_starts = i + 2;
    }
  }

  if (header_data->body_start == NULL) {
    Serial.println("Failed!  Did not find end of headers!");
    return false;
  }

  if (header_data->body_length < 0) {
    Serial.println("Failed!  Did not find body length!");
    return false;
  }

  return true;
}

static inline bool check_status_code(int status_code) {
  // Since we sent a Range header, "200 OK" means the server ignored it.
  if (status_code == 200) {
    Serial.println("Failed!  Range request not supported?");
    return false;
  }

  // TODO: Test HTTP redirects
  if (status_code / 100 == 3) {
    Serial.println("Failed!  No redirect support!");
    return false;
  }

  // We should get an HTTP "206 Partial Content" status.  If not, we failed.
  if (status_code != 206) {
    Serial.print("Failed with status code ");
    Serial.print(status_code);
    Serial.println("!");
    return false;
  }

  return true;
}

int http_fetch(const char* server, uint16_t port, const char* path,
               int start_byte, uint8_t* data, int size) {
  if (!port) {
    port = DEFAULT_PORT;
  }
  connect_if_needed(server, port);

  write_request(server, port, path, start_byte, size);

  HeaderData header_data;
  if (!read_response_headers(&header_data)) {
    close_connection();
    return -1;
  }

#ifdef DEBUG
  Serial.print("HTTP status code: ");
  Serial.println(header_data.status_code);
#endif

  if (!check_status_code(header_data.status_code)) {
    close_connection();
    return -1;
  }

#ifdef DEBUG
  Serial.print("HTTP body length: ");
  Serial.println(header_data.body_length);
#endif

  if (header_data.body_length < 0) {
    Serial.println("Failed!  Unexpected zero-length response!");
    close_connection();
    return -1;
  }

  // Can't read more than the body length.  If it's smaller than the buffer,
  // limit ourselves to that.
  if (header_data.body_length < size) {
    size = header_data.body_length;
  }

  int bytes_read_total = 0;
  int bytes_left = size;

  // Copy the body bytes found in the header buffer.
  if (header_data.body_start_length) {
    int bytes = header_data.body_start_length < size ?
        header_data.body_start_length : size;
    memcpy(data, header_data.body_start, bytes);

    bytes_read_total += bytes;
    bytes_left -= bytes;
    data += bytes;
  }

  // Continue reading body data directly into the output buffer until we have
  // the whole response.
  while (bytes_left) {
    int read_request_size = bytes_left > MAX_READ ? MAX_READ : bytes_left;
    int bytes_read = client->read(data, read_request_size);

#if 0
    if (bytes_read > 0 && bytes_read < read_request_size) {
      Serial.print("Short read: ");
      Serial.println(bytes_read);
    }
#endif

    if (bytes_read <= 0) {
      delay(1);
      continue;
    }

    bytes_read_total += bytes_read;
    bytes_left -= bytes_read;
    data += bytes_read;
  }

  if (bytes_read_total != size) {
    Serial.print("Read ");
    Serial.print(bytes_read_total);
    Serial.print(" bytes instead of ");
    Serial.print(size);
    Serial.println("!");
    close_connection();
    return -1;
  }

  return bytes_read_total;
}
