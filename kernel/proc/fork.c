#include "types.h"
#include "globals.h"
#include "errno.h"

#include "util/debug.h"
#include "util/string.h"

#include "proc/proc.h"
#include "proc/kthread.h"

#include "mm/mm.h"
#include "mm/mman.h"
#include "mm/page.h"
#include "mm/pframe.h"
#include "mm/mmobj.h"
#include "mm/pagetable.h"
#include "mm/tlb.h"

#include "fs/file.h"
#include "fs/vnode.h"

#include "vm/shadow.h"
#include "vm/vmmap.h"

#include "api/exec.h"

#include "main/interrupt.h"

/* Pushes the appropriate things onto the kernel stack of a newly forked thread
 * so that it can begin execution in userland_entry.
 * regs: registers the new thread should have on execution
 * kstack: location of the new thread's kernel stack
 * Returns the new stack pointer on success. */
static uint32_t
fork_setup_stack(const regs_t *regs, void *kstack)
{
        /* Pointer argument and dummy return address, and userland dummy return
         * address */
        uint32_t esp = ((uint32_t) kstack) + DEFAULT_STACK_SIZE - (sizeof(regs_t) + 12);
        *(void **)(esp + 4) = (void *)(esp + 8); /* Set the argument to point to location of struct on stack */
        memcpy((void *)(esp + 8), regs, sizeof(regs_t)); /* Copy over struct */
        return esp;
}

void
proc_destroy(proc_t *proc)
{
    KASSERT(proc);

    /*p_pagedir*/
    pt_destroy_pagedir(proc->p_pagedir);

    /*no need to deal with p_files: they are all NULL pointers*/

    /*p_cwd*/
    if (proc->p_cwd) {
        vput(proc->p_cwd);
    }

    /*p_vmmap will be handled just after proc_create*/
}

void
vmmap_shadow(vmmap_t *newmap, vmmap_t *oldmap)
{
    KASSERT(newmap);
    KASSERT(oldmap);

    vmarea_t *oldarea;
    list_iterate_begin(&oldmap->vmm_list, oldarea, vmarea_t, vma_plink) {
        vmarea_t *newarea;
        newarea = vmmap_lookup(oldmap, newarea->vma_start);
        if (oldarea->vma_flags & MAP_SHARED) {
            KASSERT(newarea->vma_flags & MAP_SHARED);
            continue;
        }
        KASSERT(newarea->vma_flags & MAP_PRIVATE);
        KASSERT(oldarea->vma_flags & MAP_PRIVATE);
        KASSERT(newarea->vma_obj == oldarea->vma_obj);

        mmobj_t *shadowed = newarea->vma_obj;
        mmobj_t *bottom = mmobj_bottom_obj(shadowed);
        mmobj_t *newshadow = shadow_create();
        KASSERT(newshadow);
        mmobj_t *oldshadow = shadow_create();
        KASSERT(oldshadow);

        newshadow->mmo_shadowed = shadowed;
        newshadow->mmo_un.mmo_bottom_obj = bottom;
        bottom->mmo_ops->ref(bottom);
        oldshadow->mmo_shadowed = shadowed;
        oldshadow->mmo_un.mmo_bottom_obj = bottom;
        bottom->mmo_ops->ref(bottom);

        newarea->vma_obj = newshadow;
        oldarea->vma_obj = oldshadow;
    } list_iterate_end();
}

/*
 * The implementation of fork(2). Once this works,
 * you're practically home free. This is what the
 * entirety of Weenix has been leading up to.
 * Go forth and conquer.
 */
int
do_fork(struct regs *regs)
{
    /*bulletin 2*/
    vmmap_t *newmap = vmmap_clone(curproc->p_vmmap);
    if (newmap == NULL) {
        return -ENOMEM;
    }

    /*bulletin 3*/
    vmmap_shadow(newmap, curproc->p_vmmap);

    /*bulletin 4*/
    /*not sure which pagetable to flush*/
    pagedir_t *pagedir = pt_get();
    pt_unmap_range(pagedir, USER_MEM_LOW, USER_MEM_HIGH);
    tlb_flush_all();

    /*bulletin 1*/
    proc_t *newproc = proc_create(curproc->p_comm);
    KASSERT(newproc != NULL);

    /*not gonna use the vmmap created during proc_create*/
    vmmap_destroy(newproc->p_vmmap);
        NOT_YET_IMPLEMENTED("VM: do_fork");
        return 0;
}
