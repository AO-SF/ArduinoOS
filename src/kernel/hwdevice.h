#ifndef HWDEVICE_H
#define HWDEVICE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
	HwDeviceTypeUnused,
	HwDeviceTypeRaw, // let the user write to the two associated pins
	HwDeviceTypeSdCardReader,
	HwDeviceTypeDht22, // DHT22 moisture and humidity sensor
} HwDeviceType;

typedef uint8_t HwDeviceId;
#define HwDeviceIdMax 4

void hwDeviceInit(void); // should be one of the very first things to be called during kernelBoot (used to ensure pins are output and have the correct state asap)

bool hwDeviceRegister(HwDeviceId id, HwDeviceType type);
void hwDeviceDeregister(HwDeviceId id);

HwDeviceType hwDeviceGetType(HwDeviceId id);

HwDeviceId hwDeviceGetDeviceForPin(uint8_t pinNum); // returns HwDeviceIdMax if given pin is not associated with any device (used or unused)

bool hwDeviceSdCardReaderMount(HwDeviceId id, const char *mountPoint);
void hwDeviceSdCardReaderUnmount(HwDeviceId id);

int16_t hwDeviceDht22GetTemperature(HwDeviceId id);
int16_t hwDeviceDht22GetHumidity(HwDeviceId id);
uint32_t hwDeviceDht22GetLastReadTime(HwDeviceId id);
bool hwDeviceDht22Read(HwDeviceId id);

#endif
