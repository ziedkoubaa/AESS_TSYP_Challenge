// logger_mcp3008.c — Logger MCP3008 haute cadence → CSV
// gcc -O3 -Wall -lrt -o logger_mcp3008 logger_mcp3008.c
#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <unistd.h>

static volatile int stop_flag = 0;
static void on_sigint(int s){ (void)s; stop_flag = 1; }

static const char* DEV = "/dev/spidev0.0";
static const uint32_t SPI_HZ = 1350000;
static const uint8_t  SPI_MODE = 0;
static const uint8_t  SPI_BITS = 8;

static inline double now_s(void){
  struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec + ts.tv_nsec*1e-9;
}

static int spi_fd = -1;
static int spi_open_dev(const char* dev){
  int fd = open(dev, O_RDWR);
  if (fd < 0) return -1;
  ioctl(fd, SPI_IOC_WR_MODE, &SPI_MODE);
  ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &SPI_BITS);
  ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &SPI_HZ);
  return fd;
}

static inline uint16_t mcp3008_read(int fd, uint8_t ch){
  uint8_t tx[3] = {1, (uint8_t)((8+ch)<<4), 0};
  uint8_t rx[3] = {0};
  struct spi_ioc_transfer tr = {
    .tx_buf = (unsigned long)tx,
    .rx_buf = (unsigned long)rx,
    .len = 3,
    .speed_hz = SPI_HZ,
    .bits_per_word = SPI_BITS,
    .cs_change = 0,
  };
  ioctl(fd, SPI_IOC_MESSAGE(1), &tr);
  return (uint16_t)(((rx[1] & 3) << 8) | rx[2]); // 10 bits
}

int main(int argc, char** argv){
  if (argc < 4){
    fprintf(stderr, "Usage: %s out.csv sample_rate_Hz duration_s\n", argv[0]);
    return 1;
  }
  const char* csv = argv[1];
  const int rate  = atoi(argv[2]);
  const int dur_s = atoi(argv[3]);
  if (rate <= 0 || dur_s <= 0){ fprintf(stderr, "Bad args\n"); return 1; }

  // --- Paramètres physiques (identiques au script Python) ---
  const double VREF = 3.3;
  const int CH_VIN=0, CH_IIN=1, CH_VOUT=2, CH_IOUT=3, CH_TEMP=4, CH_RIPPLE=5;
  const double R1=100000.0, R2=10000.0, R3=47000.0, R4=10000.0;
  const double RSHUNT_IN=0.01,  GAIN_IN=50.0,  VOFF_IN=0.0;
  const double RSHUNT_OUT=0.02, GAIN_OUT=50.0, VOFF_OUT=0.0;
  const double R_SERIE=10000.0, NTC_R0=10000.0, NTC_T0=25.0+273.15, NTC_B=3950.0;
  const int     USE_RIPPLE = 1;
  const double  K_RIPPLE = 1.0;

  signal(SIGINT, on_sigint);

  spi_fd = spi_open_dev(DEV);
  if (spi_fd < 0){ perror("open spidev"); return 1; }

  FILE* f = fopen(csv, "w");
  if (!f){ perror("fopen"); return 1; }
  setvbuf(f, NULL, _IOFBF, 1<<20); // gros buffer

  fprintf(f, "time_s,Vin_V,Iin_A,Vout_V,Iout_A,Temp_C,ripple_V\n");

  const double ts = 1.0 / (double)rate;
  const double t0 = now_s();
  double next = t0;
  int64_t n = 0;

  while (!stop_flag){
    double t = now_s();
    if (t - t0 >= (double)dur_s) break;
    if (t < next){
      struct timespec slp;
      double dt = next - t;
      slp.tv_sec = (time_t)dt;
      slp.tv_nsec= (long)((dt - slp.tv_sec) * 1e9);
      clock_nanosleep(CLOCK_MONOTONIC, 0, &slp, NULL);
      t = now_s();
    }
    next += ts;

    // lecture ADC
    uint16_t cvin  = mcp3008_read(spi_fd, CH_VIN);
    uint16_t ciin  = mcp3008_read(spi_fd, CH_IIN);
    uint16_t cvout = mcp3008_read(spi_fd, CH_VOUT);
    uint16_t ciout = mcp3008_read(spi_fd, CH_IOUT);
    uint16_t ctmp  = mcp3008_read(spi_fd, CH_TEMP);
    uint16_t crip  = mcp3008_read(spi_fd, CH_RIPPLE);

    double v_vin_adc = VREF * (cvin  / 1023.0);
    double v_iin_adc = VREF * (ciin  / 1023.0);
    double v_vout_adc= VREF * (cvout / 1023.0);
    double v_iout_adc= VREF * (ciout / 1023.0);
    double v_tmp_adc = VREF * (ctmp  / 1023.0);
    double v_rip_adc = VREF * (crip  / 1023.0);

    double vin  = v_vin_adc  * ((R1+R2)/R2);
    double vout = v_vout_adc * ((R3+R4)/R4);

    double iin  = (v_iin_adc  - VOFF_IN ) / (RSHUNT_IN  * GAIN_IN);
    double iout = (v_iout_adc - VOFF_OUT) / (RSHUNT_OUT * GAIN_OUT);

    double temp_c = NAN;
    if (v_tmp_adc > 0.0 && v_tmp_adc < VREF){
      double r_ntc = (v_tmp_adc * R_SERIE) / (VREF - v_tmp_adc);
      double inv_T = (1.0/NTC_T0) + (1.0/NTC_B)*log(r_ntc/NTC_R0);
      temp_c = (1.0/inv_T) - 273.15;
    }

    double ripple_v = USE_RIPPLE ? (v_rip_adc * K_RIPPLE) : 0.0;

    double rel_t = now_s() - t0;
    fprintf(f, "%.6f,%.6f,%.6f,%.6f,%.6f,%.3f,%.6f\n",
            rel_t, vin, iin, vout, iout, temp_c, ripple_v);

    if ((++n % (rate/2)) == 0) fflush(f); // flush ~2x/s
  }

  fclose(f);
  close(spi_fd);
  fprintf(stderr, "Done. Wrote %s\n", csv);
  return 0;
}
