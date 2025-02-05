/* These files have been taken from the open-source xv6 Operating System codebase (MIT License).  */

#include "types.h"
#include "param.h"
#include "layout.h"
#include "riscv.h"
#include "defs.h"
#include "buf.h"
#include "measurements.h"
#include <stdbool.h>

void main();
void timerinit();

/* entry.S needs one stack per CPU */
__attribute__ ((aligned (16))) char bl_stack[STSIZE * NCPU];

/* Context (SHA-256) for secure boot */
SHA256_CTX sha256_ctx;

/* Structure to collects system information */
struct sys_info {
  /* Bootloader binary addresses */
  uint64 bl_start;
  uint64 bl_end;
  /* Accessible DRAM addresses (excluding bootloader) */
  uint64 dr_start;
  uint64 dr_end;
  /* Kernel SHA-256 hashes */
  BYTE expected_kernel_measurement[32];
  BYTE observed_kernel_measurement[32];
};
struct sys_info* sys_info_ptr;

extern void _entry(void);
void panic(char *s)
{
  for(;;)
    ;
}

/* CSE 536: Boot into the RECOVERY kernel instead of NORMAL kernel
 * when hash verification fails. */
void setup_recovery_kernel(void) {

  uint64 kernel_load_addr       = find_kernel_load_addr(RECOVERY);
  uint64 kernel_binary_size     = find_kernel_size(RECOVERY);

  int block_num = (kernel_binary_size+BSIZE-1)/BSIZE;
  int block_to_skip = 4096/BSIZE;
  uint64 *load_addr = (uint64 *)kernel_load_addr;

  struct buf b;
  b.disk = 1;

  for(int i = block_to_skip; i<block_num; i++){
     b.valid = 0;
     b.blockno = i;

     kernel_copy(RECOVERY, &b);
     if(b.valid == 1){
        memmove((char *)load_addr + ((i-block_to_skip)*BSIZE), b.data, BSIZE);
     }
  }
  uint64 kernel_entry = find_kernel_entry_addr(RECOVERY);
  w_mepc(kernel_entry);

}

/* CSE 536: Function verifies if NORMAL kernel is expected or tampered. */
bool is_secure_boot(void) {
  bool verification = true;
   
  /* Read the binary and update the observed measurement 
   * (simplified template provided below) */
 
  sha256_init(&sha256_ctx);
 
  uint64 kernel_size = find_kernel_size(NORMAL);
  
  sha256_update(&sha256_ctx, (const BYTE*) RAMDISK, kernel_size);
    
  sha256_final(&sha256_ctx, sys_info_ptr->observed_kernel_measurement);
  memmove(sys_info_ptr->expected_kernel_measurement, trusted_kernel_hash, 32);  
  verification = (memcmp(sys_info_ptr->observed_kernel_measurement, trusted_kernel_hash, 32)==0);

  /* Three more tasks required below: 
   *  1. Compare observed measurement with expected hash
   *  2. Setup the recovery kernel if comparison fails
   *  3. Copy expected kernel hash to the system information table */
  if (!verification)
    setup_recovery_kernel();
  
  return verification;
}

// entry.S jumps here in machine mode on stack0.
void start()
{
  /* CSE 536: Define the system information table's location. */
  sys_info_ptr = (struct sys_info*) 0x80080000;

  // keep each CPU's hartid in its tp register, for cpuid().
  int id = r_mhartid();
  w_tp(id);

  // set M Previous Privilege mode to Supervisor, for mret.
  unsigned long x = r_mstatus();
  x &= ~MSTATUS_MPP_MASK;
  x |= MSTATUS_MPP_S;
  w_mstatus(x);

  // disable paging
  w_satp(0);

  /* CSE 536: Unless kernelpmp[1-2] booted, allow all memory 
   * regions to be accessed in S-mode. */ 
  #if !defined(KERNELPMP1) || !defined(KERNELPMP2)
    w_pmpaddr0(0x3fffffffffffffull);
    w_pmpcfg0(0xf);
  #endif
  uint64 bootloader_start = 0x80000000;
  uint64 top_addr = bootloader_start + (117*BSIZE*BSIZE);
  top_addr = top_addr >> 2;

    /* CSE 536: With kernelpmp1, isolate upper 10MBs using TOR */ 
  #if defined(KERNELPMP1)
    uint64 pmpcfg0_val = 1<<0 | 1<<1 | 1<<2 | 1<<3;

    w_pmpaddr0(top_addr);
    w_pmpcfg0(pmpcfg0_val);
  #endif
  /* CSE 536: With kernelpmp2, isolate 118-120 MB and 122-126 MB using NAPOT */ 
  #if defined(KERNELPMP2)
    uint64 tor_addr = 0x80000000 + ((118*BSIZE*BSIZE)-1);
    tor_addr = tor_addr >> 2;
    w_pmpaddr0(tor_addr);//0-118 MB
    w_pmpaddr1(0x21DBFFFF);//118-120 MB

    w_pmpaddr2(0x21E3FFFF);//120-122 MBB
 
    w_pmpaddr3(0x21EFFFFF);//122-126 MB
    w_pmpaddr4(0x21FBFFFF);

    uint64 pmpcfg_val = (1<<0 | 1<<1 | 1<<2 | 1<<3 ) | ((uint64)0x83 << 8) | ((uint64)0x1f << 16) | ((uint64)0x83 << 24) | ((uint64)0x1f << 32);
    
    w_pmpcfg0(pmpcfg_val);


  #endif
  uint64  kernel_load_addr = find_kernel_load_addr(NORMAL);  
 /* CSE 536: Verify if the kernel is untampered for ecure boot */
  if (!is_secure_boot()) {
     
    goto out;
  }
  
  /* CSE 536: Load the NORMAL kernel binary (assuming secure boot passed). */
  uint64 kernel_binary_size     = find_kernel_size(NORMAL);
 
  int block_num = (kernel_binary_size+BSIZE-1)/BSIZE; 
  int block_to_skip = 4096/BSIZE;
  uint64 *load_addr = (uint64 *)kernel_load_addr;

  struct buf b;
  b.disk = 1;

  for(int i = block_to_skip; i<block_num; i++){
     b.valid = 0;
     b.blockno = i;

     kernel_copy(NORMAL, &b);
     if(b.valid == 1){
        memmove((char *)load_addr + ((i-block_to_skip)*BSIZE), b.data, BSIZE);
     }
  }
  

  uint64 kernel_entry = find_kernel_entry_addr(NORMAL);
  
  /* CSE 536: Write the correct kernel entry point */
  w_mepc( kernel_entry);

  //extern uint64 end;
 
out:
  /* CSE 536: Provide system information to the kernel. */
  sys_info_ptr = (struct sys_info*) 0x80080000;
  sys_info_ptr->bl_start = 0x80000000;
  sys_info_ptr->bl_end = end;
  sys_info_ptr->dr_start = sys_info_ptr->bl_start;
  sys_info_ptr->dr_end = 0x80000000 + 128 * 1024 * 1024; 
  /* CSE 536: Send the observed hash value to the kernel (using sys_info_ptr) */

  // delegate all interrupts and exceptions to supervisor mode.
  w_medeleg(0xffff);
  w_mideleg(0xffff);
  w_sie(r_sie() | SIE_SEIE | SIE_STIE | SIE_SSIE);

  // return address fix
  uint64 addr = (uint64) panic;
  asm volatile("mv ra, %0" : : "r" (addr));

  // switch to supervisor mode and jump to main().
  asm volatile("mret");
}
