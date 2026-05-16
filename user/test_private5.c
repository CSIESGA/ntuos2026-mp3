#include "kernel/types.h"
#include "user/user.h"

static int
parse_algo(char *s)
{
  if(strcmp(s, "FP") == 0 || strcmp(s, "fp") == 0)
    return SCHED_PRIORITY;
  if(strcmp(s, "EFDF") == 0 || strcmp(s, "efdf") == 0)
    return SCHED_EFDF;
  if(strcmp(s, "DBP") == 0 || strcmp(s, "dbp") == 0)
    return SCHED_DBP;
  return -1;
}

int
main(int argc, char *argv[])
{
  if(argc < 2){
    printf("usage: test_preempt <FP|EFDF|DBP>\n");
    exit(1);
  }
  int algo = parse_algo(argv[1]);
  if(algo < 0){
    printf("unknown algorithm: %s\n", argv[1]);
    printf("usage: test_preempt <EFDF|PRIORITY|RR>\n");
    exit(1);
  }
  set_sched_algo(algo);

  int pid1 = fork();
  if(pid1 == 0){
    set_firm_constraints(getpid(), 6, 8);
    start_periodic_task(1, 3, 20, 3, 0);
    for(;;);
  }

  int pid2 = fork();
  if(pid2 == 0){
    set_firm_constraints(getpid(), 2, 8);
    start_periodic_task(1, 5, 12, 3, 0);
    for(;;);
  }

  int pid3 = fork();
  if(pid3 == 0){
    set_firm_constraints(getpid(), 3, 8);
    start_periodic_task(2, 7, 8, 2, 0);
    for(;;);
  }

  int pid4 = fork();
  if(pid4 == 0){
    set_firm_constraints(getpid(), 4, 5);
    start_periodic_task(3, 13, 5, 4, 0);
    for(;;);
  }

  int pid5 = fork();
  if(pid5 == 0){
    set_firm_constraints(getpid(), 2, 5);
    start_periodic_task(9, 19, 3, 3, 0);
    for(;;);
  }

  release_sync(5);

  int status;
  wait(&status);
  wait(&status);
  wait(&status);
  wait(&status);
  wait(&status);
  exit(0);
}
