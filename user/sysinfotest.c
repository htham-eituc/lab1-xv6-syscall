#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/riscv.h"
#include "kernel/sysinfo.h"
#include "user/user.h"

void
testfiles() {
  struct sysinfo info;
  int fd;

  sysinfo(&info);
  int before = info.nopenfiles;

  printf("[testfiles] before = %d\n", before);

  // open a file
  fd = open("README", 0);
  if(fd < 0){
    printf("FAIL: open failed\n");
    exit(1);
  }

  sysinfo(&info);
  printf("[testfiles] after open = %ld\n", info.nopenfiles);

  if(info.nopenfiles != before + 1){
    printf("FAIL: nopenfiles did not increase\n");
    exit(1);
  }

  close(fd);

  sysinfo(&info);
  printf("[testfiles] after close = %ld\n", info.nopenfiles);

  if(info.nopenfiles != before){
    printf("FAIL: nopenfiles did not decrease\n");
    exit(1);
  }
}

void
sinfo(struct sysinfo *info) {
  if (sysinfo(info) < 0) {
    printf("FAIL: sysinfo failed");
    exit(1);
  }
}

//
// use sbrk() to count how many free physical memory pages there are.
//
int
countfree()
{
  uint64 sz0 = (uint64)sbrk(0);
  struct sysinfo info;
  int n = 0;

  while(1){
    if((uint64)sbrk(PGSIZE) == 0xffffffffffffffff){
      break;
    }
    n += PGSIZE;
  }
  sinfo(&info);
  if (info.freemem != 0) {
    printf("FAIL: there is no free mem, but sysinfo.freemem=%ld\n",
      info.freemem);
    exit(1);
  }
  sbrk(-((uint64)sbrk(0) - sz0));
  return n;
}

void
testmem() {
  struct sysinfo info;
  uint64 n = countfree();
  
  sinfo(&info);
  printf("[testmem] freemem = %ld (expected %ld)\n", info.freemem, n);

  if (info.freemem!= n) {
    printf("FAIL: free mem %ld (bytes) instead of %ld\n", info.freemem, n);
    exit(1);
  }
  
  printf("[testmem] allocating one page...\n");

  if((uint64)sbrk(PGSIZE) == 0xffffffffffffffff){
    printf("sbrk failed");
    exit(1);
  }

  sinfo(&info);
  printf("[testmem] after alloc = %ld (expected %ld)\n", info.freemem, n - PGSIZE);

  if (info.freemem != n-PGSIZE) {
    printf("FAIL: free mem %ld (bytes) instead of %ld\n", n-PGSIZE, info.freemem);
    exit(1);
  }

  printf("[testmem] freeing one page...\n");
  
  if((uint64)sbrk(-PGSIZE) == 0xffffffffffffffff){
    printf("sbrk failed");
    exit(1);
  }

  sinfo(&info);
  printf("[testmem] after free = %ld (expected %ld)\n", info.freemem, n);
    
  if (info.freemem != n) {
    printf("FAIL: free mem %ld (bytes) instead of %ld\n", n, info.freemem);
    exit(1);
  }
}

void
testcall() {
  struct sysinfo info;
  
  if (sysinfo(&info) < 0) {
    printf("FAIL: sysinfo failed\n");
    exit(1);
  }

  if (sysinfo((struct sysinfo *) 0xeaeb0b5b00002f5e) !=  0xffffffffffffffff) {
    printf("FAIL: sysinfo succeeded with bad argument\n");
    exit(1);
  }
}

void testproc() {
  struct sysinfo info;
  uint64 nproc;
  int status;
  int pid;
  
  sinfo(&info);
  nproc = info.nproc;

  printf("[testproc] initial nproc = %ld\n", nproc);

  pid = fork();
  if(pid < 0){
    printf("sysinfotest: fork failed\n");
    exit(1);
  }
  if(pid == 0){
    sinfo(&info);
    if(info.nproc != nproc+1) {
      printf("sysinfotest: FAIL nproc is %ld instead of %ld\n", info.nproc, nproc+1);
      exit(1);
    }
    exit(0);
  }
  wait(&status);
  sinfo(&info);
  printf("[testproc-parent] nproc = %ld (expected %ld)\n", info.nproc, nproc);

  if(info.nproc != nproc) {
      printf("sysinfotest: FAIL nproc is %ld instead of %ld\n", info.nproc, nproc);
      exit(1);
  }
}

void testbad() {
  int pid = fork();
  int xstatus;
  
  if(pid < 0){
    printf("sysinfotest: fork failed\n");
    exit(1);
  }
  if(pid == 0){
      sinfo(0x0);
      exit(0);
  }
  wait(&xstatus);
  if(xstatus == -1)  // kernel killed child?
    exit(0);
  else {
    printf("sysinfotest: testbad succeeded %d\n", xstatus);
    exit(xstatus);
  }
}

int
main(int argc, char *argv[])
{
  struct sysinfo info;

  printf("===== sysinfotest: start =====\n");

  printf("\n[Running testcall...]\n");
  testcall();

  printf("\n[Running testmem...]\n");
  testmem();

  printf("\n[Running testproc...]\n");
  testproc();

  printf("\n[Running testfiles...]\n");
  testfiles();

  if (sysinfo(&info) < 0) {
    printf("FAIL: sysinfo failed at end\n");
    exit(1);
  }
  printf("\n[Final]\n");
  printf("Free memory: %ld\n", info.freemem);
  printf("Number of processes: %ld\n", info.nproc);
  printf("Number of open files: %ld\n", info.nopenfiles);

  printf("\n===== sysinfotest: OK =====\n");

  exit(0);
}
