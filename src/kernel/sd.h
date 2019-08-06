#ifndef SD_H
#define SD_H

#include <stdbool.h>
#include <stdint.h>

#define SdBlockSize 512

typedef enum {
	SdInitResultOk,
	SdInitResultSpiLockFail, // could not grab SPI bus lock
	SdInitResultCouldNotReset, // software reset command failed (CMD0)
	SdInitResultBadCard, // illegal response etc.
	SdInitResultUnsupportedCard, // potentially a valid card, but currently unsupported
} SdInitResult;

typedef enum {
	SdTypeBadCard,
	SdTypeSdVer2Plus,
} SdType;

typedef enum {
	SdAddressModeByte,
	SdAddressModeBlock,
} SdAddressMode;

typedef struct {
	SdType type;
	uint8_t powerPin;
	uint8_t slaveSelectPin;
	SdAddressMode addressMode;
} SdCard;

SdInitResult sdInit(SdCard *card, uint8_t powerPin, uint8_t slaveSelectPin);
void sdQuit(SdCard *card);

bool sdReadBlock(SdCard *card, uint32_t block, uint8_t *data); // SdBlockSize bytes stored into data
bool sdWriteBlock(SdCard *card, uint32_t block, const uint8_t *data);

#endif
