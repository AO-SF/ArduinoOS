; TODO: Add the rest

const SyscallIdExit 0
const SyscallIdWaitpid 6

const SyscallIdRead 256
const SyscallIdWrite 257
const SyscallIdOpen 258
const SyscallIdClose 259
const SyscallIdResizeFile 262

const SyscallIdEnvGetStdinFd 512
const SyscallIdEnvSetStdinFd 513
const SyscallIdEnvGetStdoutFd 518
const SyscallIdEnvSetStdoutFd 519

const SyscallIdRegisterSignalHandler 1024

const SyscallWaitpidStatusSuccess 0
const SyscallWaitpidStatusInterrupted 65531
const SyscallWaitpidStatusNoProcess 65532
const SyscallWaitpidStatusKilled 65534
const SyscallWaitpidStatusTimeout 65535
