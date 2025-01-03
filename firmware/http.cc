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

#include "error.h"
#include "http.h"
#include "string-util.h"

#define DEFAULT_PORT 80

#define MAX_READ 8192
#define MAX_SERVER 256

#define CONTENT_LENGTH_HEADER "Content-Length: "
#define CONTENT_LENGTH_HEADER_LENGTH 16
#define HTTP_RESPONSE_HEADER_LENGTH 9  // "HTTP/1.1 "
#define MIN_RESPONSE_LENGTH (HTTP_RESPONSE_HEADER_LENGTH + 3)

struct HeaderData {
  // Parsed from the response headers.
  int status_code;
  int body_length;

  // Some body bytes may have been read in the header buffer.
  const uint8_t* body_start;
  int body_start_length;
};

static Client* client = NULL;
static char current_server[MAX_SERVER];
static int current_port = 0;
static char range_value[128];
static char request_buffer[1024];
static char response_buffer[1024];
static uint8_t read_buffer[MAX_READ];

void http_init(Client* network_client) {
  client = network_client;
  current_server[0] = '\0';
}

// We will use persistent connections as much as possible to speed up requests.
static inline bool need_new_connection(const char* server, int port) {
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
  current_server[0] = '\0';
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

  copy_string(current_server, server, MAX_SERVER);
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

#ifdef DEBUG
  Serial.println(request_buffer);
#endif

  client->write((const uint8_t*)request_buffer, request_size);
}

static inline bool read_response_headers(HeaderData* header_data) {
  int num_read = 0;
  bool end_of_headers = false;
  while (client->connected() && !end_of_headers) {
    int last_read = client->read(
        (uint8_t*)response_buffer + num_read,
        sizeof(response_buffer) - num_read);
    if (last_read >= 0) {
      num_read += last_read;
    }
    response_buffer[num_read] = '\0';

    // NOTE: Although compliant servers are supposed to send \r\n as a line
    // terminator, compliant clients may ignore the \r and accept \n only.
    // Android's com.phlox.simpleserver, which I used briefly during testing,
    // only replies with \n, so we take care here to tolerate that.
    end_of_headers = strstr(response_buffer, "\r\n\r\n") != NULL ||
                     strstr(response_buffer, "\n\n") != NULL;
  }

  if (num_read < MIN_RESPONSE_LENGTH) {
    Serial.println("Failed!  Did not find status code!");
    return false;
  }

  if (!end_of_headers) {
    Serial.println("Failed!  Did not find end of headers!");
    return false;
  }

#ifdef DEBUG
  Serial.println(response_buffer);
#endif

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
  // headers.  See also NOTE above about line endings.
  for (int i = 0; i < num_read - 1; ++i) {
    if (response_buffer[i] == '\n') {
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

      this_header_starts = i + 1;
    }

    if (i < num_read - 3 &&
        response_buffer[i    ] == '\r' &&
        response_buffer[i + 1] == '\n' &&
        response_buffer[i + 2] == '\r' &&
        response_buffer[i + 3] == '\n') {
      // End of headers (compliant, DOS style line termination).
      // Save the location and length of the body bytes we have in buffer, then
      // quit the loop.
      header_data->body_start = (const uint8_t*)response_buffer + i + 4;
      header_data->body_start_length = num_read - (i + 4);
      break;
    } else if (response_buffer[i    ] == '\n' &&
               response_buffer[i + 1] == '\n') {
      // End of headers.  Save the location and length of the body bytes we
      // have in buffer, then quit the loop.
      header_data->body_start = (const uint8_t*)response_buffer + i + 2;
      header_data->body_start_length = num_read - (i + 2);
      break;
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
    report_error("Request failed! Range not supported?");
    return false;
  }

  // TODO: Support HTTP redirects?
  if (status_code / 100 == 3) {
    report_error("Request failed! Redirect not supported!");
    return false;
  }

  // We should get an HTTP "206 Partial Content" status.  If not, we failed.
  if (status_code != 206) {
    char buf[64];
    snprintf(buf, 64, "Request failed! HTTP status %d", status_code);
    report_error(buf);
    return false;
  }

  return true;
}

bool http_fetch(const char* server, uint16_t port, const char* path,
                int start_byte, int size, http_data_callback callback) {
  if (!client) {
    report_error("No internet connection!");
    return false;
  }

  if (!port) {
    port = DEFAULT_PORT;
  }
  connect_if_needed(server, port);

  write_request(server, port, path, start_byte, size);

  HeaderData header_data;
  if (!read_response_headers(&header_data)) {
    report_error("Failed to read HTTP headers!");
    close_connection();
    return false;
  }

#ifdef DEBUG
  Serial.print("HTTP status code: ");
  Serial.println(header_data.status_code);
#endif

  // Calls report_error() on failure
  if (!check_status_code(header_data.status_code)) {
    close_connection();
    return false;
  }

#ifdef DEBUG
  Serial.print("HTTP body length: ");
  Serial.println(header_data.body_length);
#endif

  if (header_data.body_length < 0) {
    report_error("Unexpected zero-length response!");
    close_connection();
    return false;
  }

  // Can't read more than the body length.  If it's smaller than the buffer,
  // limit ourselves to that.
  if (header_data.body_length < size) {
    size = header_data.body_length;
  }

  int bytes_left = size;

  // Copy the body bytes found in the header buffer.
  if (header_data.body_start_length) {
    bool ok = callback(header_data.body_start, header_data.body_start_length);
    if (!ok) {
      Serial.println("Transfer interrupted.");
      close_connection();
      return false;
    }
    bytes_left -= header_data.body_start_length;
  }

  // Continue reading body data directly into the output buffer until we have
  // the whole response.
  while (bytes_left) {
    int read_request_size = bytes_left > MAX_READ ? MAX_READ : bytes_left;
    int bytes_read = client->read(read_buffer, read_request_size);

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

    bool ok = callback(read_buffer, bytes_read);
    if (!ok) {
      Serial.println("Transfer interrupted.");
      close_connection();
      return false;
    }
    bytes_left -= bytes_read;
  }

  return true;
}
