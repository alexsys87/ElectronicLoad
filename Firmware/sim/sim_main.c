/** \file
  \brief PC build of the battery emulator (Linux, macOS, WSL).

  Uses the same battery_emu.c and px100.c as the firmware.

    battery_emu_sim --curve [--current A] [--cutoff V]
        Runs one full discharge as fast as possible and prints it as CSV
        (simulated time, V, A, mAh, mWh, degC). Handy to check the model
        after changing config.h, without hardware.

    battery_emu_sim [--scale N]
        Speaks the PX-100 protocol on stdin / stdout in real time, like the
        board does on its COM port. For testing host software through a
        pipe or a pseudo terminal (socat).
*/

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/select.h>

#include "battery_emu.h"
#include "px100.h"
#include "config.h"

static void stdout_write(const uint8_t *data, uint16_t len) {
  fwrite(data, 1, len, stdout);
  fflush(stdout);
}

static uint64_t now_ms(void) {
  struct timespec ts;

  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000U + (uint64_t)ts.tv_nsec / 1000000U;
}

static const char *reason_text(emu_stop_reason_t r) {
  switch (r) {
    case EMU_STOP_USER:       return "user";
    case EMU_STOP_CUTOFF:     return "cutoff voltage";
    case EMU_STOP_SAFE_LIMIT: return "safe voltage limit";
    case EMU_STOP_TIMER:      return "timer";
    case EMU_STOP_OVERTEMP:   return "over temperature";
    default:                  return "-";
  }
}

static int run_curve(double amps, double cutoff) {
  uint32_t step = 0;
  float t0;

  emu_init();
  emu_set_current_ca((uint16_t)(amps * 100.0 + 0.5));
  emu_set_cutoff_cv((uint16_t)(cutoff * 100.0 + 0.5));
  emu_set_output(true);
  t0 = emu_sim_time_s();

  printf("time_s,voltage_V,current_A,capacity_mAh,energy_mWh,temp_C\n");
  for (;;) {
    emu_step();
    // One line per simulated 10 s, plus the final one.
    if (step++ % 100U == 0U || ! emu_output_on())
      printf("%.1f,%.3f,%.3f,%u,%u,%d\n",
             (double)(emu_sim_time_s() - t0),
             emu_voltage_mv() / 1000.0, emu_current_ma() / 1000.0,
             emu_capacity_mah(), emu_energy_mwh(), emu_temperature_c());
    if ( ! emu_output_on())
      break;
    if (step > 100000000U) {
      fprintf(stderr, "no cutoff reached\n");
      return 1;
    }
  }
  fprintf(stderr, "stopped by %s after %u s, %u mAh, %u mWh\n",
          reason_text(emu_stop_reason()), emu_test_time_s(),
          emu_capacity_mah(), emu_energy_mwh());
  return 0;
}

static int run_protocol(uint16_t scale) {
  uint64_t last = now_ms();

  emu_init();
  emu_set_time_scale(scale);
  px100_init(stdout_write);

  for (;;) {
    fd_set rd;
    struct timeval tv = { 0, EMU_STEP_MS * 1000 };
    uint64_t now;

    FD_ZERO(&rd);
    FD_SET(STDIN_FILENO, &rd);
    if (select(STDIN_FILENO + 1, &rd, NULL, NULL, &tv) > 0) {
      uint8_t buf[64];
      ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
      ssize_t i;

      if (n <= 0)
        return 0;                         // Host closed the pipe.
      for (i = 0; i < n; i++)
        px100_feed(buf[i]);
    }

    now = now_ms();
    while (now - last >= EMU_STEP_MS) {
      emu_step();
      last += EMU_STEP_MS;
    }
  }
}

int main(int argc, char **argv) {
  double amps = EMU_DEFAULT_CURRENT_CA / 100.0;
  double cutoff = EMU_DEFAULT_CUTOFF_CV / 100.0;
  int scale = EMU_TIME_SCALE;
  int curve = 0;
  int i;

  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--curve") == 0)
      curve = 1;
    else if (strcmp(argv[i], "--current") == 0 && i + 1 < argc)
      amps = atof(argv[++i]);
    else if (strcmp(argv[i], "--cutoff") == 0 && i + 1 < argc)
      cutoff = atof(argv[++i]);
    else if (strcmp(argv[i], "--scale") == 0 && i + 1 < argc)
      scale = atoi(argv[++i]);
    else {
      fprintf(stderr, "usage: %s [--curve [--current A] [--cutoff V]] "
                      "[--scale N]\n", argv[0]);
      return 2;
    }
  }

  if (curve)
    return run_curve(amps, cutoff);
  return run_protocol((uint16_t)(scale > 0 && scale < 65536 ? scale : 1));
}
