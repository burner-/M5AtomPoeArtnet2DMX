#pragma once

#include <Arduino.h>

// Atomic PoE Base W5500 SPI pins for Atom Lite.
static constexpr uint8_t ETH_SCK_PIN = 22;
static constexpr uint8_t ETH_MISO_PIN = 23;
static constexpr uint8_t ETH_MOSI_PIN = 33;
static constexpr uint8_t ETH_CS_PIN = 19;

// M5Stack Port C UART pins on Atom Lite Grove connector.
static constexpr uint8_t DMX_TX_PIN = 26;
static constexpr uint8_t DMX_RX_PIN = 32;

// Atom Lite front button.
static constexpr uint8_t SETUP_BUTTON_PIN = 39;

static constexpr uint16_t HTTP_PORT = 80;
static constexpr uint16_t ARTNET_PORT = 6454;
static constexpr uint16_t MDNS_PORT = 5353;
