/** \file
  \brief PX-100 (rev. 2.70) binary protocol, device side.

  The PC software in ../WpfApp talks this protocol, so it drives the
  emulator exactly like a real PX-100 electronic load.

    Request : B1 B2 CMD D1 D2 B6
    Control : CMD 0x01..0x05, answer 6F
    Query   : CMD 0x10..0x19, answer CA CB d1 d2 d3 CE CF (24 bit, MSB first)
*/

#ifndef PX100_H
#define PX100_H

#include <stdint.h>

/// Sends the answer bytes to the host.
typedef void (*px100_write_fn)(const uint8_t *data, uint16_t len);

void px100_init(px100_write_fn write);

/// Feed one received byte. Answers are sent from inside this call.
void px100_feed(uint8_t byte);

#endif /* PX100_H */
