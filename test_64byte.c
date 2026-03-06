/* test_64byte.c - Verify that 64-byte CAN FD pack/unpack round-trips correctly.
 * Compile: gcc -std=c99 -Wall -Wextra -I out out/canfd_64byte.c test_64byte.c -o test_64byte
 * Run:     ./test_64byte
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "out/canfd_64byte.h"

#define PASS "\033[32mPASS\033[0m"
#define FAIL "\033[31mFAIL\033[0m"

static int failures = 0;

static void check(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, FAIL ": %s\n", msg);
        failures++;
    } else {
        printf(PASS ": %s\n", msg);
    }
}

#define CHECK(cond, msg) check(!!(cond), msg)

int main(void) {
    can_obj_canfd_64byte_h_t obj;
    memset(&obj, 0, sizeof(obj));

    /* --- Test values --- */
    const uint8_t  s0 = 0xAB;
    const uint16_t s1 = 0x1234;
    const uint32_t s2 = 0xDEADBEEF;

    /* Encode signals into the object */
    CHECK(encode_can_0x001_Signal0(&obj, s0) == 0, "encode Signal0 = 0xAB");
    CHECK(encode_can_0x001_Signal1(&obj, s1) == 0, "encode Signal1 = 0x1234");
    CHECK(encode_can_0x001_Signal2(&obj, s2) == 0, "encode Signal2 = 0xDEADBEEF");

    /* Pack into a 64-byte buffer via the public dispatcher */
    uint8_t buf[64];
    memset(buf, 0xFF, sizeof(buf)); /* Fill with non-zero to detect overwrite bugs */

    int dlc = pack_message(&obj, 0x001, buf);
    CHECK(dlc == 64, "pack_message returns DLC=64");
    CHECK(message_dlc(0x001) == 64, "message_dlc(0x001) == 64");

    /* Verify signal bytes are in the right place (little-endian layout):
     *   byte 0       = Signal0 (8-bit)
     *   bytes 1-2    = Signal1 (16-bit, LE)
     *   bytes 3-6    = Signal2 (32-bit, LE)
     *   bytes 7      = padding zero from uint64_t memcpy (was 0xFF, now 0x00)
     *   bytes 8-63   = untouched 0xFF (outside the packed uint64_t)            */
    CHECK(buf[0] == s0, "buf[0] == 0xAB (Signal0)");
    CHECK(buf[1] == 0x34 && buf[2] == 0x12, "buf[1..2] == 0x1234 (Signal1, LE)");
    CHECK(buf[3] == 0xEF && buf[4] == 0xBE && buf[5] == 0xAD && buf[6] == 0xDE,
          "buf[3..6] == 0xDEADBEEF (Signal2, LE)");

    int upper_untouched = 1;
    for (int i = 8; i < 64; i++)
        if (buf[i] != 0xFF) { upper_untouched = 0; break; }
    CHECK(upper_untouched, "bytes 8-63 untouched by pack (upper CAN FD payload region preserved)");

    /* Unpack and verify round-trip */
    can_obj_canfd_64byte_h_t obj2;
    memset(&obj2, 0, sizeof(obj2));
    int r = unpack_message(&obj2, 0x001, buf, 64, 0);
    CHECK(r == 64, "unpack_message returns 64");
    CHECK(obj2.can_0x001_CANFDMessage_rx == 1, "rx flag set after unpack");

    uint8_t  out0 = 0;
    uint16_t out1 = 0;
    uint32_t out2 = 0;
    CHECK(decode_can_0x001_Signal0(&obj2, &out0) == 0, "decode Signal0 ok");
    CHECK(decode_can_0x001_Signal1(&obj2, &out1) == 0, "decode Signal1 ok");
    CHECK(decode_can_0x001_Signal2(&obj2, &out2) == 0, "decode Signal2 ok");

    CHECK(out0 == s0, "Signal0 round-trip: 0xAB");
    CHECK(out1 == s1, "Signal1 round-trip: 0x1234");
    CHECK(out2 == s2, "Signal2 round-trip: 0xDEADBEEF");

    /* Unpack must reject dlc < 64 (message requires full 64-byte frame) */
    can_obj_canfd_64byte_h_t obj3;
    memset(&obj3, 0, sizeof(obj3));
    int rshort = unpack_message(&obj3, 0x001, buf, 8, 0);
    CHECK(rshort == -1, "unpack with dlc=8 returns -1 (too short for 64-byte message)");

    /* Unknown CAN ID returns -1 */
    CHECK(pack_message(&obj, 0xDEAD, buf) == -1, "pack unknown ID returns -1");
    CHECK(unpack_message(&obj, 0xDEAD, buf, 64, 0) == -1, "unpack unknown ID returns -1");
    CHECK(message_dlc(0xDEAD) == -1, "message_dlc unknown ID returns -1");

    printf("\n%s: %d failure(s)\n", failures == 0 ? PASS : FAIL, failures);
    return failures ? 1 : 0;
}
