#include "power_fdir.h"
#include "params.h"
#include <gpiod.h>
#include <time.h>
#include <stdio.h>

typedef enum { ST_NORMAL=0, ST_HOLD, ST_VERIFY, ST_SAFE } fdir_state_t;
static fdir_state_t st = ST_NORMAL;
static int dwell_hits = 0;

static struct gpiod_chip* chip = NULL;
static struct gpiod_line* en_line = NULL;
static struct gpiod_line* ef_line = NULL;

static struct timespec deadline = {0,0};

static inline void set_deadline_ms(int ms){
  clock_gettime(CLOCK_MONOTONIC, &deadline);
  deadline.tv_nsec += (long)(ms % 1000) * 1000000L;
  deadline.tv_sec  += (ms / 1000) + deadline.tv_nsec / 1000000000L;
  deadline.tv_nsec %= 1000000000L;
}
static inline bool deadline_done(void){
  struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
  return (now.tv_sec > deadline.tv_sec) ||
         (now.tv_sec == deadline.tv_sec && now.tv_nsec >= deadline.tv_nsec);
}

static void gpio_set(struct gpiod_line* ln, int val){
  if (ln) gpiod_line_set_value(ln, val ? 1 : 0);
}

bool power_init(void)
{
  chip = gpiod_chip_open_by_name(GPIOCHIP_NAME);
  if (!chip) { perror("gpiod_chip_open"); return false; }

  if (PIN_REG_EN >= 0){
    en_line = gpiod_chip_get_line(chip, PIN_REG_EN);
    if (!en_line || gpiod_line_request_output(en_line, "sel_en", 1) < 0) { perror("EN request"); return false; }
  }
  if (PIN_EFUSE_EN >= 0){
    ef_line = gpiod_chip_get_line(chip, PIN_EFUSE_EN);
    if (!ef_line || gpiod_line_request_output(ef_line, "sel_efuse", 1) < 0) { perror("EFUSE request"); return false; }
  }
  // Start powered
  gpio_set(en_line, 1); gpio_set(ef_line, 1);
  return true;
}

void power_cut(void){
  gpio_set(en_line, 0);
  gpio_set(ef_line, 0);
}

void power_restart_soft(void){
  gpio_set(ef_line, 1);
  gpio_set(en_line, 1);
}

void fdir_step(bool anomaly, const features_t* f, float score)
{
  switch (st)
  {
    case ST_NORMAL:
      if (anomaly) {
        if (++dwell_hits >= DWELL_HITS) {
          power_cut();
          set_deadline_ms(HOLDOFF_MS);
          st = ST_HOLD;
        }
      } else dwell_hits = 0;
      break;

    case ST_HOLD:
      if (deadline_done()){
        power_restart_soft();
        set_deadline_ms(VERIFY_MS);
        st = ST_VERIFY;
      }
      break;

    case ST_VERIFY: {
      /* Minimal re-latch check: if indicators rise again during verify, go SAFE */
      const bool relatch = (f->dI_dt > THR_DIDT_A_PER_MS*0.8f) && (f->Vout_droop > THR_DROOP_V*0.8f);
      if (relatch){
        power_cut();
        st = ST_SAFE;
      } else if (deadline_done()) {
        st = ST_NORMAL;
        dwell_hits = 0;
      }
    } break;

    case ST_SAFE:
      /* Stay off; operator/ground can re-enable manually (prototype behavior). */
      break;
  }
}

/* ====== Replace with your SPI-ADC reads ======
 * For a minimal compile/run, these stubs return nominal values.
 * Integrate your ADS7042/INA229/etc. driver and feed real values.
 */
void read_latest_raw(float* vin, float* iin, float* vout, float* iout, float* temp, float* ripple)
{
  *vin = 8.0f; *iin = 0.40f; *vout = 5.00f; *iout = 0.50f; *temp = 40.0f;
#if USE_RIPPLE
  *ripple = 0.010f;
#else
  *ripple = 0.0f;
#endif
}
