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
        newarea = vmmap_lookup(newmap, oldarea->vma_start);
        if (oldarea->vma_flags & MAP_SHARED) {
            KASSERT(newarea->vma_flags & MAP_SHARED);
            continue;
        }
        KASSERT(newarea->vma_flags & MAP_PRIVATE);
        KASSERT(oldarea->vma_flags & MAP_PRIVATE);
        KASSERT(newarea->vma_obj == oldarea->vma_obj);
        KASSERT(newarea->vma_start == oldarea->vma_start);
        KASSERT(newarea->vma_end == oldarea->vma_end);
        KASSERT(newarea->vma_off == oldarea->vma_off);
        KASSERT(newarea->vma_prot == oldarea->vma_prot);
        KASSERT(newarea->vma_vmmap = newmap);
        KASSERT(oldarea->vma_vmmap = oldmap);

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
        newshadow->mmo_ops->ref(newshadow);
        oldarea->vma_obj = oldshadow;
        oldshadow->mmo_ops->ref(oldshadow);
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
    /*need to add to newproc's vmmap*/

    /*bulletin 4*/
    /*not sure which pagetable to flush*/
    pagedir_t *pagedir = pt_get();
    pt_unmap_range(pagedir, USER_MEM_LOW, USER_MEM_HIGH);
    tlb_flush_all();

    /*bulletin 1*/
    proc_t *newproc = proc_create(curproc->p_comm);
    /*bulletin 7 set up p_cwd is also handled by proc_create*/
    KASSERT(newproc != NULL);

    /*not gonna use the vmmap created during proc_create*/
    vmmap_destroy(newproc->p_vmmap);

    /*bulletin 6*/
    int i = 0;
    for (i = 0 ; i < NFILES ; i++) {
        if (curproc->p_files[i]) {
            newproc->p_files[i] = curproc->p_files[i];
            fref(newproc->p_files[i]);
        } else {
            KASSERT(newproc->p_files == NULL);
        }
    }

    /*bulletin 8*/
    KASSERT(!(list_empty(&curproc->p_threads)));
    KASSERT(curproc->p_threads.l_next == curproc->p_threads.l_prev);
    kthread_t *oldthr = list_item(curproc->p_threads.l_next, kthread_t, kt_plink);
    KASSERT(oldthr);
    kthread_t *newthr = kthread_clone(oldthr);
    KASSERT(newthr);
    newthr->kt_proc = newproc;

    /*bulletin 7*/
    newthr->kt_ctx.c_pdptr = newproc->p_pagedir;
    newthr->kt_ctx.c_eip = (uintptr_t)userland_entry;
    newthr->kt_ctx.c_esp = fork_setup_stack(regs, newthr->kt_kstack);
    /*c_kstack and c_kstacksz is set during kthread_clone*/

    /*bulletin 9: seems already been set during proc_create*/

    /*bulletin 10*/
    sched_make_runnable(newthr);

    return 0;
        NOT_YET_IMPLEMENTED("VM: do_fork");
        return 0;
}
