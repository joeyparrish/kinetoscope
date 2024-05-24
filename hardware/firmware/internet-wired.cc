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

#include "internet.h"

EthernetClient client;

Client* internet_init_wired(uint8_t* mac) {
  SPI.begin();

  // Default SPI pins for RP2040:
  Ethernet.init(SS);  // GP17, SPI0_CSn
  // MOSI == GP19
  // MISO == GP16
  // SCK == 18

  // FIXME: Handle failures here
  int ok = Ethernet.begin(mac);
  Serial.print("DHCP: ");
  Serial.println(ok ? "success" : "failure");

  if (Ethernet.hardwareStatus() == EthernetNoHardware) {
    Serial.println("Ethernet shield was not found.");
  } else if (Ethernet.hardwareStatus() == EthernetW5100) {
    Serial.println("W5100 Ethernet controller detected.");
  } else if (Ethernet.hardwareStatus() == EthernetW5200) {
    Serial.println("W5200 Ethernet controller detected.");
  } else if (Ethernet.hardwareStatus() == EthernetW5500) {
    Serial.println("W5500 Ethernet controller detected.");
  }

  IPAddress ip = Ethernet.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);

  return &client;
}
