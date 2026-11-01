#pragma once

#include <Arduino.h>
#include <LittleFS.h>

struct Reading {
  uint32_t timestamp;
  float temperature;
  float humidity;
  bool timestampEstimated;
};

// Compact binary representation: 16 bytes per reading.
#pragma pack(push, 1)

struct StoredReading {
  uint32_t timestamp;
  float temperature;
  float humidity;
  uint8_t flags;
  uint8_t reserved[3];
};

#pragma pack(pop)

struct QueueState {
  uint32_t magic;
  uint32_t headIndex;
  uint32_t lastTimestamp;
  uint8_t thinningLevel;
};

class Storage {
public:
  static constexpr uint32_t STATE_MAGIC = 0xAFF1A6E1;

  bool begin() {
    if (!LittleFS.begin()) {
      return false;
    }

    // Recover from an interrupted queue compaction.
    if (!LittleFS.exists("/queue.bin") &&
        LittleFS.exists("/queue.bak")) {
      LittleFS.rename("/queue.bak", "/queue.bin");
    }

    // If queue.bin exists, it is the completed replacement.
    if (LittleFS.exists("/queue.bak") &&
        LittleFS.exists("/queue.bin")) {
      LittleFS.remove("/queue.bak");
    }

    // An incomplete temporary file is never authoritative.
    if (LittleFS.exists("/queue.tmp")) {
      LittleFS.remove("/queue.tmp");
    }

    loadState();

    if (!LittleFS.exists("/queue.bin")) {
      File file = LittleFS.open("/queue.bin", "w");

      if (!file) {
        return false;
      }

      file.close();
    }

    // Recover the timestamp if power failed after appending a record
    // but before saving the state file.
    recoverLastTimestamp();

    return true;
  }

  uint32_t freeBytes() const {
    FSInfo info;

    if (!LittleFS.info(info)) {
      return 0;
    }

    return info.totalBytes - info.usedBytes;
  }

  uint32_t usedPercent() const {
    FSInfo info;

    if (!LittleFS.info(info) || info.totalBytes == 0) {
      return 0;
    }

    return (info.usedBytes * 100UL) / info.totalBytes;
  }

  uint32_t count() const {
    File file = LittleFS.open("/queue.bin", "r");

    if (!file) {
      return 0;
    }

    uint32_t totalRecords =
      file.size() / sizeof(StoredReading);

    file.close();

    if (totalRecords <= state.headIndex) {
      return 0;
    }

    return totalRecords - state.headIndex;
  }

  uint32_t lastTimestamp() const {
    return state.lastTimestamp;
  }

  uint8_t thinningLevel() const {
    return state.thinningLevel;
  }

  bool append(const Reading& reading) {
    StoredReading record = {};

    record.timestamp = reading.timestamp;
    record.temperature = reading.temperature;
    record.humidity = reading.humidity;
    record.flags = reading.timestampEstimated ? 1 : 0;

    File file = LittleFS.open("/queue.bin", "a");

    if (!file) {
      return false;
    }

    size_t written = file.write(
      reinterpret_cast<const uint8_t*>(&record),
      sizeof(record)
    );

    file.flush();
    file.close();

    if (written != sizeof(record)) {
      return false;
    }

    state.lastTimestamp = reading.timestamp;
    saveState();

    applyAdaptiveThinning();

    return true;
  }

  bool peek(Reading& reading) const {
    if (count() == 0) {
      return false;
    }

    File file = LittleFS.open("/queue.bin", "r");

    if (!file) {
      return false;
    }

    uint32_t offset =
      state.headIndex * sizeof(StoredReading);

    if (!file.seek(offset, SeekSet)) {
      file.close();
      return false;
    }

    StoredReading record;

    bool ok = file.read(
      reinterpret_cast<uint8_t*>(&record),
      sizeof(record)
    ) == sizeof(record);

    file.close();

    if (!ok) {
      return false;
    }

    reading.timestamp = record.timestamp;
    reading.temperature = record.temperature;
    reading.humidity = record.humidity;
    reading.timestampEstimated = (record.flags & 1) != 0;

    return true;
  }

  bool acknowledge(uint32_t timestamp) {
    Reading oldest;

    // Only acknowledge the exact oldest outstanding reading.
    if (!peek(oldest) || oldest.timestamp != timestamp) {
      return false;
    }

    state.headIndex++;
    saveState();

    File file = LittleFS.open("/queue.bin", "r");

    if (!file) {
      return false;
    }

    uint32_t totalRecords =
      file.size() / sizeof(StoredReading);

    file.close();

    // Reclaim space periodically rather than rewriting the queue
    // on every individual acknowledgement.
    if (state.headIndex >= 64 &&
        state.headIndex * 2 >= totalRecords) {
      return compact(false, state.thinningLevel);
    }

    return true;
  }

private:
  QueueState state = {
    STATE_MAGIC,
    0,
    0,
    0
  };

  void loadState() {
    if (!LittleFS.exists("/state.bin")) {
      saveState();
      return;
    }

    File file = LittleFS.open("/state.bin", "r");

    if (!file) {
      return;
    }

    QueueState candidate;

    bool valid = file.read(
      reinterpret_cast<uint8_t*>(&candidate),
      sizeof(candidate)
    ) == sizeof(candidate);

    file.close();

    if (valid && candidate.magic == STATE_MAGIC) {
      state = candidate;
    }
  }

  void saveState() {
    File file = LittleFS.open("/state.bin", "w");

    if (!file) {
      return;
    }

    file.write(
      reinterpret_cast<const uint8_t*>(&state),
      sizeof(state)
    );

    file.flush();
    file.close();
  }

  void recoverLastTimestamp() {
    File file = LittleFS.open("/queue.bin", "r");

    if (!file) {
      return;
    }

    uint32_t totalRecords =
      file.size() / sizeof(StoredReading);

    if (totalRecords > 0) {
      uint32_t offset =
        (totalRecords - 1) * sizeof(StoredReading);

      if (file.seek(offset, SeekSet)) {
        StoredReading record;

        if (file.read(
              reinterpret_cast<uint8_t*>(&record),
              sizeof(record)
            ) == sizeof(record)) {

          if (record.timestamp > state.lastTimestamp) {
            state.lastTimestamp = record.timestamp;
            saveState();
          }
        }
      }
    }

    file.close();
  }

  void applyAdaptiveThinning() {
    uint32_t percent = usedPercent();

    uint8_t targetLevel = state.thinningLevel;

    if (percent >= 80 && targetLevel < 3) {
      targetLevel = 3;
    } else if (percent >= 65 && targetLevel < 2) {
      targetLevel = 2;
    } else if (percent >= 50 && targetLevel < 1) {
      targetLevel = 1;
    }

    if (targetLevel > state.thinningLevel) {
      Serial.printf(
        "Storage at %lu%%. Thinning queue to level %u\n",
        static_cast<unsigned long>(percent),
        targetLevel
      );

      compact(true, targetLevel);
    }
  }

  bool compact(bool thin, uint8_t newLevel) {
    File source = LittleFS.open("/queue.bin", "r");

    if (!source) {
      return false;
    }

    File target = LittleFS.open("/queue.tmp", "w");

    if (!target) {
      source.close();
      return false;
    }

    uint32_t totalRecords =
      source.size() / sizeof(StoredReading);

    if (!source.seek(
        state.headIndex * sizeof(StoredReading),
        SeekSet
        )) {
        target.close();
        source.close();
        LittleFS.remove("/queue.tmp");
        return false;
    }

    uint32_t stride = thin
      ? (1UL << newLevel)
      : 1UL;

    uint32_t activeIndex = 0;
    StoredReading record;
    bool ok = true;

    for (uint32_t i = state.headIndex;
         i < totalRecords;
         i++) {

      if (source.read(
            reinterpret_cast<uint8_t*>(&record),
            sizeof(record)
          ) != sizeof(record)) {
        ok = false;
        break;
      }

      bool isNewest = (i == totalRecords - 1);

      bool keep =
        !thin ||
        activeIndex == 0 ||
        (activeIndex % stride == 0) ||
        isNewest;

      if (keep) {
        if (target.write(
              reinterpret_cast<const uint8_t*>(&record),
              sizeof(record)
            ) != sizeof(record)) {
          ok = false;
          break;
        }
      }

      activeIndex++;
    }

    target.flush();
    target.close();
    source.close();

    if (!ok) {
      LittleFS.remove("/queue.tmp");
      Serial.println(F("ERROR: Queue compaction failed"));
      return false;
    }

    // Keep the original until the replacement is complete.
    LittleFS.remove("/queue.bak");

    if (!LittleFS.rename("/queue.bin", "/queue.bak")) {
      LittleFS.remove("/queue.tmp");
      return false;
    }

    if (!LittleFS.rename("/queue.tmp", "/queue.bin")) {
      LittleFS.rename("/queue.bak", "/queue.bin");
      return false;
    }

    LittleFS.remove("/queue.bak");

    state.headIndex = 0;

    if (thin && newLevel > state.thinningLevel) {
      state.thinningLevel = newLevel;
    }

    saveState();

    Serial.printf(
      "Queue compacted. %lu readings remain.\n",
      static_cast<unsigned long>(count())
    );

    return true;
  }
};