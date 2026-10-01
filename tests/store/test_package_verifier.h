#ifndef TEST_PACKAGE_VERIFIER_H
#define TEST_PACKAGE_VERIFIER_H
#include "tinyrt_store.h"
/* Test fixture only: NOT SHA-256 or signature verification. */
void test_package_make(uint8_t *, uint32_t, const char *, uint32_t, uint8_t, tinyrt_app_info_t *);
tinyrt_status_t test_package_verify(void *, const tinyrt_store_io_t *, uint32_t, uint32_t, tinyrt_app_info_t *);
#endif
