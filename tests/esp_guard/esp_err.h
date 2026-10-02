#ifndef TEST_ESP_ERR_H
#define TEST_ESP_ERR_H
#include <stdlib.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERROR_CHECK(call) do { if((call)!=ESP_OK)abort(); } while(0)
#endif
