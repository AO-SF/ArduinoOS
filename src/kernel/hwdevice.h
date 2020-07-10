#ifndef HWDEVICE_H
#define HWDEVICE_H

#include <stdbool.h>
#include <stdint.h>

#define HwDevicePinsMax 4 // max pins per HW device (not including general pins such as those used by the SPI bus)

typedef enum {
	HwDeviceTypeUnused,
	HwDeviceTypeRaw, // let the user write to the two associated pins
	HwDeviceTypeSdCardReader,
	HwDeviceTypeDht22, // DHT22 moisture and humidity sensor
} HwDeviceType;
#define HwDeviceTypeBits 2

typedef uint8_t HwDeviceId;
#define HwDeviceIdMax 4

void hwDeviceInit(void); // should be one of the very first things to be called during kernelBoot (used to ensure pins are output and have the correct state asap)
void hwDeviceTick(void); // ticks all hardware devices which need it

bool hwDeviceRegister(HwDeviceId id, HwDeviceType type, const uint8_t *pins); // number of entries in pins array should be equal to hwDeviceTypeGetPinCount
void hwDeviceDeregister(HwDeviceId id);

HwDeviceType hwDeviceGetType(HwDeviceId id);
uint8_t hwDeviceGetPinN(HwDeviceId id, unsigned n); // returns PinInvalid on failure (bad id or n too large for device type)

uint8_t hwDeviceSdCardReaderGetPowerPin(HwDeviceId id); // returns PinInvalid on failure (bad id)
uint8_t hwDeviceSdCardReaderGetSlaveSelectPin(HwDeviceId id); // returns PinInvalid on failure (bad id)
bool hwDeviceSdCardReaderMount(HwDeviceId id, const char *mountPoint);
void hwDeviceSdCardReaderUnmount(HwDeviceId id);

uint8_t hwDeviceDht22GetPowerPin(HwDeviceId id); // returns PinInvalid on failure (bad id)
uint8_t hwDeviceDht22GetDataPin(HwDeviceId id); // returns PinInvalid on failure (bad id)
int16_t hwDeviceDht22GetTemperature(HwDeviceId id);
int16_t hwDeviceDht22GetHumidity(HwDeviceId id);
uint32_t hwDeviceDht22GetLastReadTime(HwDeviceId id);
bool hwDeviceDht22Read(HwDeviceId id);

unsigned hwDeviceTypeGetPinCount(HwDeviceType type); // number of pins that should be contained in the array passed to hwDeviceRegister

#endif
