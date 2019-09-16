; Syscall ids
const SyscallIdExit 0
const SyscallIdGetPid 1
const SyscallIdArgc 2
const SyscallIdArgvN 3
const SyscallIdFork 4
const SyscallIdExec 5
const SyscallIdWaitPid 6
const SyscallIdGetPidPath 7
const SyscallIdGetPidState 8
const SyscallIdGetAllCpuCounts 9
const SyscallIdKill 10
const SyscallIdGetPidRam 11
const SyscallIdSignal 12
const SyscallIdGetPidFdN 13

const SyscallIdRead 256
const SyscallIdWrite 257
const SyscallIdOpen 258
const SyscallIdClose 259
const SyscallIdDirGetChildN 260
const SyscallIdGetPath 261
const SyscallIdResizeFile 262
const SyscallIdGetFileLen 263
const SyscallIdTryReadByte 264
const SyscallIdIsDir 265
const SyscallIdFileExists 266
const SyscallIdDelete 267

const SyscallIdEnvGetStdinFd 512
const SyscallIdEnvSetStdinFd 513
const SyscallIdEnvGetPwd 514
const SyscallIdEnvSetPwd 515
const SyscallIdEnvGetPath 516
const SyscallIdEnvSetPath 517
const SyscallIdEnvGetStdoutFd 518
const SyscallIdEnvSetStdoutFd 519

const SyscallIdTimeMonotonic 768

const SyscallIdRegisterSignalHandler 1024

const SyscallIdShutdown 1280
const SyscallIdMount 1281
const SyscallIdUnmount 1282
const SyscallIdIoctl 1283
const SyscallIdGetLogLevel 1284
const SyscallIdSetLogLevel 1285

const SyscallIdStrChr 1536
const SyscallIdStrChrNul 1537
const SyscallIdMemMove 1538

const SyscallIdHwDeviceRegister 1792
const SyscallIdHwDeviceDeregister 1793
const SyscallIdHwDeviceGetType 1794
const SyscallIdHwDeviceSdCardReaderMount 1795
const SyscallIdHwDeviceSdCardReaderUnmount 1796
const SyscallIdHwDeviceDht22GetTemperature 1797
const SyscallIdHwDeviceDht22GetHumidity 1798

; Exec flags
const SyscallExecPathFlagLiteral 0
const SyscallExecPathFlagSearch 1

; WaitPid special return values
const SyscallWaitpidStatusSuccess 0
const SyscallWaitpidStatusInterrupted 65531
const SyscallWaitpidStatusNoProcess 65532
const SyscallWaitpidStatusKilled 65534
const SyscallWaitpidStatusTimeout 65535

; HW device constants
const SyscallHwDeviceIdMax 4

const SyscallHwDeviceTypeUnused 0
const SyscallHwDeviceTypeRaw 1
const SyscallHwDeviceTypeSdCardReader 2
const SyscallHwDeviceTypeDht22 3

; Mount type/format constants
const SyscallMountFormatCustomMiniFs 0
const SyscallMountFormatFlatFile 1
