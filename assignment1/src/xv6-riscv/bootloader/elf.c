#include "types.h"
#include "param.h"
#include "layout.h"
#include "riscv.h"
#include "defs.h"
#include "buf.h"
#include "elf.h"

#include <stdbool.h>

struct elfhdr* kernel_elfhdr;
struct proghdr* kernel_phdr;

uint64 find_kernel_load_addr(enum kernel ktype) {
    /* CSE 536: Get kernel load address from headers */
    uint64 result_val;
    uint64 addr_disk;
    if(ktype == NORMAL){
	addr_disk = RAMDISK;
   }
    else{
   	addr_disk = 0x84500000; 
    }
       kernel_elfhdr = (struct elfhdr*)addr_disk;
    
       kernel_phdr = (struct proghdr*)(addr_disk + kernel_elfhdr->phoff);
       kernel_phdr = (struct proghdr*)((char *)kernel_phdr + kernel_elfhdr->phentsize);
       result_val =  kernel_phdr->vaddr;
 

    return result_val;
}

uint64 find_kernel_size(enum kernel ktype) {
    /* CSE 536: Get kernel binary size from headers */
    uint64 kernel_size = kernel_elfhdr->shoff + (kernel_elfhdr->shentsize * kernel_elfhdr->shnum);
    return kernel_size;
}

uint64 find_kernel_entry_addr(enum kernel ktype) {
    /* CSE 536: Get kernel entry point from headers */
    
    return kernel_elfhdr->entry;
}
