#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum class CdoManufacturer : uint8_t {
  AD,
  Mettler,
};

struct CdoBalanceDefinition {
  const char *id;
  CdoManufacturer manufacturer;
  const char *model;
  uint8_t desiredComPort;
};

static const CdoBalanceDefinition CDO_BALANCES[] = {
    {"CDO02", CdoManufacturer::AD, "A&D MC-30K", 2},
    {"CDO03", CdoManufacturer::AD, "A&D MC-6100", 3},
    {"CDO04", CdoManufacturer::Mettler, "Mettler XP504", 4},
    {"CDO05", CdoManufacturer::AD, "A&D BA-225", 5},
    {"CDO06", CdoManufacturer::Mettler, "Mettler XP56", 6},
};

static constexpr size_t CDO_BALANCE_COUNT =
    sizeof(CDO_BALANCES) / sizeof(CDO_BALANCES[0]);

inline char cdoUpperAscii(char value) {
  return value >= 'a' && value <= 'z' ? value - ('a' - 'A') : value;
}

inline bool cdoIdSeparator(char value) {
  return value == ' ' || value == '\t' || value == '"' || value == '\'' ||
         value == '-' || value == '_' || value == ':' || value == ',';
}

inline bool cdoIdBoundary(char value) {
  const char upper = cdoUpperAscii(value);
  return !((upper >= 'A' && upper <= 'Z') ||
           (value >= '0' && value <= '9'));
}

inline const CdoBalanceDefinition *findCdoBalance(const char *id) {
  if (id == nullptr) return nullptr;
  for (size_t index = 0; index < CDO_BALANCE_COUNT; ++index) {
    if (strcmp(CDO_BALANCES[index].id, id) == 0) {
      return &CDO_BALANCES[index];
    }
  }
  return nullptr;
}

inline bool cdoAdIdPrefix(const char *data, size_t numberStart) {
  if (data == nullptr || numberStart < 3) return false;
  size_t cursor = 0;
  while (cursor < numberStart &&
         (data[cursor] == ' ' || data[cursor] == '\t')) {
    ++cursor;
  }
  return cursor + 3 <= numberStart &&
      cdoUpperAscii(data[cursor]) == 'I' &&
      cdoUpperAscii(data[cursor + 1]) == 'D' &&
      data[cursor + 2] == ',';
}

inline bool cdoOnlyLineEnding(const char *data, size_t start, size_t length) {
  for (size_t index = start; index < length; ++index) {
    if (data[index] != '\r' && data[index] != '\n' &&
        data[index] != ' ' && data[index] != '\t') {
      return false;
    }
  }
  return true;
}

inline const CdoBalanceDefinition *extractCdoNumericId(
    const char *data,
    size_t length,
    char output[6]) {
  if (data == nullptr || output == nullptr || length < 6) return nullptr;

  // A&D ?ID replies are numeric. Their zero padding differs between models;
  // only the two final digits carry the CDO identity. Require the ID, prefix
  // (or an entirely numeric reply) so a streamed weighing value such as
  // "S 0.003 g" is never misidentified.
  for (size_t start = 0; start < length; ++start) {
    const bool prefixed = cdoAdIdPrefix(data, start);
    if (!prefixed && start != 0) continue;
    size_t digitCount = 0;
    while (start + digitCount < length &&
           data[start + digitCount] >= '0' &&
           data[start + digitCount] <= '9') {
      ++digitCount;
    }
    if (digitCount < 2 ||
        (!prefixed && !cdoOnlyLineEnding(data, digitCount, length)) ||
        (start + digitCount < length &&
         !cdoIdBoundary(data[start + digitCount]))) {
      continue;
    }

    output[0] = 'C';
    output[1] = 'D';
    output[2] = 'O';
    output[3] = data[start + digitCount - 2];
    output[4] = data[start + digitCount - 1];
    output[5] = '\0';
    const CdoBalanceDefinition *match = findCdoBalance(output);
    if (match != nullptr) return match;
  }
  return nullptr;
}

// Extract only an allow-listed CDO identity. Real replies can be a raw A&D ID
// ("CDO03") or an MT-SICS reply ("I10 A \"CDO04\"").
inline const CdoBalanceDefinition *extractCdoBalance(
    const char *data,
    size_t length,
    char output[6]) {
  if (data == nullptr || output == nullptr || length < 5) return nullptr;

  for (size_t start = 0; start + 4 < length; ++start) {
    if (cdoUpperAscii(data[start]) != 'C' ||
        cdoUpperAscii(data[start + 1]) != 'D' ||
        cdoUpperAscii(data[start + 2]) != 'O') {
      continue;
    }

    size_t cursor = start + 3;
    size_t skipped = 0;
    while (cursor < length && cdoIdSeparator(data[cursor]) && skipped < 8) {
      ++cursor;
      ++skipped;
    }
    if (cursor + 1 >= length ||
        data[cursor] < '0' || data[cursor] > '9' ||
        data[cursor + 1] < '0' || data[cursor + 1] > '9') {
      continue;
    }
    if (cursor + 2 < length && !cdoIdBoundary(data[cursor + 2])) continue;

    output[0] = 'C';
    output[1] = 'D';
    output[2] = 'O';
    output[3] = data[cursor];
    output[4] = data[cursor + 1];
    output[5] = '\0';

    const CdoBalanceDefinition *match = findCdoBalance(output);
    if (match != nullptr) return match;
  }
  const CdoBalanceDefinition *numeric = extractCdoNumericId(data, length, output);
  if (numeric != nullptr) return numeric;
  output[0] = '\0';
  return nullptr;
}
