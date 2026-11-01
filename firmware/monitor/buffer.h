#pragma once

#include <Arduino.h>
#include <time.h>
#include "storage.h"

class ReadingBuffer {
public:
  explicit ReadingBuffer(Storage& storageRef)
    : storage(storageRef) {}

  void begin() {
    lastTimestampValue = storage.lastTimestamp();
  }

  bool enqueue(const Reading& reading) {
    return storage.append(reading);
  }

  bool oldest(Reading& reading) const {
    return storage.peek(reading);
  }

  bool acknowledge(uint32_t timestamp) {
    return storage.acknowledge(timestamp);
  }

  uint32_t queued() const {
    return storage.count();
  }

  uint32_t samplingIntervalMs() const {
    switch (storage.thinningLevel()) {
      case 1:
        return 60000UL;   // 1 minute
      case 2:
        return 120000UL;  // 2 minutes
      case 3:
        return 300000UL;  // 5 minutes
      default:
        return 30000UL;   // 30 seconds
    }
  }

  uint32_t nextTimestamp(
    uint32_t ntpTimestamp,
    bool& estimated,
    uint32_t intervalSeconds
  ) {
    uint32_t candidate = ntpTimestamp;

    estimated = false;

    if (candidate == 0 || candidate <= lastTimestampValue) {
      estimated = true;

      if (lastTimestampValue > 0) {
        candidate = lastTimestampValue + intervalSeconds;
      } else {
        // Fallback if the device has never synchronised its clock
        // and has no previously stored timestamp.
        candidate = 1793539200UL;
      }
    }

    if (candidate <= lastTimestampValue) {
      candidate = lastTimestampValue + 1;
      estimated = true;
    }

    lastTimestampValue = candidate;

    return candidate;
  }

private:
  Storage& storage;
  uint32_t lastTimestampValue = 0;
};