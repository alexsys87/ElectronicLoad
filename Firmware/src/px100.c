/** \file
  \brief PX-100 (rev. 2.70) binary protocol, device side.

  Reference: github.com/misdoro/Electronic_load_px100, protocol_PX-100_2_70.md
  and ../WpfApp/PX100Protocol.cs (the host side).

    0x01 on/off     D1 = 1 on, 0 off
    0x02 current    D1 = integer part, D2 = hundredths (A)
    0x03 cutoff     D1 = integer part, D2 = hundredths (V)
    0x04 timer      D1:D2 = seconds, big endian
    0x05 reset      clear mAh / mWh / time

    0x10 on/off     0x11 mV         0x12 mA         0x13 time h:m:s
    0x14 mAh        0x15 mWh        0x16 degC       0x17 set current A*100
    0x18 cutoff V*100               0x19 timer h:m:s

  Framing: bytes are collected until they form B1 B2 x x x B6. Anything
  else is skipped byte by byte, so the parser resynchronizes on its own
  after line noise or a half sent frame.
*/

#include "px100.h"
#include "battery_emu.h"

#include <stddef.h>

#define REQ_HEAD1   0xB1
#define REQ_HEAD2   0xB2
#define REQ_TAIL    0xB6
#define ACK         0x6F

static px100_write_fn out;
static uint8_t frame[6];
static uint8_t frame_len;

void px100_init(px100_write_fn write) {
  out = write;
  frame_len = 0;
}

static void send_ack(void) {
  static const uint8_t ack = ACK;

  out(&ack, 1);
}

static void send_value(uint32_t v) {
  uint8_t a[7];

  if (v > 0xFFFFFFUL)
    v = 0xFFFFFFUL;
  a[0] = 0xCA;
  a[1] = 0xCB;
  a[2] = (uint8_t)(v >> 16);
  a[3] = (uint8_t)(v >> 8);
  a[4] = (uint8_t)v;
  a[5] = 0xCE;
  a[6] = 0xCF;
  out(a, sizeof(a));
}

/// Time as three bytes h, m, s (hours saturate at 255).
static void send_hms(uint32_t seconds) {
  uint32_t h = seconds / 3600U;

  if (h > 255U)
    h = 255U;
  send_value((h << 16) | (((seconds / 60U) % 60U) << 8) | (seconds % 60U));
}

static void handle(uint8_t cmd, uint8_t d1, uint8_t d2) {
  switch (cmd) {
    // Control.
    case 0x01:
      emu_set_output(d1 != 0);
      send_ack();
      break;
    case 0x02:
      emu_set_current_ca((uint16_t)(d1 * 100U + (d2 > 99 ? 99 : d2)));
      send_ack();
      break;
    case 0x03:
      emu_set_cutoff_cv((uint16_t)(d1 * 100U + (d2 > 99 ? 99 : d2)));
      send_ack();
      break;
    case 0x04:
      emu_set_timer_s((uint16_t)((d1 << 8) | d2));
      send_ack();
      break;
    case 0x05:
      emu_reset_counters();
      send_ack();
      break;

    // Queries.
    case 0x10: send_value(emu_output_on() ? 1U : 0U);         break;
    case 0x11: send_value(emu_voltage_mv());                   break;
    case 0x12: send_value(emu_current_ma());                   break;
    case 0x13: send_hms(emu_test_time_s());                    break;
    case 0x14: send_value(emu_capacity_mah());                 break;
    case 0x15: send_value(emu_energy_mwh());                   break;
    case 0x16: {
      int32_t t = emu_temperature_c();

      send_value(t > 0 ? (uint32_t)t : 0U);
      break;
    }
    case 0x17: send_value(emu_current_setpoint_ca());          break;
    case 0x18: send_value(emu_cutoff_setpoint_cv());           break;
    case 0x19: send_hms(emu_timer_setpoint_s());               break;

    default:                            // Unknown: no answer, like the original.
      break;
  }
}

/// Does frame[0 .. frame_len) look like the start of a request?
static uint8_t frame_prefix_ok(void) {
  if (frame_len >= 1 && frame[0] != REQ_HEAD1)
    return 0;
  if (frame_len >= 2 && frame[1] != REQ_HEAD2)
    return 0;
  if (frame_len >= 6 && frame[5] != REQ_TAIL)
    return 0;
  return 1;
}

void px100_feed(uint8_t byte) {
  frame[frame_len++] = byte;

  // Drop leading bytes until the rest could still become a frame.
  while (frame_len > 0 && ! frame_prefix_ok()) {
    uint8_t i;

    for (i = 1; i < frame_len; i++)
      frame[i - 1] = frame[i];
    frame_len--;
  }

  if (frame_len == sizeof(frame)) {
    frame_len = 0;
    if (out != NULL)
      handle(frame[2], frame[3], frame[4]);
  }
}
