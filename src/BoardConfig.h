// Copyright (c) multi-boot-ereader contributors. MIT licensed.
#pragma once

// Hardware description for the Xteink X4 (ESP32-C3). The pin assignments match
// the open-x4 community SDK; see docs/architecture.md.

// E-paper panel (SSD1677, 800x480).
constexpr int8_t kEpdSclk = 8;
constexpr int8_t kEpdMosi = 10;
constexpr int8_t kEpdCs = 21;
constexpr int8_t kEpdDc = 4;
constexpr int8_t kEpdRst = 5;
constexpr int8_t kEpdBusy = 6;

// Label of the data partition holding the slot metadata blob.
constexpr const char* kMetadataPartitionLabel = "mbmeta";
// Custom data partition subtype used for that partition.
constexpr uint8_t kMetadataPartitionSubtype = 0x40;

// How long a button has to be held to count as a long press.
constexpr uint32_t kLongPressMillis = 800;
