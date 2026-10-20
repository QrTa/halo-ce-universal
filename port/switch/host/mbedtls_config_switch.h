/*
MBEDTLS_CONFIG_SWITCH.H

The Switch's additions to Mbed TLS's configuration (port/third_party/mbedtls,
which is not edited): tools/switch_build.py gives this file to the library
and to posix_dtls.c as MBEDTLS_USER_CONFIG_FILE, which build_info.h includes
after mbedtls_config.h.

Mbed TLS's own entropy source reads /dev/urandom or calls getrandom, which
the console has neither of; its random numbers come from libnx's randomGet
instead (a ChaCha20 generator the kernel's entropy seeds), through the
hardware source (mbedtls_hardware_poll, port/switch/host/host_net.c).
Its millisecond clock is the console's too.
*/

#ifndef SWITCH_MBEDTLS_CONFIG_H
#define SWITCH_MBEDTLS_CONFIG_H

#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_ENTROPY_HARDWARE_ALT

/* (devkitA64 is neither Unix nor Windows to the library: its sockets and
timer modules, which posix_dtls.c does not use (it carries the datagrams
and keeps the timers itself), are left out, and its millisecond clock is
the console's, mbedtls_ms_time in host_net.c) */
#undef MBEDTLS_NET_C
#undef MBEDTLS_TIMING_C
#define MBEDTLS_PLATFORM_MS_TIME_ALT

#endif
