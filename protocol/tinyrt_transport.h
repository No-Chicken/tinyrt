#ifndef TINYRT_TRANSPORT_H
#define TINYRT_TRANSPORT_H
#include <stdint.h>
#define TINYRT_MGMT_VERSION 1u
#define TINYRT_MGMT_MAX_MESSAGE 256u
#define TINYRT_MGMT_FRAME_SIZE 20u
#define TINYRT_MGMT_HEADER_SIZE 10u
#define TINYRT_OP_HELLO 0x10u
#define TINYRT_OP_LIST 0x11u
#define TINYRT_OP_QUERY 0x12u
#define TINYRT_OP_PREPARE 0x13u
#define TINYRT_OP_UNINSTALL 0x14u
#define TINYRT_OP_LAUNCH 0x15u
#define TINYRT_OP_STOP 0x16u
#define TINYRT_OP_LIST_QUARANTINED 0x17u
#define TINYRT_OP_LIST_PAGE 0x18u
#define TINYRT_OP_STORAGE 0x19u
#define TINYRT_OP_APP_INFO 0x1Au
#define TINYRT_OP_RUNTIME_INFO 0x1Bu
#define TINYRT_CAP_QUARANTINE 8u
#define TINYRT_CAP_PAGED_LIST 16u
#define TINYRT_CAP_STORAGE 32u
#define TINYRT_CAP_PACKAGE_V2 64u
typedef struct {
    uint8_t payload[TINYRT_MGMT_MAX_MESSAGE];
    uint16_t request_id, total, received;
    uint8_t opcode, response, active;
} tinyrt_mgmt_rx_t;
/* 0 complete, 1 incomplete, -1 malformed (partial state discarded).
 * Caller resets on disconnect and after a 5 s incomplete-message timeout. */
int tinyrt_mgmt_feed(tinyrt_mgmt_rx_t *, const uint8_t *, uint16_t);
void tinyrt_mgmt_reset(tinyrt_mgmt_rx_t *);
typedef int (*tinyrt_mgmt_emit_fn)(void *, const uint8_t *, uint16_t);
int tinyrt_mgmt_emit(uint8_t opcode, uint16_t request_id, uint8_t response,
    const uint8_t *, uint16_t, tinyrt_mgmt_emit_fn, void *);
#endif
