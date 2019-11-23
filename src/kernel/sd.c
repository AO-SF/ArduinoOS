#include <assert.h>
#include <inttypes.h>
#include <stdarg.h>

#include "kernel.h"
#include "ktime.h"
#include "log.h"
#include "sd.h"
#include "spi.h"
#include "util.h"

typedef enum {
	SdCsdVersion1,
	SdCsdVersion2,
	SdCsdVersionNB,
} SdCsdVersion;

////////////////////////////////////////////////////////////////////////////////
// Private prototypes
////////////////////////////////////////////////////////////////////////////////

void sdWriteCommand(uint8_t firstByte, ...); // terminate with 0xFF, which is not sent

void sdWriteDummyBytes(void); // continues until 0xFF is read
void sdWriteDummyBytesN(unsigned count);

uint8_t sdWaitForResponse(unsigned max); // Waits until we receive something other than 0xFF, and returns what was read. If not found after max reads, stops and returns 0xFF.

////////////////////////////////////////////////////////////////////////////////
// Public functions
////////////////////////////////////////////////////////////////////////////////

SdInitResult sdInit(SdCard *card, uint8_t powerPin, uint8_t slaveSelectPin) {
	uint8_t responseByte;
	SdInitResult result=SdInitResultOk;

	// Setup card fields
	card->blockCount=0;
	card->type=SdTypeBadCard;
	card->powerPin=powerPin;
	card->slaveSelectPin=slaveSelectPin;
	card->addressMode=SdAddressModeByte;

	// Attempt to grab SPI bus lock
	if (!kernelSpiGrabLockNoSlaveSelect()) {
		kernelLog(LogTypeWarning, kstrP("sdInit failed: could not grab SPI bus lock (powerPin=%u, slaveSelectPin=%u)\n"), powerPin, slaveSelectPin);
		return SdInitResultSpiLockFail;
	}

	// Ensure MOSI is high initially
	spiWriteByte(0xFF);

	// Turn on power pin
	pinWrite(powerPin, true);

	// Delay for at least 1ms
	ktimeDelayMs(10);

	// We are required to send at least 74 dummy bytes, but do 200 to be safe.
	sdWriteDummyBytesN(200);

	// Enable the slave by setting pin low
	pinWrite(slaveSelectPin, false);

	// Send CMD0 - software reset
	sdWriteCommand(0x40, 0x00, 0x00, 0x00, 0x00, 0x95, 0xFF);
	responseByte=sdWaitForResponse(16);
	if (responseByte!=0x01) {
		kernelLog(LogTypeWarning, kstrP("sdInit failed: CMD0 (software reset) bad response %u (powerPin=%u, slaveSelectPin=%u)\n"), responseByte, powerPin, slaveSelectPin);
		result=SdInitResultCouldNotReset;
		goto error;
	}
	sdWriteDummyBytes();

	// Send CMD8 - check voltage range (SDC v2 only)
	sdWriteCommand(0x48, 0x00, 0x00, 0x01, 0xAA, 0x87, 0xFF);
	responseByte=sdWaitForResponse(16);
	if (responseByte!=0x01) {
		sdWriteDummyBytes();

		// Unhandled case
		// Should follow up by sending CMD55+ACMD41 (with args 0x00000000 for latter) repeatedly,
		// until we get either 0x00 indicating SD version 1, or something other than 0x01.
		// In the latter case try sending CMD1, looking for 0x00, retrying on 0x01, and giving up otherwise.

		kernelLog(LogTypeWarning, kstrP("sdInit failed: CMD8 (check voltage range) unhandled response %u (powerPin=%u, slaveSelectPin=%u)\n"), responseByte, powerPin, slaveSelectPin);
		result=SdInitResultUnsupportedCard;
		goto error;
	}

	// Read 4 byte response for CMD8
	uint8_t responseR7[4];
	responseR7[0]=sdWaitForResponse(16);
	responseR7[1]=sdWaitForResponse(16);
	responseR7[2]=sdWaitForResponse(16);
	responseR7[3]=sdWaitForResponse(16);
	sdWriteDummyBytes();

	// Check if argument was passed back properly.
	if (responseR7[0]!=0x00 || responseR7[1]!=0x00 || responseR7[2]!=0x01 || responseR7[3]!=0xAA) {
		// Bad card
		kernelLog(LogTypeWarning, kstrP("sdInit failed: CMD8 (check voltage range) bad echo response 0x%02X%02X%02X%02X (powerPin=%u, slaveSelectPin=%u)\n"), responseR7[0], responseR7[1], responseR7[2], responseR7[3], powerPin, slaveSelectPin);
		result=SdInitResultBadCard;
		goto error;
	}

	// CMD55+ACMD41 initialise attempt loop
	unsigned i;
	for(i=0; i<1024; ++i) {
		// Send CMD55 - ACMD command prefix
		sdWriteCommand(0x77, 0x00, 0x00, 0x00, 0x00, 0x65, 0xFF);
		responseByte=sdWaitForResponse(16);
		sdWriteDummyBytes();
		if (responseByte==0x05) {
			// Old card - unsupported, use CMD1 similar to case above.
			kernelLog(LogTypeWarning, kstrP("sdInit failed: CMD55 (ACMD prefix) unhandled response %u (powerPin=%u, slaveSelectPin=%u)\n"), responseByte, powerPin, slaveSelectPin);
			result=SdInitResultUnsupportedCard;
			goto error;
		} else if (responseByte!=0x01) {
			// Bad response
			kernelLog(LogTypeWarning, kstrP("sdInit failed: CMD55 (ACMD prefix) bad response %u (powerPin=%u, slaveSelectPin=%u)\n"), responseByte, powerPin, slaveSelectPin);
			result=SdInitResultBadCard;
			goto error;
		}

		// Send ACMD41 - SDC initialisation
		sdWriteCommand(0x69, 0x40, 0x00, 0x00, 0x00, 0x77, 0xFF);
		responseByte=sdWaitForResponse(16);
		sdWriteDummyBytes();

		if (responseByte==0x01)
			continue; // give the SD card more time and try again
		else if (responseByte==0x00)
			break; // success
		else {
			// Bad card
			kernelLog(LogTypeWarning, kstrP("sdInit failed: ACMD41 (SDC init) bad response %u (powerPin=%u, slaveSelectPin=%u)\n"), responseByte, powerPin, slaveSelectPin);
			result=SdInitResultBadCard;
			goto error;
		}
	}

	if (i==1024) {
		// Timeout - bad card
		kernelLog(LogTypeWarning, kstrP("sdInit failed: CMD55+ACMD41 (SDC init) timeout (powerPin=%u, slaveSelectPin=%u)\n"), responseByte, powerPin, slaveSelectPin);
		result=SdInitResultBadCard;
		goto error;
	}

	// We now know card's type.
	card->type=SdTypeSdVer2Plus;

	// Send CMD58 to get card voltage and whether it is extended or not
	sdWriteCommand(0x7A, 0x00, 0x00, 0x00, 0x00, 0x01, 0xFF);
	responseByte=sdWaitForResponse(16);
	if (responseByte!=0x00) {
		// All cards should support this
		sdWriteDummyBytes();
		kernelLog(LogTypeWarning, kstrP("sdInit failed: CMD58 (read OCR) bad response %u (powerPin=%u, slaveSelectPin=%u)\n"), responseByte, powerPin, slaveSelectPin);
		result=SdInitResultBadCard;
		goto error;
	}

	// Read 4 byte response for CMD58
	uint8_t responseR3[4];
	responseR3[0]=sdWaitForResponse(16);
	responseR3[1]=sdWaitForResponse(16);
	responseR3[2]=sdWaitForResponse(16);
	responseR3[3]=sdWaitForResponse(16);
	sdWriteDummyBytes();

	if (responseR3[0]&0x40)
		card->addressMode=SdAddressModeBlock;
	else
		card->addressMode=SdAddressModeByte;

	// If in byte mode, want to send CMD16 to set block size to 512 to match FAT file system.
	// (block mode devices are fixed at 512 anyway)
	assert(SdBlockSize==512);
	if (card->addressMode==SdAddressModeByte) {
		sdWriteCommand(0x50, 0x00, 0x00, 0x02, 0x00, 0x01, 0xFF);
		responseByte=sdWaitForResponse(16);
		sdWriteDummyBytes();
		if (responseByte!=0x00) {
			// All cards should support this
			kernelLog(LogTypeWarning, kstrP("sdInit failed: CMD16 (set block size) bad response %u (powerPin=%u, slaveSelectPin=%u)\n"), responseByte, powerPin, slaveSelectPin);
			result=SdInitResultBadCard;
			goto error;
		}
	}

	// Send CMD9 - read CSD register
	sdWriteCommand(0x49, 0x00, 0x00, 0x00, 0x00, 0x01, 0xFF);
	responseByte=sdWaitForResponse(16);
	sdWriteDummyBytes();
	if (responseByte!=0x00) {
		// All cards should support this
		kernelLog(LogTypeWarning, kstrP("sdInit failed: CMD9 (read CSD) bad response %u (powerPin=%u, slaveSelectPin=%u)\n"), responseByte, powerPin, slaveSelectPin);
		result=SdInitResultBadCard;
		goto error;
	}

	responseByte=sdWaitForResponse(1024); // read data token
	if (responseByte!=0xFE) {
		kernelLog(LogTypeWarning, kstrP("sdInit failed: CMD9 (read CSD) bad data token %u (powerPin=%u, slaveSelectPin=%u)\n"), responseByte, powerPin, slaveSelectPin);
		result=SdInitResultBadCard;
		goto error;
	}

	responseByte=spiReadByte(); // read data byte  0 [120:127]
	SdCsdVersion csdVersion=(responseByte>>6);
	switch(csdVersion) {
		case SdCsdVersion1: {
			responseByte=spiReadByte(); // read data byte  1 [112:119]
			responseByte=spiReadByte(); // read data byte  2 [104:111]
			responseByte=spiReadByte(); // read data byte  3 [ 96:103]
			responseByte=spiReadByte(); // read data byte  4 [ 88: 95]
			responseByte=spiReadByte(); // read data byte  5 [ 80: 87]
			uint8_t readBlLen=(responseByte&0x0F);
			if (readBlLen<SdBlockSizeBits-2) {
				kernelLog(LogTypeWarning, kstrP("sdInit failed: CSD 1.0 unsupported READ_BL_LEN value %u (powerPin=%u, slaveSelectPin=%u)\n"), readBlLen, powerPin, slaveSelectPin);
				result=SdInitResultBadCard;
				goto error;
			}
			responseByte=spiReadByte(); // read data byte  6 [ 72: 79]
			uint16_t cSize=(((uint16_t)(responseByte&3))<<10);
			responseByte=spiReadByte(); // read data byte  7 [ 64: 71]
			cSize|=((uint16_t)responseByte)<<2;
			responseByte=spiReadByte(); // read data byte  8 [ 56: 63]
			cSize|=(responseByte>>6);
			responseByte=spiReadByte(); // read data byte  9 [ 48: 55]
			uint8_t cSizeMult=(responseByte&3)<<1;
			responseByte=spiReadByte(); // read data byte 10 [ 40: 47]
			cSizeMult|=(responseByte>>7);
			responseByte=spiReadByte(); // read data byte 11 [ 32: 39]
			responseByte=spiReadByte(); // read data byte 12 [ 24: 31]
			responseByte=spiReadByte(); // read data byte 13 [ 16: 23]
			responseByte=spiReadByte(); // read data byte 14 [  8: 15]
			responseByte=spiReadByte(); // read data byte 15 [  0:  7]

			card->blockCount=(cSize+1)*(((uint32_t)1)<<(cSizeMult+2+readBlLen-SdBlockSizeBits)); // note: cannot overflow, max is 2gb, and also shift is non-negative as readBlLen>=SdBlockSizeBits-2 per check above
		} break;
		case SdCsdVersion2: {
			responseByte=spiReadByte(); // read data byte  1 [112:119]
			responseByte=spiReadByte(); // read data byte  2 [104:111]
			responseByte=spiReadByte(); // read data byte  3 [ 96:103]
			responseByte=spiReadByte(); // read data byte  4 [ 88: 95]
			responseByte=spiReadByte(); // read data byte  5 [ 80: 87]
			uint8_t readBlLen=(responseByte&0x0F);
			if (readBlLen!=SdBlockSizeBits) {
				kernelLog(LogTypeWarning, kstrP("sdInit failed: CSD 2.0 READ_BL_LEN value %u (powerPin=%u, slaveSelectPin=%u)\n"), readBlLen, powerPin, slaveSelectPin);
				result=SdInitResultBadCard;
				goto error;
			}
			responseByte=spiReadByte(); // read data byte  6 [ 72: 79]
			responseByte=spiReadByte(); // read data byte  7 [ 64: 71]
			uint32_t csdVersion2CSize=0;
			csdVersion2CSize|=(((uint32_t)(responseByte&0x3F))<<16);
			responseByte=spiReadByte(); // read data byte  8 [ 56: 63]
			csdVersion2CSize|=(((uint32_t)responseByte)<<8);
			responseByte=spiReadByte(); // read data byte  9 [ 48: 55]
			csdVersion2CSize|=(((uint32_t)responseByte)<<0);
			responseByte=spiReadByte(); // read data byte 10 [ 40: 47]
			responseByte=spiReadByte(); // read data byte 11 [ 32: 39]
			responseByte=spiReadByte(); // read data byte 12 [ 24: 31]
			responseByte=spiReadByte(); // read data byte 13 [ 16: 23]
			responseByte=spiReadByte(); // read data byte 14 [  8: 15]
			responseByte=spiReadByte(); // read data byte 15 [  0:  7]

			uint32_t csdVersion2CSizeMax=(((uint32_t)1u)<<(32-10))-1;
			if (csdVersion2CSize>=csdVersion2CSizeMax) {
				kernelLog(LogTypeWarning, kstrP("sdInit: CSD 2.0 C_SIZE too large, reducing from %"PRIu32" to %"PRIu32"\n"), csdVersion2CSize, csdVersion2CSizeMax-1);
				csdVersion2CSize=csdVersion2CSizeMax-1;
			}

			card->blockCount=(csdVersion2CSize+1)*1024;
		} break;
		default: {
			kernelLog(LogTypeWarning, kstrP("sdInit failed: bad CSD version %u (powerPin=%u, slaveSelectPin=%u)\n"), csdVersion, powerPin, slaveSelectPin);
			result=SdInitResultBadCard;
			goto error;
		} break;
	}

	spiReadByte(); // read (and ignore) two CRC bytes
	spiReadByte();

	sdWriteDummyBytes();

	// Release slave select and lock
	pinWrite(slaveSelectPin, true);
	kernelSpiReleaseLock();

	assert(result==SdInitResultOk);
	return result;

	error:
	assert(result!=SdInitResultOk);
	pinWrite(slaveSelectPin, true);
	pinWrite(powerPin, false);
	kernelSpiReleaseLock();
	return result;
}

void sdQuit(SdCard *card) {
	// Not initialised?
	if (card->type==SdTypeBadCard)
		return;

	// Turn off power pin
	pinWrite(card->powerPin, false);

	// Mark 'unmounted'
	card->type=SdTypeBadCard;
}

bool sdReadBlock(SdCard *card, uint32_t block, uint8_t *data) {
	uint8_t responseByte;

	// Bad block?
	if (block>=card->blockCount) {
		kernelLog(LogTypeWarning, kstrP("sdReadBlock failed: bad block (block=%"PRIu32", count=%"PRIu32")\n"), block, card->blockCount);
		return false;
	}

	// Attempt to grab SPI bus lock
	if (!kernelSpiGrabLockNoSlaveSelect()) {
		kernelLog(LogTypeWarning, kstrP("sdReadBlock failed: could not grab SPI bus lock (block=%"PRIu32")\n"), block);
		return false;
	}

	// Ensure MOSI is high initially
	sdWriteDummyBytes();

	// Enable the slave by setting pin low
	pinWrite(card->slaveSelectPin, false);

	// Send CMD17 - read block
	uint32_t addr=(card->addressMode==SdAddressModeBlock ? block : block*SdBlockSize);
	spiWriteByte(0x51); // we use write byte rather than sdWriteCommand as address argument parts may be 0xFF
	spiWriteByte((addr>>24)&0xFF);
	spiWriteByte((addr>>16)&0xFF);
	spiWriteByte((addr>>8)&0xFF);
	spiWriteByte((addr>>0)&0xFF);
	spiWriteByte(0x01);
	responseByte=sdWaitForResponse(16);
	if (responseByte!=0x00) {
		kernelLog(LogTypeWarning, kstrP("sdReadBlock failed: bad CMD17 R1 response 0x%02X (block=%"PRIu32")\n"), responseByte, block);
		goto error;
	}

	// Read data token byte
	responseByte=sdWaitForResponse(1024);
	if (responseByte!=0xFE) {
		kernelLog(LogTypeWarning, kstrP("sdReadBlock failed: bad CMD17 data token byte 0x%02X (block=%"PRIu32")\n"), responseByte, block);
		goto error;
	}

	// Read data bytes
	for(unsigned i=0; i<SdBlockSize; ++i)
		data[i]=spiReadByte(); // Note: we cannot use sdWaitForResponse as we do not want to ignore 0xFF bytes for once

	// Read (and ignore) two CRC bytes
	spiReadByte();
	spiReadByte();

	// Flush after response
	sdWriteDummyBytes();

	// Release slave select and lock
	pinWrite(card->slaveSelectPin, true);
	kernelSpiReleaseLock();

	// Write to log
	kernelLog(LogTypeInfo, kstrP("sdReadBlock success (block=%"PRIu32")\n"), block);

	return true;

	error:
	pinWrite(card->slaveSelectPin, true);
	kernelSpiReleaseLock();
	return false;
}

bool sdWriteBlock(SdCard *card, uint32_t block, const uint8_t *data) {
	uint8_t responseByte;

	// Bad block?
	if (block>=card->blockCount) {
		kernelLog(LogTypeWarning, kstrP("sdWriteBlock failed: bad block (block=%"PRIu32", count=%"PRIu32")\n"), block, card->blockCount);
		return false;
	}

	// Attempt to grab SPI bus lock
	if (!kernelSpiGrabLockNoSlaveSelect()) {
		kernelLog(LogTypeWarning, kstrP("sdWriteBlock failed: could not grab SPI bus lock (block=%"PRIu32")\n"), block);
		return false;
	}

	// Ensure MOSI is high initially
	sdWriteDummyBytes();

	// Enable the slave by setting pin low
	pinWrite(card->slaveSelectPin, false);

	// Send CMD24 - write block
	uint32_t addr=(card->addressMode==SdAddressModeBlock ? block : block*SdBlockSize);
	spiWriteByte(0x58); // we use write byte rather than sdWriteCommand as address argument parts may be 0xFF
	spiWriteByte((addr>>24)&0xFF);
	spiWriteByte((addr>>16)&0xFF);
	spiWriteByte((addr>>8)&0xFF);
	spiWriteByte((addr>>0)&0xFF);
	spiWriteByte(0x01);
	responseByte=sdWaitForResponse(16);
	if (responseByte!=0x00) {
		kernelLog(LogTypeWarning, kstrP("sdWriteBlock failed: bad CMD24 R1 response 0x%02X (block=%"PRIu32")\n"), responseByte, block);
		goto error;
	}

	// Flush after response
	sdWriteDummyBytes();

	// Write data token byte
	spiWriteByte(0xFE);

	// Write data bytes
	for(unsigned i=0; i<SdBlockSize; ++i)
		spiWriteByte(data[i]);

	// Write two (dummy) CRC bytes
	spiWriteByte(0x01);
	spiWriteByte(0x01);

	// Flush after sending
	sdWriteDummyBytes();

	// Release slave select and lock
	pinWrite(card->slaveSelectPin, true);
	kernelSpiReleaseLock();

	// Write to log
	kernelLog(LogTypeInfo, kstrP("sdWriteBlock success (block=%"PRIu32")\n"), block);

	return true;

	error:
	pinWrite(card->slaveSelectPin, true);
	kernelSpiReleaseLock();
	return false;
}

////////////////////////////////////////////////////////////////////////////////
// Private functions
////////////////////////////////////////////////////////////////////////////////

void sdWriteCommand(uint8_t firstByte, ...) {
	va_list ap;
	va_start(ap, firstByte);

	spiWriteByte(firstByte);

	uint8_t byte;
	while((byte=(uint8_t)va_arg(ap, int))!=0xFF)
		spiWriteByte(byte);

	va_end(ap);
}

void sdWriteDummyBytes(void) {
	while(spiReadByte()!=0xFF)
		;
}

void sdWriteDummyBytesN(unsigned count) {
	for(unsigned i=0; i<count; ++i)
		spiWriteByte(0xFF);
}

uint8_t sdWaitForResponse(unsigned max) {
	for(unsigned i=0; i<max; ++i) {
		uint8_t byte=spiReadByte();
		if (byte!=0xFF)
			return byte;
	}
	return 0xFF;
}
