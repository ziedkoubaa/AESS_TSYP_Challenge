#include <stdio.h>
#include <pthread.h>
#include <time.h>
#include <sched.h>
#include <unistd.h>

#include "params.h"
#include "features_if.h"
#include "power_fdir.h"

/* Sleep helper with CLOCK_MONOTONIC. */
static void sleep_ms(int ms){
  struct timespec ts = { ms/1000, (long)(ms%1000)*1000000L };
  clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, NULL);
}

int main(void)
{
  /* Optional: set real-time priority (requires CAP_SYS_NICE or sudo). */
  struct sched_param sp = {.sched_priority = 80};
  if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) != 0){
    perror("pthread_setschedparam (non-fatal)"); /* you can still run without RT */
  }

  if (!power_init()){
    fprintf(stderr, "power_init failed\n");
    return 1;
  }
  feats_reset();

  const int hop_ms = HOP_MS;     // 1 ms
  const float thr  = iforest_threshold();  // from model, or fallback

  fprintf(stdout, "IF threshold = %.6f ; loop hop = %d ms\n", thr, hop_ms);

  /* === Main 1 ms loop === */
  while (1){
    /* 1) Read latest raw samples (replace stub in power_fdir.c with real ADC). */
    float vin,iin,vout,iout,temp,ripple;
    read_latest_raw(&vin,&iin,&vout,&iout,&temp,&ripple);

    /* 2) Push into ring buffer at FS_HZ cadence:
          If your ADC runs in a separate thread/ISR at 10 kHz, call feats_push_raw() from there.
          For this simple loop, we assume effective sampling meets FS_HZ over time. */
    feats_push_raw(vin,iin,vout,iout,temp,ripple);

    /* 3) Compute features every hop; skip until we have a full window. */
    features_t feats;
    if (feats_compute(&feats)){
      /* 4) Model score + rule guard. */
      const float score = iforest_score(&feats);    // higher ⇒ more anomalous with this scorer
      const bool  ai_hit = (score > thr);
      const bool  rl_hit = rules_triggered(&feats);
      const bool  anomaly = ai_hit || rl_hit;

      /* 5) FDIR state machine step (CUT→HOLD→RESTART→VERIFY). */
      fdir_step(anomaly, &feats, score);
    }

    /* Keep cadence. Note: with other processes running, Linux may jitter—acceptable for prototype. */
    sleep_ms(hop_ms);
  }

  return 0;
}
