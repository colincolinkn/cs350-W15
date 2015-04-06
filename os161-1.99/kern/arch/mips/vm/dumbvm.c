/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>
#include "opt-A3.h"
/*
 * Dumb MIPS-only "VM system" that is intended to only be just barely
 * enough to struggle off the ground. You should replace all of this
 * code while doing the VM assignment. In fact, starting in that
 * assignment, this file is not included in your kernel!
 */

/* under dumbvm, always have 48k of user stack */
#define DUMBVM_STACKPAGES    1
#if OPT_A3
#define PAGE_SIZE 4096
paddr_t lo;
paddr_t hi;
int table_size = 0;
bool stealMem = 1;
struct CoreEntry {
	paddr_t cm_addr;
	int cm_blocks;
	bool cm_valid;
};
struct CoreEntry* core_map;
#endif /* OPT_A3 */
/*
 * Wrap rma_stealmem in a spinlock.
 */
static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;

void
vm_bootstrap(void)
{
	#if OPT_A3 
	ram_getsize(&lo, &hi);
	table_size = (hi - lo)/PAGE_SIZE;
	int offset = (hi - lo)%PAGE_SIZE;
	lo += offset;
	if (hi - lo < table_size * sizeof(struct CoreEntry)) {
		panic("No memory for core map");
	}
	core_map = (struct CoreEntry *)PADDR_TO_KVADDR(lo);
	for (int i = 0; i < table_size; i++) {
		if ((unsigned)i*PAGE_SIZE>=(unsigned)table_size * sizeof(struct CoreEntry)) {
			core_map[i].cm_valid = 1;
		} else {
			core_map[i].cm_valid = 0;
		}
		core_map[i].cm_addr = lo + i * PAGE_SIZE;
		core_map[i].cm_blocks = 0;
	}
	stealMem = 0;
	#endif /* OPT_A3 */
	/* Do nothing. */
}

static
paddr_t
getppages(unsigned long npages)
{
	paddr_t addr;

	#if OPT_A3
	if (stealMem) {
		spinlock_acquire(&stealmem_lock);
		addr = ram_stealmem(npages);
		spinlock_release(&stealmem_lock);
	} else {
		int page_count = 0;
		for (int i = 0; i < table_size; i++) {
			if (core_map[i].cm_valid) {
				page_count++;
				if (page_count == (int)npages) {
					for (int j = 0;j < (int)npages; j++) {
						core_map[i-(int)j].cm_valid = 0;
					}
					core_map[i+1-npages].cm_blocks = npages;
					addr = core_map[i+1-npages].cm_addr;
					break;
				} 
			} else {
				page_count = 0;
			}
		} if (page_count < (int)npages) {
			return ENOMEM;
		}
	}
	#else
	spinlock_acquire(&stealmem_lock);
	addr = ram_stealmem(npages);
	spinlock_release(&stealmem_lock);
	#endif /* OPT_A3 */
	return addr;
}

/* Allocate/free some kernel-space virtual pages */
vaddr_t 
alloc_kpages(int npages)
{
	paddr_t pa;
	pa = getppages(npages);
	if (pa==0) {
		return 0;
	}
	return PADDR_TO_KVADDR(pa);
}

void 
free_kpages(vaddr_t addr)
{
	/* nothing - leak the memory. */
	#if OPT_A3
	int paddr = KVADDR_TO_PADDR(addr);
	int idx = (paddr-lo)/PAGE_SIZE;
	int npages = core_map[idx].cm_blocks;
	for (int i = 0; i < npages; i++) {
		core_map[i+idx].cm_valid = 1;
	}
	#endif /* OPT_A3*/
	(void)addr;
}

void
vm_tlbshootdown_all(void)
{
	panic("dumbvm tried to do tlb shootdown?!\n");
}

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
	panic("dumbvm tried to do tlb shootdown?!\n");
}

int
vm_fault(int faulttype, vaddr_t faultaddress)
{
	vaddr_t vbase1, vtop1, vbase2, vtop2, stackbase, stacktop;
	paddr_t paddr;
	int i;
	uint32_t ehi, elo;
	struct addrspace *as;
	int spl;

	faultaddress &= PAGE_FRAME;

	DEBUG(DB_VM, "dumbvm: fault: 0x%x\n", faultaddress);

	switch (faulttype) {
	    case VM_FAULT_READONLY:
		/* We always create pages read-write, so we can't get this */
		panic("dumbvm: got VM_FAULT_READONLY\n");
	    case VM_FAULT_READ:
	    case VM_FAULT_WRITE:
		break;
	    default:
		return EINVAL;
	}

	if (curproc == NULL) {
		/*
		 * No process. This is probably a kernel fault early
		 * in boot. Return EFAULT so as to panic instead of
		 * getting into an infinite faulting loop.
		 */
		return EFAULT;
	}

	as = curproc_getas();
	if (as == NULL) {
		/*
		 * No address space set up. This is probably also a
		 * kernel fault early in boot.
		 */
		return EFAULT;
	}

	/* Assert that the address space has been set up properly. */
	#if OPT_A3
	#else 
	KASSERT(as->as_vbase1 != 0);
	KASSERT(as->as_pbase1 != 0);
	KASSERT(as->as_npages1 != 0);
	KASSERT(as->as_vbase2 != 0);
	KASSERT(as->as_pbase2 != 0);
	KASSERT(as->as_npages2 != 0);
	KASSERT(as->as_stackpbase != 0);
	KASSERT((as->as_vbase1 & PAGE_FRAME) == as->as_vbase1);
	KASSERT((as->as_pbase1 & PAGE_FRAME) == as->as_pbase1);
	KASSERT((as->as_vbase2 & PAGE_FRAME) == as->as_vbase2);
	KASSERT((as->as_pbase2 & PAGE_FRAME) == as->as_pbase2);
	KASSERT((as->as_stackpbase & PAGE_FRAME) == as->as_stackpbase);
	#endif /* OPT_A3 */
	
	#if OPT_A3
	vbase1 = as->as_vbase1[0];
        vtop1 = vbase1 + as->as_npages1 * PAGE_SIZE;
        vbase2 = as->as_vbase2[0];
        vtop2 = vbase2 + as->as_npages2 * PAGE_SIZE;
	#else
	vbase1 = as->as_vbase1;
	vtop1 = vbase1 + as->as_npages1 * PAGE_SIZE;
	vbase2 = as->as_vbase2;
	vtop2 = vbase2 + as->as_npages2 * PAGE_SIZE;
	#endif /* OPT_A3 */
	stackbase = USERSTACK - DUMBVM_STACKPAGES * PAGE_SIZE;
	stacktop = USERSTACK;

	if (faultaddress >= vbase1 && faultaddress < vtop1) {
		#if OPT_A3
		paddr = as->as_pbase1[(faultaddress - vbase1)/PAGE_SIZE];
		#else
		paddr = (faultaddress - vbase1) + as->as_pbase1;
		#endif
	}
	else if (faultaddress >= vbase2 && faultaddress < vtop2) {
		#if OPT_A3
                paddr = as->as_pbase2[(faultaddress - vbase2)/PAGE_SIZE];
                #else
		paddr = (faultaddress - vbase2) + as->as_pbase2;
		#endif
	}
	else if (faultaddress >= stackbase && faultaddress < stacktop) {
		#if OPT_A3
                paddr = as->as_stackpbase[(faultaddress - stackbase)/PAGE_SIZE];
                #else
		paddr = (faultaddress - stackbase) + as->as_stackpbase;
		#endif
	}
	else {
		return EFAULT;
	}

	/* make sure it's page-aligned */
	KASSERT((paddr & PAGE_FRAME) == paddr);

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_read(&ehi, &elo, i);
		if (elo & TLBLO_VALID) {
			continue;
		}
		ehi = faultaddress;
                #if OPT_A3
		elo = paddr | TLBLO_VALID;
                if ((!as->as_loaded) || faultaddress < vbase1 || faultaddress >= vtop1) {
                	elo |= TLBLO_DIRTY;
                }
                #else
                elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
                #endif /* OPT_A3 */
		DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", faultaddress, paddr);
		tlb_write(ehi, elo, i);
		splx(spl);
		return 0;
	}
        
        #if OPT_A3
        tlb_read(&ehi, &elo, i);
        ehi = faultaddress;
        elo = paddr | TLBLO_VALID;
        if ((!as->as_loaded) || faultaddress < vbase1 || faultaddress >= vtop1) {
                        elo |= TLBLO_DIRTY;
                }
        DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", faultaddress, paddr);
        tlb_random(ehi, elo);
        splx(spl);
        return 0;
        #else
	kprintf("dumbvm: Ran out of TLB entries - cannot handle page fault\n");
	splx(spl);
	return EFAULT;
        #endif /* OPT_A3 */
}

struct addrspace *
as_create(void)
{
	struct addrspace *as = kmalloc(sizeof(struct addrspace));
	if (as==NULL) {
		return NULL;
	}
	#if OPT_A3
	as->as_vbase1 = NULL;
	as->as_pbase1 = NULL;
	as->as_npages1 = 0;
	as->as_vbase2 = NULL;
        as->as_pbase2 = NULL;
	as->as_npages2 = 0;
	as->as_stackpbase = NULL;	
        as->as_loaded = 0;
	#else
	as->as_vbase1 = 0;
	as->as_pbase1 = 0;
	as->as_npages1 = 0;
	as->as_vbase2 = 0;
	as->as_pbase2 = 0;
	as->as_npages2 = 0;
	as->as_stackpbase = 0;
	#endif /* OPT_A3 */

	return as;
}

void
as_destroy(struct addrspace *as)
{
	#if OPT_A3
	for (int i = 0; i < (int)as->as_npages1; i++) {
		free_kpages(PADDR_TO_KVADDR(as->as_pbase1[i]));
	}
	for (int i = 0; i < (int)as->as_npages2; i++) {
		free_kpages(PADDR_TO_KVADDR(as->as_pbase2[i]));
	}
	for (int i = 0; i< DUMBVM_STACKPAGES; i++) {
		free_kpages(PADDR_TO_KVADDR(as->as_stackpbase[i]));
	}
	kfree(as->as_vbase1);
	kfree(as->as_pbase1);
kfree(as->as_vbase2);
kfree(as->as_pbase2);
kfree(as->as_stackpbase);
	#endif
	kfree(as);
}

void
as_activate(void)
{
	int i, spl;
	struct addrspace *as;

	as = curproc_getas();
#ifdef UW
        /* Kernel threads don't have an address spaces to activate */
#endif
	if (as == NULL) {
		return;
	}

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	splx(spl);
}

void
as_deactivate(void)
{
	/* nothing */
}

int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t sz,
		 int readable, int writeable, int executable)
{
	size_t npages; 

	/* Align the region. First, the base... */
	sz += vaddr & ~(vaddr_t)PAGE_FRAME;
	vaddr &= PAGE_FRAME;

	/* ...and now the length. */
	sz = (sz + PAGE_SIZE - 1) & PAGE_FRAME;

	npages = sz / PAGE_SIZE;

	/* We don't use these - all pages are read-write */
	(void)readable;
	(void)writeable;
	(void)executable;

	#if OPT_A3
	if (as->as_vbase1 == NULL) {
                as->as_npages1 = npages;
		as->as_vbase1 = kmalloc(npages*sizeof(vaddr_t));
		for (int i = 0; i < (int)npages; i++) {
			as->as_vbase1[i] = vaddr + i * PAGE_SIZE;
		}
                return 0;
        }
	#else
	if (as->as_vbase1 == 0) {
		as->as_vbase1 = vaddr;
		as->as_npages1 = npages;
		return 0;
	}
	#endif /* OPT_A3 */
	
	#if OPT_A3
        if (as->as_vbase2 == NULL) {
                as->as_npages2 = npages;
                as->as_vbase2 = kmalloc(npages*sizeof(vaddr_t));
                for (int i = 0; i < (int)npages; i++) {
                        as->as_vbase2[i] = vaddr + i * PAGE_SIZE;
                }
                return 0;
        }
        #else
	if (as->as_vbase2 == 0) {
		as->as_vbase2 = vaddr;
		as->as_npages2 = npages;
		return 0;
	}
	#endif /* OPT_A3 */

	/*
	 * Support for more than two regions is not available.
	 */
	kprintf("dumbvm: Warning: too many regions\n");
	return EUNIMP;
}

static
void
as_zero_region(paddr_t paddr, unsigned npages)
{
	bzero((void *)PADDR_TO_KVADDR(paddr), npages * PAGE_SIZE);
}

int
as_prepare_load(struct addrspace *as)
{
	/*KASSERT(as->as_pbase1 == 0);
	KASSERT(as->as_pbase2 == 0);
	KASSERT(as->as_stackpbase == 0);*/
	

	#if OPT_A3
	as->as_pbase1 = kmalloc(as->as_npages1 * sizeof(paddr_t));
	if (as->as_pbase1 == NULL) {
		return ENOMEM;
	}
	for (int i = 0; i < (int)as->as_npages1; i++) {
		as->as_pbase1[i] = getppages(1);
		as_zero_region(as->as_pbase1[i], 1);
	}
	#else
	as->as_pbase1 = getppages(as->as_npages1);
	if (as->as_pbase1 == 0) {
		return ENOMEM;
	}
	#endif
	
	#if OPT_A3
        as->as_pbase2 = kmalloc(as->as_npages2 * sizeof(paddr_t));
        if (as->as_pbase2 == NULL) {
                return ENOMEM;
        }
        for (int i = 0; i < (int)as->as_npages2; i++) {
                as->as_pbase2[i] = getppages(1);
		as_zero_region(as->as_pbase2[i], 1);
        }
        #else
	as->as_pbase2 = getppages(as->as_npages2);
	if (as->as_pbase2 == 0) {
		return ENOMEM;
	}
	#endif

	#if OPT_A3
        as->as_stackpbase = kmalloc(DUMBVM_STACKPAGES * sizeof(paddr_t));
        if (as->as_stackpbase == NULL) {
                return ENOMEM;
        }
        for (int i = 0; i < (int)DUMBVM_STACKPAGES; i++) {
                as->as_stackpbase[i] = getppages(1);
		as_zero_region(as->as_stackpbase[i], 1);
        }
        #else
	as->as_stackpbase = getppages(DUMBVM_STACKPAGES);
	if (as->as_stackpbase == 0) {
		return ENOMEM;
	}
	
	as_zero_region(as->as_pbase1, as->as_npages1);
	as_zero_region(as->as_pbase2, as->as_npages2);
	as_zero_region(as->as_stackpbase, DUMBVM_STACKPAGES);
	#endif
	return 0;
}

int
as_complete_load(struct addrspace *as)
{
        #if OPT_A3
        as->as_loaded = 1;
        #else
	(void)as;
        #endif /* OPT_A3 */
	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	KASSERT(as->as_stackpbase != 0);

	*stackptr = USERSTACK;
	return 0;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *new;

	new = as_create();
	if (new==NULL) {
		return ENOMEM;
	}	
	
	#if OPT_A3
	new->as_npages1 = old->as_npages1;
	new->as_vbase1 = kmalloc(new->as_npages1 * sizeof(vaddr_t));
	for (int i = 0; i < (int)new->as_npages1; i++) {
		new->as_vbase1[i] = old->as_vbase1[i];
	}

	new->as_npages2 = old->as_npages2;
        new->as_vbase2 = kmalloc(new->as_npages2 * sizeof(vaddr_t));
        for (int i = 0; i < (int)new->as_npages2; i++) {
                new->as_vbase2[i] = old->as_vbase2[i];
        }

        new->as_stackpbase = kmalloc(DUMBVM_STACKPAGES * sizeof(vaddr_t));
        for (int i = 0; i < (int)DUMBVM_STACKPAGES; i++) {
                new->as_stackpbase[i] = old->as_stackpbase[i];
        }
	#else
	new->as_vbase1 = old->as_vbase1;
	new->as_npages1 = old->as_npages1;
	new->as_vbase2 = old->as_vbase2;
	new->as_npages2 = old->as_npages2;
	#endif
	/* (Mis)use as_prepare_load to allocate some physical memory. */
	if (as_prepare_load(new)) {
		as_destroy(new);
		return ENOMEM;
	}
	
	#if OPT_A3
	#else
	KASSERT(new->as_pbase1 != 0);
	KASSERT(new->as_pbase2 != 0);
	KASSERT(new->as_stackpbase != 0);
	#endif /*OPT_A3*/

	#if OPT_A3
	for (int i = 0; i < (int)new->as_npages1; i++) {
		memmove((void *)PADDR_TO_KVADDR(new->as_pbase1[i]),
                (const void *)PADDR_TO_KVADDR(old->as_pbase1[i]),
                PAGE_SIZE);
	}
	#else
	memmove((void *)PADDR_TO_KVADDR(new->as_pbase1),
		(const void *)PADDR_TO_KVADDR(old->as_pbase1),
		old->as_npages1*PAGE_SIZE);
	#endif /*OPT_A3*/

        #if OPT_A3
        for (int i = 0; i < (int)new->as_npages2; i++) {
                memmove((void *)PADDR_TO_KVADDR(new->as_pbase2[i]),
                (const void *)PADDR_TO_KVADDR(old->as_pbase2[i]),
                PAGE_SIZE);
        }
        #else
	memmove((void *)PADDR_TO_KVADDR(new->as_pbase2),
		(const void *)PADDR_TO_KVADDR(old->as_pbase2),
		old->as_npages2*PAGE_SIZE);
	#endif /*OPT_A3*/

        #if OPT_A3
        for (int i = 0; i < (int)DUMBVM_STACKPAGES; i++) {
                memmove((void *)PADDR_TO_KVADDR(new->as_stackpbase[i]),
                (const void *)PADDR_TO_KVADDR(old->as_stackpbase[i]),
                PAGE_SIZE);
        }
        #else
	memmove((void *)PADDR_TO_KVADDR(new->as_stackpbase),
		(const void *)PADDR_TO_KVADDR(old->as_stackpbase),
		DUMBVM_STACKPAGES*PAGE_SIZE);
	#endif /*OPT_A3*/
	*ret = new;
	return 0;
}
