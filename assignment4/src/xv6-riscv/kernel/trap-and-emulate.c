#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
 
// Struct to keep VM registers (Sample; feel free to change.)
struct vm_reg {
    int     code;
    int     mode;
    uint64  val;
};

// Keep the virtual state of the VM's privileged registers
struct vm_virtual_state {

    // Supervisor trap setup
    struct vm_reg sstatus;
    struct vm_reg sie;
    struct vm_reg stvec;
    struct vm_reg scounteren;

    // Supervisor trap handling
    struct vm_reg sscratch;
    struct vm_reg sepc;
    struct vm_reg scause;
    struct vm_reg sip;
    struct vm_reg stval;

    // Supervisor page table register
    struct vm_reg satp;

    // Machine information registers
    struct vm_reg mvendroid;
    struct vm_reg marchid;
    struct vm_reg mimpid;
    struct vm_reg mhartid;
    struct vm_reg mconfigptr;

    // Machine trap setup registers
    struct vm_reg mstatus;
    struct vm_reg misa;
    struct vm_reg medeleg;
    struct vm_reg mideleg;
    struct vm_reg mie;
    struct vm_reg mtvec;
    struct vm_reg mcounteren;
    struct vm_reg mstatush;
 
    // Machine trap handling registers
    struct vm_reg mscratch;
    struct vm_reg mepc;
    struct vm_reg mcause;
    struct vm_reg mtval;
    struct vm_reg mip;
    struct vm_reg mtinst;
    struct vm_reg mtval2;
    
    struct vm_reg pmpcfg0;
    struct vm_reg pmpaddr[4];
    
    pagetable_t old_ptable;    
    pagetable_t new_ptable;

    int current_mode; 
    int pmp_configuration;   
};

struct vm_virtual_state vm_state;
/*
void uvmunmap_func(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  uint64 a;
  pte_t *pte;

  if((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");

  for(a = va; a < va + npages*PGSIZE; a += PGSIZE){
    if((pte = walk(pagetable, a, 0)) == 0){
      printf("uvmunmap: walk");
      continue;
    }
    if((*pte & PTE_V) == 0){
      printf("uvmunmap: not mapped");
      continue;
    }
    if(PTE_FLAGS(*pte) == PTE_V)
      panic("uvmunmap: not a leaf");
    if(do_free){
      uint64 pa = PTE2PA(*pte);
      kfree((void*)pa);
    }
    *pte = 0;
  }
}*/
void switch_page_table(struct proc *p){
     p->pagetable = vm_state.old_ptable;
}

void apply_pmp_restrictions(pagetable_t pagetable, uint64 start, uint64 end){
   for(uint64 i= start; i<end; i+=PGSIZE){
      uvmunmap(pagetable, i, 1, 0);
   }
}

// In your ECALL, add the following for prints
// struct proc* p = myproc();
// printf("(EC at %p)\n", p->trapframe->epc);
void copy_pagetable_region(pagetable_t old, pagetable_t new, uint64 start, uint64 end){


    pte_t *pte;
    uint64 pa;
    int flags;
    for(uint64 i=start; i<end; i += PGSIZE){
	
	if((pte = walk(old, i, 0))==0){
           panic("No mapping is present");
	}
	if((*pte & PTE_V)==0){
	   panic("");
	}
        pa = PTE2PA(*pte) ;
	flags = PTE_FLAGS(*pte);
	mappages(new, i, PGSIZE, pa, flags); 
    }
}


struct vm_reg *get_vm_reg(uint32 csr){
    struct vm_reg *base = (struct vm_reg*)&vm_state;
    int num_regs  = sizeof(vm_state) / sizeof(struct vm_reg);
    for(int i=0; i<num_regs; i++){
    	struct vm_reg *current =  base + i;
	if(current->code == csr){
	   return current;
	}
    }
    return NULL;
}



void print_reg(){
    struct vm_reg *base = (struct vm_reg*)&vm_state;
    int num_regs = sizeof(vm_state) / sizeof(struct vm_reg);
    for (int i=0; i<num_regs; i++){
	struct vm_reg *current = base + i;
	printf("Current register-> code: %x, value: %x\n", current->code, current->val);
    }
}

void handle_ecall(struct proc *p){
    if(vm_state.current_mode == 0){
    	struct vm_reg *sepc_reg = get_vm_reg(0x141);
	struct vm_reg *sstatus_reg = get_vm_reg(0x100);
	struct vm_reg *stvec_reg = get_vm_reg(0x105);
	sepc_reg->val = p->trapframe->epc;
	uint64 sstatus = sstatus_reg->val;
	sstatus |= (0 << 8);
	
	uint64 uie = (sstatus >> 0) & 0x1;
	sstatus |= (uie << 5);
        sstatus |= (0 << 1);
	
	vm_state.current_mode = 1;
	p->trapframe->epc = stvec_reg->val;
	
    }
    else if(vm_state.current_mode == 1){
	struct vm_reg *mepc_reg = get_vm_reg(0x341);
	struct vm_reg *mstatus_reg = get_vm_reg(0x300);
	struct vm_reg *mtvec_reg = get_vm_reg(0x305);
		
	mepc_reg->val = p->trapframe->epc;
	uint64 mstatus = mstatus_reg->val;
	mstatus |= (0x3 << 11);
	
	uint64 mie = (mstatus >> 3) & 0x1;
	mstatus |= (mie << 7);
	mstatus |= (0 << 3);
	
	vm_state.current_mode = 2;
	p->trapframe->epc = mtvec_reg->val;
	p->pagetable = vm_state.old_ptable;
    }
    else{
	setkilled(p);
	trap_and_emulate_init();
    }
}

void handle_sret(struct proc *p){
    if(vm_state.current_mode == 1)
    {
	struct vm_reg *sstatus_reg = get_vm_reg(0x100);
	struct vm_reg *sepc_reg = get_vm_reg(0x141);
	uint64 sstatus = sstatus_reg->val;
	uint64 spp = (sstatus >> 8) & 0x1;
	
	uint64 spie = (sstatus >> 5) & 0x1;
	sstatus |= (spie << 1);
	sstatus |= (1 << 5);
	if(spp < 1){
	   vm_state.current_mode = spp;
	}
	sstatus_reg->val = sstatus;
	p->trapframe->epc = sepc_reg->val;
    }
    else{
	p->pagetable = vm_state.old_ptable;
	setkilled(p);
	trap_and_emulate_init();
    }
}

void handle_mret(struct proc *p){
    if(vm_state.current_mode >= 2){

        struct vm_reg *mstatus_reg = get_vm_reg(0x300);
	struct vm_reg *mepc_reg = get_vm_reg(0x341);
	uint64 mstatus = mstatus_reg->val;
        uint64 mpp = (mstatus >> 11) & 0x3;
	
	uint64 mpie = (mstatus >> 7) & 0x1;
	mstatus |= (mpie << 3);
	mstatus |= (1 << 7);
	if(mpp<2)
	{
	   vm_state.current_mode = mpp;
	}	
	mstatus_reg->val = mstatus;
	p->trapframe->epc = mepc_reg->val;
	
        
    }
    else{
        setkilled(p);
	trap_and_emulate_init();
    }
    if(vm_state.pmp_configuration == 1)
    {
	uint64 start_addr = 0x80000000;
	uint64 end_addr = 0x80000000;

	vm_state.new_ptable = proc_pagetable(p);
	copy_pagetable_region(p->pagetable, vm_state.new_ptable, 0, p->sz);
	copy_pagetable_region(p->pagetable, vm_state.new_ptable, 0x80000000, 0x80400000);
       	uint64 pmpcfg_val = vm_state.pmpcfg0.val;

	for(int i=0; i<4; i++) 
	{  
	   if(vm_state.pmpaddr[i].val == 0)
	      break;
	   uint8 pmpcfg_entry = (pmpcfg_val >> (i*8)) & 0xFF;
	   uint8 rwx_bits = pmpcfg_entry & 0x07;
	   if(i>0){
	      start_addr = (uint64)(vm_state.pmpaddr[i-1].val) << 2 & 0xFFFFFFFFFFFFFFFF;
	   }
	 
	   

	   end_addr = (uint64)(vm_state.pmpaddr[i].val) << 2 & 0xFFFFFFFFFFFFFFFF;
	   if(rwx_bits != 0x07){
	      apply_pmp_restrictions(vm_state.new_ptable, start_addr, end_addr);
	   }
	
	  /* if(vm_state.pmpaddr[i].val == 0 && i-1>= 0){
	      end_addr = (uint64)(vm_state.pmpaddr[i-1].val) << 2 & 0xFFFFFFFFFFFFFFFF;
	      if(i-2>=0 && address_mode != 0x01){
		 start_addr = (uint64)(vm_state.pmpaddr[i-2].val) << 2 & 0xFFFFFFFFFFFFFFFF;
	      }
	      break; 
	   }*/
	}
       /* vm_state.new_ptable = proc_pagetable(p);
	copy_pagetable_region(p->pagetable, vm_state.new_ptable, 0, p->sz);
	copy_pagetable_region(p->pagetable, vm_state.new_ptable, 0x80000000, 0x80400000);
	apply_pmp_restrictions(vm_state.new_ptable, start_addr, end_addr);*/
	p->pagetable = vm_state.new_ptable;
    }
}

void handle_csrw(struct proc *p, uint32 rs1, uint32 uimm){
     
    uint64 *src_reg = &(p->trapframe->ra) + rs1 - 1;
    uint64 val = *src_reg;
    struct vm_reg *privileged_inst = get_vm_reg(uimm);
    if(privileged_inst != NULL && privileged_inst->mode <= vm_state.current_mode){
	privileged_inst->val = val;
    }
    else{
	setkilled(p);
        trap_and_emulate_init();
    }
    if(uimm == 0x3a0 ||uimm == 0x3b0 || uimm == 0x3b1 || uimm == 0x3b2 || uimm == 0x3b3){
	vm_state.pmp_configuration = 1;
    }
    p->trapframe->epc += 4;
    
}
void handle_csrr(struct proc *p, uint32 rd, uint32 uimm){
    //printf("In the corresponding function\n");
    struct vm_reg *privileged_inst = get_vm_reg(uimm);
    uint64 *dest_reg = &(p->trapframe->ra)+rd-1;
    if(privileged_inst->mode <= vm_state.current_mode)
    {
	*dest_reg = privileged_inst->val;
    }
    else
    {

	setkilled(p);
	trap_and_emulate_init();
    }
    p->trapframe->epc += 4;
    //w_sepc(p->trapframe->epc);
}

void trap_and_emulate(void) {
    /* Comes here when a VM tries to execute a supervisor instruction. */
    struct proc *p = myproc();
    if(!vm_state.old_ptable && p->pagetable){
    	vm_state.old_ptable = proc_pagetable(p);
	copy_pagetable_region(p->pagetable, vm_state.old_ptable, 0, p->sz);
	copy_pagetable_region(p->pagetable, vm_state.old_ptable, 0x80000000, 0x80400000);
    }
   
    /* Retrieve all required values from the instruction */
    uint64 addr     = r_sepc();
    uint64 paddr = walkaddr(p->pagetable, addr) | (addr & 0xFFF);
    uint64 instruction = *((uint32*)paddr);
    uint32 op       = instruction & ((1 << 7)-1);
    uint32 rd       = (instruction >> 7) & ((1<<5)-1);
    uint32 funct3   = (instruction >> 12) & ((1<<3)-1);
    uint32 rs1      = (instruction >> 15) & ((1<<5)-1);
    uint32 uimm     = (instruction >> 20) & ((1<<12)-1);

    switch(funct3){
	case 0x0:
	   if(uimm == 0x102){
	      /* Print the statement */
              printf("(PI at %p) op = %x, rd = %x, funct3 = %x, rs1 = %x, uimm = %x\n", 
                addr, op, rd, funct3, rs1, uimm);

	      handle_sret(p);
	   }
	   else if(uimm == 0x302){
              /* Print the statement */
              printf("(PI at %p) op = %x, rd = %x, funct3 = %x, rs1 = %x, uimm = %x\n", 
                addr, op, rd, funct3, rs1, uimm);

   	      handle_mret(p);
           }
	   else{
	      printf("(EC at %p)\n", addr);
	      handle_ecall(p);
   	   }
	   break;

	case 0x1:
           /* Print the statement */
           printf("(PI at %p) op = %x, rd = %x, funct3 = %x, rs1 = %x, uimm = %x\n", 
                addr, op, rd, funct3, rs1, uimm);
	   handle_csrw(p, rs1, uimm);	   
	   break;
	case 0x2:
           /* Print the statement */
           printf("(PI at %p) op = %x, rd = %x, funct3 = %x, rs1 = %x, uimm = %x\n", 
                addr, op, rd, funct3, rs1, uimm);

	   handle_csrr(p, rd, uimm);
	  // printf("Call CSR read function \n");
	   break;
    }
    //print_reg();

}


void trap_and_emulate_init(void) {
    /* Create and initialize all state for the VM */

    // Supervisor trap setup
    vm_state.sstatus = (struct vm_reg){.code = 0x100, .mode = 1, .val = 0};
    vm_state.sie = (struct vm_reg){.code = 0x104, .mode = 1, .val = 0};
    vm_state.stvec = (struct vm_reg){.code = 0x105, .mode = 1, .val = 0};   
    vm_state.scounteren = (struct vm_reg){.code = 0x106, .mode = 1, .val = 0};
 
    // Supervisor trap handling
    vm_state.sscratch = (struct vm_reg){.code = 0x140, .mode = 1, .val = 0}; 
    vm_state.sepc = (struct vm_reg){.code = 0x141, .mode = 1, .val = 0};
    vm_state.scause = (struct vm_reg){.code = 0x142, .mode = 1, .val = 0};
    vm_state.stval = (struct vm_reg){.code = 0x143, .mode = 1, .val = 0};
    vm_state.sip = (struct vm_reg){.code = 0x144, .mode = 1, .val = 0};
    
    // Supervisor page table register
    vm_state.satp = (struct vm_reg){.code = 0x180, .mode = 1, .val = 0};

    // Machine information registers
    vm_state.mvendroid = (struct vm_reg){.code = 0xf11, .mode = 2, .val = 0x637365353336};
    vm_state.marchid = (struct vm_reg){.code = 0xf12, .mode = 2, .val = 0};
    vm_state.mimpid = (struct vm_reg){.code = 0xf13, .mode = 2, .val = 0};
    vm_state.mhartid = (struct vm_reg){.code = 0xf14, .mode = 2, .val = 0};
    vm_state.mconfigptr = (struct vm_reg){.code = 0xf15, .mode = 2, .val = 0};


    // Machine trap setup registers
    vm_state.mstatus = (struct vm_reg){.code = 0x300, .mode = 2, .val = 0};
    vm_state.misa = (struct vm_reg){.code = 0x301, .mode = 2, .val = 0};
    vm_state.medeleg = (struct vm_reg){.code = 0x302, .mode = 2, .val = 0};
    vm_state.mideleg = (struct vm_reg){.code = 0x303, .mode = 2, .val = 0};
    vm_state.mie = (struct vm_reg){.code = 0x304, .mode = 2, .val = 0};
    vm_state.mtvec = (struct vm_reg){.code = 0x305, .mode = 2, .val = 0};
    vm_state.mcounteren = (struct vm_reg){.code = 0x306, .mode = 2, .val = 0};
    vm_state.mstatush = (struct vm_reg){.code = 0x310, .mode = 2, .val = 0};

 
    // Machine trap handling registers
    vm_state.mscratch = (struct vm_reg){.code = 0x340, .mode = 2, .val = 0};
    vm_state.mepc = (struct vm_reg){.code = 0x341, .mode = 2, .val = 0};
    vm_state.mcause = (struct vm_reg){.code = 0x342, .mode = 2, .val = 0};
    vm_state.mtval = (struct vm_reg){.code = 0x343, .mode = 2, .val = 0};
    vm_state.mip = (struct vm_reg){.code = 0x344, .mode = 2, .val = 0};
    vm_state.mtinst = (struct vm_reg){.code = 0x34A, .mode = 2, .val = 0};   
    vm_state.mtval2 = (struct vm_reg){.code = 0x34B, .mode = 2, .val = 0};
 
    vm_state.pmpcfg0 = (struct vm_reg){.code = 0x3a0, .mode = 0, .val = 0};
    vm_state.pmpaddr[0] = (struct vm_reg){.code = 0x3b0, .mode = 0, .val = 0};
    vm_state.pmpaddr[1] = (struct vm_reg){.code = 0x3b1, .mode = 0, .val = 0};
    vm_state.pmpaddr[2] = (struct vm_reg){.code = 0x3b2, .mode = 0, .val = 0};
    vm_state.pmpaddr[3] = (struct vm_reg){.code = 0x3b3, .mode = 0, .val = 0};

    vm_state.current_mode = 2; 
    vm_state.pmp_configuration = 0;
    vm_state.old_ptable = NULL;
    vm_state.new_ptable = NULL;

}
