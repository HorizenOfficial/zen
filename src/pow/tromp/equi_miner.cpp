#include "equi_miner.h"

u32 hashsize(const u32 r) {
  const u32 hashbits = WN - (r+1) * DIGITBITS + RESTBITS;
  return (hashbits + 7) / 8;
}

u32 hashwords(u32 bytes) {
  return (bytes + 3) / 4;
}

u32 min(const u32 a, const u32 b) {
  return a < b ? a : b;
}

void barrier(pthread_barrier_t *barry) {
  const int rc = pthread_barrier_wait(barry);
  if (rc != 0 && rc != PTHREAD_BARRIER_SERIAL_THREAD) {
//    printf("Could not wait on barrier\n");
    pthread_exit(NULL);
  }
}

void *worker(void *vp) {
  thread_ctx *tp = (thread_ctx *)vp;
  equi *eq = tp->eq;

  if (tp->id == 0)
//    printf("Digit 0\n");
  barrier(&eq->barry);
  eq->digit0(tp->id);
  barrier(&eq->barry);
  if (tp->id == 0) {
    eq->xfull = eq->bfull = eq->hfull = 0;
    eq->showbsizes(0);
  }
  barrier(&eq->barry);
  for (u32 r = 1; r < WK; r++) {
    if (tp->id == 0)
//      printf("Digit %d", r);
    barrier(&eq->barry);
    r&1 ? eq->digitodd(r, tp->id) : eq->digiteven(r, tp->id);
    barrier(&eq->barry);
    if (tp->id == 0) {
//      printf(" x%d b%d h%d\n", eq->xfull, eq->bfull, eq->hfull);
      eq->xfull = eq->bfull = eq->hfull = 0;
      eq->showbsizes(r);
    }
    barrier(&eq->barry);
  }
  if (tp->id == 0)
//    printf("Digit %d\n", WK);
  eq->digitK(tp->id);
  barrier(&eq->barry);
  pthread_exit(NULL);
  return 0;
}