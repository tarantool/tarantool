#if __x86_64
#include "objc_msgSend.x86-64.S"
#elif __i386
#include "objc_msgSend.x86-32.S"
#elif __arm__
#include "objc_msgSend.arm.S"
#else
#warning objc_msgSend() not implemented for your architecture
#endif
