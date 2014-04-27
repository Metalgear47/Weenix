#include "types.h"
#include "globals.h"
#include "kernel.h"
#include "errno.h"

#include "util/debug.h"

#include "proc/proc.h"

#include "mm/mm.h"
#include "mm/mman.h"
#include "mm/page.h"
#include "mm/mmobj.h"
#include "mm/pframe.h"
#include "mm/pagetable.h"

#include "vm/pagefault.h"
#include "vm/vmmap.h"

/*
 * This gets called by _pt_fault_handler in mm/pagetable.c The
 * calling function has already done a lot of error checking for
 * us. In particular it has checked that we are not page faulting
 * while in kernel mode. Make sure you understand why an
 * unexpected page fault in kernel mode is bad in Weenix. You
 * should probably read the _pt_fault_handler function to get a
 * sense of what it is doing.
 *
 * Before you can do anything you need to find the vmarea that
 * contains the address that was faulted on. Make sure to check
 * the permissions on the area to see if the process has
 * permission to do [cause]. If either of these checks does not
 * pass kill the offending process, setting its exit status to
 * EFAULT (normally we would send the SIGSEGV signal, however
 * Weenix does not support signals).
 *
 * Now it is time to find the correct page (don't forget
 * about shadow objects, especially copy-on-write magic!). Make
 * sure that if the user writes to the page it will be handled
 * correctly.
 *
 * Finally call pt_map to have the new mapping placed into the
 * appropriate page table.
 *
 * @param vaddr the address that was accessed to cause the fault
 *
 * @param cause this is the type of operation on the memory
 *              address which caused the fault, possible values
 *              can be found in pagefault.h
 */
void
handle_pagefault(uintptr_t vaddr, uint32_t cause)
{
    dbg(DBG_MM, "vaddr is %#.8x, cause is %u\n", vaddr, cause);

    int pagenum = ADDR_TO_PN(vaddr);
    vmarea_t *area = vmmap_lookup(curproc->p_vmmap, pagenum);
    if (area == NULL) {
        proc_kill(curproc, EFAULT);
    }

    int forwrite = 0;

    if (cause == PROT_NONE) {
        panic("Really?PROT is none? Have no idea what to do\n");
    }

    /*
     *if ((cause & PROT_READ) == 0) {
     *    panic("no PROT_READ, what the heck?\n");
     *    do_exit(EFAULT);
     *}
     */

    if (cause & FAULT_WRITE) {
        if ((area->vma_prot & PROT_WRITE) == 0) {
            do_exit(EFAULT);
        }
        forwrite = 1;
    }

    if (cause & FAULT_EXEC) {
        if ((area->vma_prot & PROT_EXEC) == 0) {
            do_exit(EFAULT);
        }
    }

    KASSERT(area->vma_obj);
    pframe_t *pf = NULL;
    int err = area->vma_obj->mmo_ops->lookuppage(area->vma_obj, 
                pagenum - area->vma_start + area->vma_off, forwrite, &pf);

    /*TODO: redo it when shadow object is done*/

    KASSERT(err == 0);
    KASSERT(pf);
    KASSERT(pf->pf_addr);

    if (forwrite) {
        err = area->vma_obj->mmo_ops->dirtypage(area->vma_obj, pf);
        KASSERT(err == 0);
    }

    pagedir_t *pagedir = pt_get();

    KASSERT(PAGE_ALIGN_DOWN(vaddr) == PN_TO_ADDR(pagenum));
    err = pt_map(pagedir, (uintptr_t)PN_TO_ADDR(pagenum), 
            (uintptr_t)pf->pf_addr, 0x3f, 0x1ff);
    KASSERT(err == 0);


    /*tlb_flush(PAGE_ALIGN_DOWN(vaddr));*/

    /*panic("heck, panic for now\n");*/
        /*NOT_YET_IMPLEMENTED("VM: handle_pagefault");*/
}
