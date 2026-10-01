#ifndef FAKE_NOR_H
#define FAKE_NOR_H
#include "tinyrt_store.h"
typedef struct { char kind; uint32_t off, len; } fake_nor_op_t;
typedef struct {
    uint8_t bytes[TINYRT_STORE_SIZE];
    fake_nor_op_t log[2048];
    unsigned mutations, reads, fail_at;
    uint32_t partial_bytes;
    int powered_off, fail_read, stay_on_after_failure, fail_reads_on_failure;
    uint32_t fail_read_offset;
} fake_nor_t;
void fake_nor_init(fake_nor_t *);
void fake_nor_reset_log(fake_nor_t *);
tinyrt_store_io_t fake_nor_io(fake_nor_t *);
#endif
