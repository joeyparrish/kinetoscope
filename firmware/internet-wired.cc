// Kinetoscope: A Sega Genesis Video Player
//
// Copyright (c) 2024 Joey Parrish
//
// See MIT License in LICENSE.txt

// Firmware that runs on the microcontroller inside the cartridge.
// The microcontroller accepts commands from the player in the Sega ROM, and
// can stream video from the Internet to the cartridge's shared banks of SRAM.

// This is the interface to a wired connection using an SPI-connected Ethernet
// board.

#include <Ethernet.h>
#include <HardwareSerial.h>
#include <SPI.h>
#include <utility/w5100.h>  // Inside Ethernet library

#include "internet.h"

EthernetClient client;

Client* internet_init_wired(const uint8_t* mac, unsigned int timeout_seconds) {
  SPI.begin();

  // Default SPI pins for RP2040:
  Ethernet.init(SS);  // GP17, SPI0_CSn
  // MOSI == GP19
  // MISO == GP16
  // SCK == 18

  // hardwareStatus() isn't valid until the W5100 library is initialized, which
  // normally happens during DHCP negotiation.  Since that has a long timeout,
  // go around the Ethernet library and initialize the chipset directly first.
  W5100.init();
  int hardware_status = Ethernet.hardwareStatus();

  if (hardware_status == EthernetW5100) {
    Serial.println("W5100 Ethernet controller detected.");
  } else if (hardware_status == EthernetW5200) {
    Serial.println("W5200 Ethernet controller detected.");
  } else if (hardware_status == EthernetW5500) {
    Serial.println("W5500 Ethernet controller detected.");
  } else {
    Serial.println("No Ethernet controller found.");
    return NULL;
  }

  unsigned long timeout_ms = timeout_seconds * 1000L;
  // It's really stupid that this library doesn't take a const input.
  int ok = Ethernet.begin(const_cast<uint8_t*>(mac), timeout_ms);

  Serial.print("DHCP: ");
  Serial.println(ok ? "success" : "failure");
  if (!ok) {
    return NULL;
  }

  IPAddress ip = Ethernet.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);
  return &client;
}
