#ifndef SD_H
#define SD_H

#include <stdbool.h>
#include <stdint.h>

#define SdBlockSizeBits 9
#define SdBlockSize (1<<SdBlockSizeBits) // =512

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
#define SdTypeBits 1

typedef enum {
	SdAddressModeByte,
	SdAddressModeBlock,
} SdAddressMode;
#define SdAddressModeBits 1

STATICASSERT(SdTypeBits+SdAddressModeBits<=8);
typedef struct {
	uint32_t blockCount; // Card size is blockCount*SdBlockSize, allowing up to 2TB. However we only support 4gb due to 32 bit addressing (see hwDeviceSdCardReaderMount).
	uint8_t type:SdTypeBits;
	uint8_t addressMode:SdAddressModeBits;
	uint8_t reserved:(8-SdTypeBits-SdAddressModeBits);
	uint8_t powerPin;
	uint8_t slaveSelectPin;
} SdCard;

SdInitResult sdInit(SdCard *card, uint8_t powerPin, uint8_t slaveSelectPin);
void sdQuit(SdCard *card);

bool sdReadBlock(SdCard *card, uint32_t block, uint8_t *data); // SdBlockSize bytes stored into data. Note that on failure the passed data array may have been clobbered and cannot be trusted.
bool sdWriteBlock(SdCard *card, uint32_t block, const uint8_t *data);

#endif
