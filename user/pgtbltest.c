#include "kernel/param.h"
#include "kernel/fcntl.h"
#include "kernel/types.h"
#include "kernel/riscv.h"
#include "user/user.h"

#define N (8 * (1 << 20))

void print_pgtbl();
void print_kpgtbl();
void ugetpid_test();
void superpg_test();

void vmprint_test();
void pgaccess_test();

int
main(int argc, char *argv[])
{
  print_pgtbl();
  // ugetpid_test();
  print_kpgtbl();
  // superpg_test();
  vmprint_test();
  pgaccess_test();
  printf("pgtbltest: all tests succeeded\n");
  exit(0);
}

char *testname = "???";

void
err(char *why)
{
  printf("pgtbltest: %s failed: %s, pid=%d\n", testname, why, getpid());
  exit(1);
}

void
print_pte(uint64 va)
{
    pte_t pte = (pte_t) pgpte((void *) va);
    printf("va 0x%lx pte 0x%lx pa 0x%lx perm 0x%lx\n", va, pte, PTE2PA(pte), PTE_FLAGS(pte));
}

void
print_pgtbl()
{
  printf("print_pgtbl starting\n");
  for (uint64 i = 0; i < 10; i++) {
    print_pte(i * PGSIZE);
  }
  uint64 top = MAXVA/PGSIZE;
  for (uint64 i = top-10; i < top; i++) {
    print_pte(i * PGSIZE);
  }
  printf("print_pgtbl: OK\n");
}

void
ugetpid_test()
{
  int i;

  printf("ugetpid_test starting\n");
  testname = "ugetpid_test";

  for (i = 0; i < 64; i++) {
    int ret = fork();
    if (ret != 0) {
      wait(&ret);
      if (ret != 0)
        exit(1);
      continue;
    }
    if (getpid() != ugetpid())
      err("missmatched PID");
    exit(0);
  }
  printf("ugetpid_test: OK\n");
}

void
print_kpgtbl()
{
  printf("print_kpgtbl starting\n");
  kpgtbl();
  printf("print_kpgtbl: OK\n");
}


void
supercheck(uint64 s)
{
  pte_t last_pte = 0;

  for (uint64 p = s;  p < s + 512 * PGSIZE; p += PGSIZE) {
    pte_t pte = (pte_t) pgpte((void *) p);
    if(pte == 0)
      err("no pte");
    if ((uint64) last_pte != 0 && pte != last_pte) {
        err("pte different");
    }
    if((pte & PTE_V) == 0 || (pte & PTE_R) == 0 || (pte & PTE_W) == 0){
      err("pte wrong");
    }
    last_pte = pte;
  }

  for(int i = 0; i < 512; i += PGSIZE){
    *(int*)(s+i) = i;
  }

  for(int i = 0; i < 512; i += PGSIZE){
    if(*(int*)(s+i) != i)
      err("wrong value");
  }
}

void
superpg_test()
{
  int pid;
  
  printf("superpg_test starting\n");
  testname = "superpg_test";
  
  char *end = sbrk(N);
  if (end == 0 || end == (char*)0xffffffffffffffff)
    err("sbrk failed");
  
  uint64 s = SUPERPGROUNDUP((uint64) end);
  supercheck(s);
  if((pid = fork()) < 0) {
    err("fork");
  } else if(pid == 0) {
    supercheck(s);
    exit(0);
  } else {
    int status;
    wait(&status);
    if (status != 0) {
      exit(0);
    }
  }
  printf("superpg_test: OK\n");  
}

void
vmprint_test()
{
  printf("vmprint_test starting\n");
  testname = "vmprint_test";
 
  // ── Part A: Trigger vmprint via exec ─────────────────────────────────────
  // Fork so the exec doesn't replace *our* process.
  int pid = fork();
  if (pid < 0)
    err("fork failed");
 
  if (pid == 0) {
    // Child: exec a simple binary. The kernel's exec() will call vmprint()
    // just before returning, printing the child's new page table to the
    // console. "echo" is always present in xv6's file system.
    char *argv[] = { "echo", "vmprint_test_child", 0 };
    exec("echo", argv);
    // If exec returns, something went wrong.
    err("exec failed");
  }
 
  // Parent: wait for the child and check its exit status.
  int status;
  wait(&status);
  if (status != 0)
    err("child exited with non-zero status");
 
  // ── Part B: Sanity-check our own page table entries ───────────────────────
  //
  // The first few pages of every process hold its text and data. Each should
  // be mapped (pte != 0) and valid (PTE_V set). We check 8 pages starting
  // at virtual address 0.
  // Page 0 (text) must always be mapped — check it explicitly.
  pte_t pte0 = (pte_t)pgpte((void *)0);
  if (pte0 == 0)
    err("page 0 (text) not mapped");
  if ((pte0 & PTE_V) == 0)
    err("page 0 PTE missing PTE_V");
  if ((pte0 & PTE_R) == 0)
    err("page 0 PTE missing PTE_R");
 
  // Walk pages until we hit an unmapped one, verifying each mapped page
  // has PTE_V and PTE_R. We don't assume a fixed count because the number
  // of text+data pages depends on the binary size.
  int mapped = 0;
  for (int i = 0; i < 64; i++) {
    uint64 va = (uint64)i * PGSIZE;
    pte_t pte = (pte_t)pgpte((void *)va);
    if (pte == 0)
      break;   // reached the end of the mapped region — that's fine
    if ((pte & PTE_V) == 0)
      err("mapped page PTE missing PTE_V");
    if ((pte & PTE_R) == 0)
      err("mapped page PTE missing PTE_R");
    mapped++;
  }
  if (mapped == 0)
    err("no mapped pages found starting at VA 0");
 
  // ── Part C: Verify the trampoline page at the top of the address space ───
  //
  // xv6 maps the trampoline at MAXVA - PGSIZE in every process. It should be
  // valid, readable, and executable, but NOT user-accessible (no PTE_U) and
  // NOT writable (no PTE_W).
  uint64 trampoline_va = MAXVA - PGSIZE;
  pte_t tramp_pte = (pte_t)pgpte((void *)trampoline_va);
 
  if (tramp_pte == 0)
    err("trampoline page not mapped");
  if ((tramp_pte & PTE_V) == 0)
    err("trampoline PTE missing PTE_V");
  if ((tramp_pte & PTE_R) == 0)
    err("trampoline PTE missing PTE_R");
  if ((tramp_pte & PTE_X) == 0)
    err("trampoline PTE missing PTE_X");
  // Trampoline must NOT be user-accessible — it runs in supervisor mode.
  if ((tramp_pte & PTE_U) != 0)
    err("trampoline PTE has PTE_U set (should not be user-accessible)");
 
  // ── Part D: Verify the trapframe page (just below the trampoline) ─────────
  //
  // xv6 maps the trapframe at MAXVA - 2*PGSIZE. It holds saved registers
  // during traps. Should be R/W but not executable, and not user-accessible.
  uint64 trapframe_va = MAXVA - 2 * PGSIZE;
  pte_t tf_pte = (pte_t)pgpte((void *)trapframe_va);
 
  if (tf_pte == 0)
    err("trapframe page not mapped");
  if ((tf_pte & PTE_V) == 0)
    err("trapframe PTE missing PTE_V");
  if ((tf_pte & PTE_R) == 0)
    err("trapframe PTE missing PTE_R");
  if ((tf_pte & PTE_W) == 0)
    err("trapframe PTE missing PTE_W");
  if ((tf_pte & PTE_U) != 0)
    err("trapframe PTE has PTE_U set (should not be user-accessible)");
 
  printf("vmprint_test: OK\n");
}

void
pgaccess_test()
{
  printf("pgaccess_test starting\n");
  testname = "pgaccess_test";
 
  // Allocate a contiguous buffer large enough for 64 pages.
  // sbrk(n) grows the heap by n bytes and returns the old break (start of new
  // region). We need 64 pages so the max-count stress test fits.
  char *buf = sbrk(64 * PGSIZE);
  if (buf == (char *)-1)
    err("sbrk failed");
 
  uint64 mask;
 
  // ── Test 1: Basic detection ───────────────────────────────────────────────
  buf[0 * PGSIZE] = 1;   // write to page 0
  buf[2 * PGSIZE] = 1;   // write to page 2
  buf[5 * PGSIZE] = 1;   // write to page 5
 
  mask = 0;
  if (pgaccess(buf, 8, &mask) < 0)
    err("pgaccess syscall failed");
 
  if (mask != ((1 << 0) | (1 << 2) | (1 << 5)))
    err("test1: wrong bitmask for touched pages");
 
  // ── Test 2: Bit clearing ──────────────────────────────────────────────────
  mask = 0xdeadbeef;   // poison value — if pgaccess leaves it unchanged, we
                       // know the syscall isn't writing 0 properly
  if (pgaccess(buf, 8, &mask) < 0)
    err("pgaccess syscall failed (test2)");
 
  if (mask != 0)
    err("test2: A-bits not cleared after first pgaccess call");
 
  // ── Test 3: Only the written page appears ─────────────────────────────────
  buf[7 * PGSIZE] = 42;
 
  mask = 0;
  if (pgaccess(buf, 8, &mask) < 0)
    err("pgaccess syscall failed (test3)");
 
  if (mask != (1 << 7))
    err("test3: expected only bit 7 set");
 
  // ── Test 4: Read access is also detected (not just writes) ────────────────
  volatile char c = buf[3 * PGSIZE];   // 'volatile' prevents the compiler
  (void)c;                             // from optimising away the load
 
  mask = 0;
  if (pgaccess(buf, 8, &mask) < 0)
    err("pgaccess syscall failed (test4)");
 
  if ((mask & (1 << 3)) == 0)
    err("test4: read access not detected (PTE_A not set on load)");
 
  // ── Test 5: Untouched pages never appear ─────────────────────────────────
  mask = 0;
  if (pgaccess(buf, 8, &mask) < 0)
    err("pgaccess syscall failed (test5)");
 
  if (mask != 0)
    err("test5: untouched pages reported as accessed");
 
  // ── Test 6: Stress — max pages (64), all touched ─────────────────────────
  for (int i = 0; i < 64; i++)
    buf[i * PGSIZE] = (char)i;
 
  mask = 0;
  if (pgaccess(buf, 64, &mask) < 0)
    err("pgaccess syscall failed (test6)");
 
  // All 64 bits should be set.
  uint64 expected_all = (uint64)-1;   // all bits 1: 0xFFFFFFFFFFFFFFFF
  if (mask != expected_all)
    err("test6: not all bits set when all pages touched");
 
  // Confirm clearing works at scale too.
  mask = 0;
  if (pgaccess(buf, 64, &mask) < 0)
    err("pgaccess syscall failed (test6 clear check)");
  if (mask != 0)
    err("test6: A-bits not cleared after stress call");

    // ── Test 7: Fork isolation ────────────────────────────────────────────────
  buf[0 * PGSIZE] = 1;   // parent touches page 0 before fork
 
  int pid = fork();
  if (pid < 0)
    err("fork failed");
 
  if (pid == 0) {
    // ── Child ──
    // Touch pages 1 and 2 in the child's address space.
    buf[1 * PGSIZE] = 10;
    buf[2 * PGSIZE] = 20;
 
    mask = 0;
    if (pgaccess(buf, 4, &mask) < 0)
      exit(1);
 
    // Bits 1 and 2 must be set (child accessed them).
    if ((mask & (1 << 1)) == 0 || (mask & (1 << 2)) == 0)
      exit(1);
 
    // Bit 3 must NOT be set (neither parent nor child touched page 3).
    if (mask & (1 << 3))
      exit(1);
 
    exit(0);
  }
 
  // ── Parent ──
  int status;
  wait(&status);
  if (status != 0)
    err("test7: child reported incorrect pgaccess results after fork");
 
  // Parent touches pages 4 and 5 after the fork (child cannot see these).
  buf[4 * PGSIZE] = 77;
  buf[5 * PGSIZE] = 88;
 
  mask = 0;
  if (pgaccess(buf, 8, &mask) < 0)
    err("pgaccess syscall failed (test7 parent)");
 
  // Bits 4 and 5 must be set in the parent.
  if ((mask & (1 << 4)) == 0 || (mask & (1 << 5)) == 0)
    err("test7: parent post-fork accesses not detected");
 
  // Bits 6 and 7 must not be set (neither process touched them).
  if (mask & ((1 << 6) | (1 << 7)))
    err("test7: bits 6/7 spuriously set in parent");
 
  printf("pgaccess_test: OK\n");
}

