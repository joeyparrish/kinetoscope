// Kinetoscope: A Sega Genesis Video Player
//
// Copyright (c) 2024 Joey Parrish
//
// See MIT License in LICENSE.txt

// Firmware that runs on the microcontroller inside the cartridge.
// The microcontroller accepts commands from the player in the Sega ROM, and
// can stream video from the Internet to the cartridge's shared banks of SRAM.

// This is the interface to HTTP(S) requests.

#include <ArduinoHttpClient.h>
#include <HardwareSerial.h>

#define DEFAULT_PORT 80

#include "https.h"

static Client* client = NULL;

static char* current_server = NULL;
static int current_port = 0;
static HttpClient* https_client = NULL;

void https_init(Client* network_client) {
   client = network_client;
}

// We will use persistent connections as much as possible to speed up requests.
static bool need_new_https_client(const char* server, int port) {
  if (!https_client) {
    return true;
  }

  if (strcmp(server, current_server)) {
    return true;
  }

  if (port != current_port) {
    return true;
  }

  return false;
}

static void create_https_client(const char* server, int port) {
  if (!need_new_https_client(server, port)) {
#ifdef DEBUG
    Serial.print("Reusing HTTP client to connect to ");
    Serial.println(server);
#endif
    return;
  }

  if (https_client) {
    delete https_client;
    free(current_server);
  }

#ifdef DEBUG
  Serial.print("Creating new HTTP client to connect to ");
  Serial.println(server);
#endif

  https_client = new HttpClient(*client, server, port);
  https_client->connectionKeepAlive();
  current_server = strdup(server);
  current_port = port;
}

int https_fetch(const char* server, uint16_t port, const char* path,
                int start_byte, uint8_t* data, int size) {
  if (!port) {
    port = DEFAULT_PORT;
  }
  create_https_client(server, port);

  // Tell the library that we will send some custom headers, and it should wait
  // for us to finish those before completing the request.
  https_client->beginRequest();

  // Send a basic GET request.
  if (https_client->get(path) != 0) {
    Serial.println("Failed to connect!");
    https_client->stop();
    return -1;
  }

  // Add a Range header.
  char rangeValue[128];
  snprintf(rangeValue, sizeof(rangeValue), "bytes=%d-%d",
           start_byte, start_byte + size - 1);
  https_client->sendHeader("Range", rangeValue);

  // Tell the library that we're done sending headers.
  https_client->endRequest();

  // Check the HTTP status code.
  int status_code = https_client->responseStatusCode();
#ifdef DEBUG
  Serial.print("HTTP status code: ");
  Serial.println(status_code);
#endif

  // Since we sent a Range header, "200 OK" means the server ignored it.
  if (status_code == 200) {
    Serial.println("Failed!  Range request not supported?");
    https_client->stop();
    return -1;
  }

  // FIXME: Test HTTP redirects
  if (status_code / 100 == 3) {
    Serial.println("Failed!  No redirect support!");
    https_client->stop();
    return -1;
  }

  // We should get an HTTP "206 Partial Content" status.  If not, we failed.
  if (status_code != 206) {
    Serial.print("Failed with status code ");
    Serial.print(status_code);
    Serial.println("!");
    https_client->stop();
    return -1;
  }

  // Skip the response headers.
  if (https_client->skipResponseHeaders() != 0) {
    Serial.println("Failed to read headers!");
    https_client->stop();
    return -1;
  }

  int body_length = https_client->contentLength();
#ifdef DEBUG
  Serial.print("HTTP body length: ");
  Serial.println(body_length);
#endif

  if (body_length < size) {
    size = body_length;
  }

  int bytes_read_total = 0;
  int bytes_left = size;
  while (bytes_left) {
    int bytes_read = https_client->read(data, bytes_left);
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
    https_client->stop();
    return -1;
  }

  https_client->stop();
  return bytes_read_total;
}