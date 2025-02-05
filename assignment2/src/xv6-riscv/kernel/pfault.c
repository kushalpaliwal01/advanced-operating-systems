#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "elf.h"

#include "sleeplock.h"
#include "fs.h"
#include "buf.h"

int loadseg(pagetable_t pagetable, uint64 va, struct inode *ip, uint offset, uint sz);
int flags2perm(int flags);

/* CSE 536: (2.4) read current time. */
uint64 read_current_timestamp() {
  uint64 curticks = 0;
  acquire(&tickslock);
  curticks = ticks;
  wakeup(&ticks);
  release(&tickslock);
  return curticks;
}

bool psa_tracker[PSASIZE];

/* All blocks are free during initialization. */
void init_psa_regions(void)
{
    for (int i = 0; i < PSASIZE; i++) 
        psa_tracker[i] = false;
}

/* Evict heap page to disk when resident pages exceed limit */
void evict_page_to_disk(struct proc* p) {
    /* Find free block :*/
    int blockno = 0;
    for(int i=0; i<(PSASIZE-PSASTART); i++){
   	if(psa_tracker[i] == false){
 	   blockno = i;

 	   break;
        }
    }
     
    /* Find victim page using FIFO. */
    int victim_page_index = -1;
    for(int i=0; i<MAXHEAP; i++)
    {   
       if(p->heap_tracker[i].loaded == 0){
          if(victim_page_index == -1 || (p->heap_tracker[i].last_load_time < p->heap_tracker[victim_page_index].last_load_time)){      	    victim_page_index = i;
          }
       }
    }
    for(int i = blockno; i< (blockno)+4; i++)
    {
 	psa_tracker[i] = true;
    }
   
    uint64 victim_page_addr = p->heap_tracker[victim_page_index].addr;
    
    /* Set bool loaded to true for the victim page */
    p->heap_tracker[victim_page_index].loaded = 1;
    
    /* Set startblock field for the victim page*/
    p->heap_tracker[victim_page_index].startblock = blockno;
    char *kernel_page = kalloc();

    /* Print statement. */
    print_evict_page(victim_page_addr, blockno);
 
    /* Read memory from the user to kernel memory first. */
    copyin(p->pagetable, kernel_page, victim_page_addr, PGSIZE);
   
    /* Write to the disk blocks. Below is a template as to how this works. There is
      * definitely a better way but this works for now. :p */
 
    int count=0;
    for(uint64 i=0x4000; i<0x3eb000; i+=0x1000)
    {
	if(i==victim_page_addr)
	{
	   break;
	}
	count++;
    }
    struct buf* b;
    for(int i=0; i<4; i++)
    {
    	b = bread(1, PSASTART+blockno+i);
    	memmove(b->data, kernel_page+(1024*i), 1024);
    	bwrite(b);
    	brelse(b);
    }
      // printf("Working till memmove\n\n");
    

    /* Unmap swapped out page */
    uvmunmap(p->pagetable, victim_page_addr, 1, 0);

}

/* Retrieve faulted page from disk. */
void retrieve_page_from_disk(struct proc* p, uint64 uvaddr) {
    
     int blockno = -1;
     for(int i=0; i<MAXHEAP; i++)
     {
	if(p->heap_tracker[i].addr == uvaddr && p->heap_tracker[i].loaded == 1)
	{
	   blockno = p->heap_tracker[i].startblock;
	   p->heap_tracker[i].loaded = 0;
	}
     }
     if(blockno == -1)
	return;

	
 
     /*Kernel Page allocation*/  
     char *kernel_page = kalloc();

     struct buf* b;
     for(int i=0; i<4; i++)
     {
	int block_to_read = PSASTART+blockno+i;
	b = bread(1, block_to_read);
	memmove(kernel_page+(1024*i), b->data, 1024);
	brelse(b);
     }

    /* int* byteval = (int *)kernel_page;
     for(int i=0; i<1024; i++)
     {
	printf("%d-", byteval[i]); 
     }
     printf("\n");*/
    
     uvmalloc(p->pagetable, uvaddr, uvaddr+PGSIZE, PTE_W);
     copyout(p->pagetable, uvaddr, kernel_page, PGSIZE);
     print_retrieve_page(uvaddr, blockno);
     for(int i = blockno; i<blockno+4; i++)
     {
	psa_tracker[i] = false;
     }
    
}


void page_fault_handler(void) 
{
    /* Current process struct */
    struct proc *p = myproc();
   
    /* Track whether the heap page should be brought back from disk or not. */
    bool load_from_disk = true;

    int page_index = -1;
    /* Find faulting address. */
    uint64 faulting_addr = r_stval();
   
    uint64 aligned_addr = faulting_addr & ~(PGSIZE-1);
      
	
    for(int i =0; i<MAXHEAP; i++)
    {
       if(p->heap_tracker[i].addr == aligned_addr)
       {
	      load_from_disk = false;
              page_index = i;
	      goto heap_handle;
	      goto out;
       }
    }
  
    if(p->cow_enabled == 1 && r_scause() == 15)
    {
       pte_t *pte = walk(p->pagetable, aligned_addr, 0);
       if((*pte & PTE_V) && !(*pte & PTE_W))
       {
          print_page_fault(p->name, (r_stval() & ~(PGSIZE-1)));
          copy_on_write();
          goto out;
       }
    }
    
    if(load_from_disk)
    {     
       struct elfhdr elf;
       struct proghdr ph;
       struct inode *ip;
       pagetable_t pagetable = p->pagetable;
       begin_op();
       if((ip = namei(p->name))==0){
          panic("File not Found\n");
       }
    
       ilock(ip);
       readi(ip, 0, (uint64)&elf, 0, sizeof(elf));
    
       if(elf.magic != ELF_MAGIC)
          panic("Invalid ELF file\n");

       for(int i=0, off = elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){
          readi(ip, 0, (uint64)&ph, off, sizeof(ph));
          if(ph.type != ELF_PROG_LOAD){
             continue;
          }
      
     
          if(aligned_addr >= ph.vaddr && aligned_addr < (ph.vaddr+ph.memsz)){
             uint64 sz1;
	     if((sz1 = uvmalloc(pagetable, aligned_addr, aligned_addr+PGSIZE, flags2perm(ph.flags)))==0)
	        panic("Failed to allocate a page\n");
         
             if(loadseg(pagetable, aligned_addr, ip, ph.off+(aligned_addr - ph.vaddr), PGSIZE)<0)
	        panic("Failed to load segment\n");		
	 
     	     break;
          }
       }
       iunlockput(ip);
       end_op();
       ip =0;
       print_page_fault(p->name, aligned_addr);
       print_load_seg(aligned_addr, ph.off+(aligned_addr-ph.vaddr), PGSIZE);
    }
   
     // goto heap_handle;
    /* Go to out, since the remainder of this code is for the heap. */
    goto out;

heap_handle:

    print_page_fault(p->name, aligned_addr);
 
    /* 2.4: Check if resident pages are more than heap pages. If yes, evict. */
    if (p->resident_heap_pages == MAXRESHEAP) {
        evict_page_to_disk(p);
        p->resident_heap_pages -= 1;
    }
    
 
    if(p->heap_tracker[page_index].loaded == 1)
    {
	load_from_disk = true;
    } 
    /* 2.4: Heap page was swapped to disk previously. We must load it from disk. */
    if (load_from_disk) {
        retrieve_page_from_disk(p, aligned_addr);
    }
    else
    {
   /* 2.3: Map a heap page into the process' address space. (Hint: check growproc) */
    	uint64 sz = p->sz;
    	sz = uvmalloc(p->pagetable, sz, sz+PGSIZE, PTE_W);
    	p->sz = sz; 
    }	  


    /* 2.4: Update the last load time for the loaded heap page in p->heap_tracker. */

    for(int i =0; i< MAXHEAP; i++)
    { 
	   if(p->heap_tracker[i].addr == aligned_addr)
	   {  
	      p->heap_tracker[i].last_load_time = read_current_timestamp();
 	   }
    }	
  




      
       
   
    /* Track that another heap page has been brought into memory. */
    p->resident_heap_pages += 1;

   
out:
    /* Flush stale page table entries. This is important to always do. */
    sfence_vma();
    return;
}
