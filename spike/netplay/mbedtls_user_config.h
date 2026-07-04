/* Appended to MbedTLS's default config via MBEDTLS_USER_CONFIG_FILE
 * (set for every TU in this build so the library and its consumers
 * compile against the same configuration).
 *
 * libdatachannel's MbedTLS DTLS transport (src/impl/dtlstransport.cpp)
 * references the DTLS-SRTP API unconditionally — even with NO_MEDIA —
 * and the default MbedTLS config compiles that API out. */

#define MBEDTLS_SSL_DTLS_SRTP
