#include "kernel.h"
#include "config.h"
#include "globals.h"
#include "errno.h"

#include "util/debug.h"
#include "util/list.h"
#include "util/string.h"
#include "util/printf.h"

#include "proc/kthread.h"
#include "proc/proc.h"
#include "proc/sched.h"
#include "proc/proc.h"

#include "mm/slab.h"
#include "mm/page.h"
#include "mm/mmobj.h"
#include "mm/mm.h"
#include "mm/mman.h"

#include "vm/vmmap.h"

#include "fs/vfs.h"
#include "fs/vfs_syscall.h"
#include "fs/vnode.h"
#include "fs/file.h"

proc_t *curproc = NULL; /* global */
static slab_allocator_t *proc_allocator = NULL;

static list_t _proc_list;
static proc_t *proc_initproc = NULL; /* Pointer to the init process (PID 1) */

void
proc_init()
{
        list_init(&_proc_list);
        proc_allocator = slab_allocator_create("proc", sizeof(proc_t));
        KASSERT(proc_allocator != NULL);
}

static pid_t next_pid = 0;

/**
 * Returns the next available PID.
 *
 * Note: Where n is the number of running processes, this algorithm is
 * worst case O(n^2). As long as PIDs never wrap around it is O(n).
 *
 * @return the next available PID
 */
static int
_proc_getid()
{
        proc_t *p;
        pid_t pid = next_pid;
        while (1) {
failed:
                list_iterate_begin(&_proc_list, p, proc_t, p_list_link) {
                        if (p->p_pid == pid) {
                                if ((pid = (pid + 1) % PROC_MAX_COUNT) == next_pid) {
                                        return -1;
                                } else {
                                        goto failed;
                                }
                        }
                } list_iterate_end();
                next_pid = (pid + 1) % PROC_MAX_COUNT;
                return pid;
        }
}

/*
 * The new process, although it isn't really running since it has no
 * threads, should be in the PROC_RUNNING state.
 *
 * Don't forget to set proc_initproc when you create the init
 * process. You will need to be able to reference the init process
 * when reparenting processes to the init process.
 */
proc_t *
proc_create(char *name)
{
    /*the name arg not NULL*/
    KASSERT(NULL != name); 

    proc_t *proc_struct = slab_obj_alloc(proc_allocator);
    KASSERT(NULL != proc_struct);
    /*it should not be null*/

    proc_struct->p_pid = _proc_getid();
    if (proc_struct->p_pid == PID_INIT) {
        /*setting the init process if pid is 1*/
        dbg(DBG_PROC, "proc_initproc is set\n");
        proc_initproc = proc_struct;
    }

    strcpy(proc_struct->p_comm, name);
    
    /*exit value? p_status not set here.*/

    list_init(&proc_struct->p_threads);
    list_init(&proc_struct->p_children);

    proc_struct->p_state = PROC_RUNNING;

    sched_queue_init(&proc_struct->p_wait);

    proc_struct->p_pagedir = pt_create_pagedir();

    list_link_init(&proc_struct->p_list_link);
    list_insert_tail(&_proc_list, &proc_struct->p_list_link);
    /*add itself to _proc_list*/

    list_link_init(&proc_struct->p_child_link);

    if (PID_IDLE != proc_struct->p_pid) {
        KASSERT(NULL != curproc);
        proc_struct->p_pproc = curproc;
        /*not sure about the parent process.*/

        KASSERT(curproc == proc_struct->p_pproc);
        list_insert_tail(&proc_struct->p_pproc->p_children, &proc_struct->p_child_link);
        /*dbg(DBG_PROC, "Not IDLE_PROC, hook it up with parent: %d\n", proc_struct->p_pid);*/
    } else {
        proc_struct->p_pproc = NULL;
    }

    /* VFS-related: */
    /*p_files*/
    int i = 0;
    for (i = 0 ; i < NFILES ; i++) {
        proc_struct->p_files[i] = NULL;
    }
    /*p_cwd*/
    if (proc_struct->p_pid != PID_IDLE && proc_struct->p_pid != PID_INIT) {
        proc_struct->p_cwd = curproc->p_cwd;
        if (proc_struct->p_cwd) {
            vref(proc_struct->p_cwd);
        }
    } else {
        proc_struct->p_cwd = NULL;
    }

    /* VM */
    proc_struct->p_vmmap = vmmap_create();
    KASSERT(proc_struct->p_vmmap);

    dbg(DBG_PROC, "Created process with name: %s\n", name);
    dbginfo(DBG_PROC, proc_info, proc_struct);
    dbginfo(DBG_PROC, proc_list_info, NULL);
    
    return proc_struct;

        /*NOT_YET_IMPLEMENTED("PROCS: proc_create");*/
}

/**
 * Cleans up as much as the process as can be done from within the
 * process. This involves:
 *    - Closing all open files (VFS)
 *    - Cleaning up VM mappings (VM)
 *    - Waking up its parent if it is waiting
 *    - Reparenting any children to the init process
 *    - Setting its status and state appropriately
 *
 * The parent will finish destroying the process within do_waitpid (make
 * sure you understand why it cannot be done here). Until the parent
 * finishes destroying it, the process is informally called a 'zombie'
 * process.
 *
 * This is also where any children of the current process should be
 * reparented to the init process (unless, of course, the current
 * process is the init process. However, the init process should not
 * have any children at the time it exits).
 *
 * Note: You do _NOT_ have to special case the idle process. It should
 * never exit this way.
 *
 * @param status the status to exit the process with
 */
void
proc_cleanup(int status)
{
    /*waking up its parent*/
    sched_wakeup_on(&curproc->p_pproc->p_wait);

    if (curproc == proc_initproc) {
        KASSERT(list_empty(&curproc->p_children));
    }

    /*reparenting*/
    proc_t *child_proc;
    list_iterate_begin(&curproc->p_children, child_proc, proc_t, p_child_link) {
        child_proc->p_pproc = proc_initproc;
        list_remove(&child_proc->p_child_link);
        list_insert_tail(&proc_initproc->p_children, &child_proc->p_child_link);
        dbg(DBG_PROC, "Reparenting to proc: %s\n", child_proc->p_pproc->p_comm);
    } list_iterate_end();
    
    KASSERT(list_empty(&curproc->p_children));
    dbg(DBG_PROC, "After reparenting:\n");
    dbginfo(DBG_PROC, proc_list_info, NULL);

    /*setting state and status*/
    curproc->p_state = PROC_DEAD;
    curproc->p_status = status;

    /*clean up file descriptors*/
    /*VFS*/
    int i = 0;
    for (i = 0 ; i < NFILES ; i++) {
        if (curproc->p_files[i]) {
            fput(curproc->p_files[i]);
        }
    }

    if (curproc->p_cwd) {
        vput(curproc->p_cwd);
    }

        /*NOT_YET_IMPLEMENTED("PROCS: proc_cleanup");*/
}

/*
 * This has nothing to do with signals and kill(1).
 *
 * Calling this on the current process is equivalent to calling
 * do_exit().
 *
 * In Weenix, this is only called from proc_kill_all.
 */
void
proc_kill(proc_t *p, int status)
{
    KASSERT(NULL != p);
    KASSERT(NULL != curproc);

    if (curproc == p) {
        do_exit(status);
    } else {
        kthread_t *kthr;
        list_iterate_begin(&p->p_threads, kthr, kthread_t, kt_plink) {
            /*remove it from parent's thread list*/
            list_remove(&kthr->kt_plink);
            /*cancel the thread*/
            kthread_cancel(kthr, (void *)(&status));
        } list_iterate_end();
        /*proc_cleanup? -maybe not here, wait till syscall*/
    }

        /*NOT_YET_IMPLEMENTED("PROCS: proc_kill");*/
}

/*
 * Remember, proc_kill on the current process will _NOT_ return.
 * Don't kill direct children of the idle process.
 *
 * In Weenix, this is only called by sys_halt.
 */
void
proc_kill_all()
{
    proc_t *proc_iter;
    list_iterate_begin(&_proc_list, proc_iter, proc_t, p_list_link) {
        /*no direct children of idle proces, not curproc*/
        if (PID_IDLE != proc_iter->p_pproc->p_pid || curproc != proc_iter) {
            list_remove(&proc_iter->p_list_link);
            list_remove(&proc_iter->p_child_link);
            proc_kill(proc_iter, 0);
        }
    } list_iterate_end();

    /*kill current process*/
    do_exit(0);

        /*NOT_YET_IMPLEMENTED("PROCS: proc_kill_all");*/
}

proc_t *
proc_lookup(int pid)
{
        proc_t *p;
        list_iterate_begin(&_proc_list, p, proc_t, p_list_link) {
                if (p->p_pid == pid) {
                        return p;
                }
        } list_iterate_end();
        return NULL;
}

list_t *
proc_list()
{
        return &_proc_list;
}

/*
 * This function is only called from kthread_exit.
 *
 * Unless you are implementing MTP, this just means that the process
 * needs to be cleaned up and a new thread needs to be scheduled to
 * run. If you are implementing MTP, a single thread exiting does not
 * necessarily mean that the process should be exited.
 */
void
proc_thread_exited(void *retval)
{
    /*it should not be in any wait queue*/
    KASSERT(NULL == curthr->kt_wchan);

    /*deal with NULL pointer*/
    /*
     *if (retval == NULL) {
     *    curproc->p_status = 0;
     *    proc_cleanup(0);
     *} else {
     *    curproc->p_status = *((int *)retval);
     *    proc_cleanup(*((int *)retval));
     *}
     */

    /*p_status will be set during proc_cleanup*/
    /*curproc->p_status = (int)retval;*/
    proc_cleanup((int)retval);

    dbg(DBG_PROC, "Exiting process: [%s], now gonna make the switch and never return.\n", curproc->p_comm);

    sched_switch();

        /*NOT_YET_IMPLEMENTED("PROCS: proc_thread_exited");*/
}

/* If pid is -1 dispose of one of the exited children of the current
 * process and return its exit status in the status argument, or if
 * all children of this process are still running, then this function
 * blocks on its own p_wait queue until one exits.
 *
 * If pid is greater than 0 and the given pid is a child of the
 * current process then wait for the given pid to exit and dispose
 * of it.
 *
 * If the current process has no children, or the given pid is not
 * a child of the current process return -ECHILD.
 *
 * Pids other than -1 and positive numbers are not supported.
 * Options other than 0 are not supported.
 */
pid_t
do_waitpid(pid_t pid, int options, int *status)
{
    KASSERT(0 != pid);
    KASSERT(0 == options);

    if (list_empty(&curproc->p_children)) {
        return ECHILD;
    }

    /*sched_make_runnable(curthr);*/
    /*sched_switch();*/

CheckAgain:
    dbg(DBG_PROC, "do_waitpid now starts collecting one child process.\n");

    proc_t *child_proc = NULL;
    pid_t child_pid = -1;
    int child_exist = 0;

    if (-1 == pid) {
        proc_t *proc_iter;
        list_iterate_begin(&curproc->p_children, proc_iter, proc_t, p_child_link) {
            if (PROC_DEAD == proc_iter->p_state) {
                /*record child's pid*/
                child_pid = proc_iter->p_pid;

                child_proc = proc_iter;
                break;
            }
        } list_iterate_end();
    } else {
        proc_t *proc_iter;
        list_iterate_begin(&curproc->p_children, proc_iter, proc_t, p_child_link) {
            if (pid == proc_iter->p_pid) {
                child_exist = 1;
                if (PROC_DEAD == proc_iter->p_state) {
                    /*record child's pid*/
                    child_pid = pid;

                    child_proc = proc_iter;
                }
                break;
            }
        } list_iterate_end();
    }

    /*child exists but not dead yet*/
    if (child_exist && NULL == child_proc) {
        dbg(DBG_PROC, "The child current process is waiting for is not dead yet.\n");
        sched_sleep_on(&curproc->p_wait);
        dbg(DBG_PROC, "Get woken up because it's child exited.\n");
        goto CheckAgain;
    }

    /*pid is not child of curproc*/
    if (child_proc == NULL && pid > 0) {
        return ECHILD;
    }

    /*no dead child found*/
    if (child_proc == NULL && -1 == pid) {
        dbg(DBG_PROC, "Aha! None of your children are dead, put yourself to sleep...\n");
        /*sched_make_runnable(curthr);*/
        /*sched_switch();*/
        sched_sleep_on(&curproc->p_wait);
        dbg(DBG_PROC, "Get woken up because one of it's children exited.\n");
        goto CheckAgain;
    }
    KASSERT(NULL != child_proc);

    /*cleanup the thread*/
    kthread_t *kthr;
    list_iterate_begin(&child_proc->p_threads, kthr, kthread_t, kt_plink) {
        page_free((void *)kthr->kt_ctx.c_kstack);
        kthread_destroy(kthr);
    } list_iterate_end();
    KASSERT(list_empty(&child_proc->p_threads));

    /*cleanup the proc*/
    /*assign the return value to out parameter*/
    if (NULL != status) {
        *status = child_proc->p_status;
    }

    /*debug infomation*/
    dbg(DBG_PROC, "About to clean the process: %s\n", child_proc->p_comm);

    list_remove(&child_proc->p_list_link);
    list_remove(&child_proc->p_child_link);

    /*destroy page table and the struct*/
    pt_destroy_pagedir(child_proc->p_pagedir);
    slab_obj_free(proc_allocator, child_proc);

    return child_pid;

        /*NOT_YET_IMPLEMENTED("PROCS: do_waitpid");*/
}

/*
 * Cancel all threads, join with them, and exit from the current
 * thread.
 *
 * @param status the exit status of the process
 */
void
do_exit(int status)
{
    /*implementation for now, only one thread*/
    kthread_exit((void *)status);

        /*NOT_YET_IMPLEMENTED("PROCS: do_exit");*/
}

size_t
proc_info(const void *arg, char *buf, size_t osize)
{
        const proc_t *p = (proc_t *) arg;
        size_t size = osize;
        proc_t *child;

        KASSERT(NULL != p);
        KASSERT(NULL != buf);

        iprintf(&buf, &size, "pid:          %i\n", p->p_pid);
        iprintf(&buf, &size, "name:         %s\n", p->p_comm);
        if (NULL != p->p_pproc) {
                iprintf(&buf, &size, "parent:       %i (%s)\n",
                        p->p_pproc->p_pid, p->p_pproc->p_comm);
        } else {
                iprintf(&buf, &size, "parent:       -\n");
        }

#ifdef __MTP__
        int count = 0;
        kthread_t *kthr;
        list_iterate_begin(&p->p_threads, kthr, kthread_t, kt_plink) {
                ++count;
        } list_iterate_end();
        iprintf(&buf, &size, "thread count: %i\n", count);
#endif

        if (list_empty(&p->p_children)) {
                iprintf(&buf, &size, "children:     -\n");
        } else {
                iprintf(&buf, &size, "children:\n");
        }
        list_iterate_begin(&p->p_children, child, proc_t, p_child_link) {
                iprintf(&buf, &size, "     %i (%s)\n", child->p_pid, child->p_comm);
        } list_iterate_end();

        iprintf(&buf, &size, "status:       %i\n", p->p_status);
        iprintf(&buf, &size, "state:        %i\n", p->p_state);

#ifdef __VFS__
#ifdef __GETCWD__
        if (NULL != p->p_cwd) {
                char cwd[256];
                lookup_dirpath(p->p_cwd, cwd, sizeof(cwd));
                iprintf(&buf, &size, "cwd:          %-s\n", cwd);
        } else {
                iprintf(&buf, &size, "cwd:          -\n");
        }
#endif /* __GETCWD__ */
#endif

#ifdef __VM__
        iprintf(&buf, &size, "start brk:    0x%p\n", p->p_start_brk);
        iprintf(&buf, &size, "brk:          0x%p\n", p->p_brk);
#endif

        return size;
}

size_t
proc_list_info(const void *arg, char *buf, size_t osize)
{
        size_t size = osize;
        proc_t *p;

        KASSERT(NULL == arg);
        KASSERT(NULL != buf);

#if defined(__VFS__) && defined(__GETCWD__)
        iprintf(&buf, &size, "%5s %-13s %-18s %-s\n", "PID", "NAME", "PARENT", "CWD");
#else
        iprintf(&buf, &size, "%5s %-13s %-s\n", "PID", "NAME", "PARENT");
#endif

        list_iterate_begin(&_proc_list, p, proc_t, p_list_link) {
                char parent[64];
                if (NULL != p->p_pproc) {
                        snprintf(parent, sizeof(parent),
                                 "%3i (%s)", p->p_pproc->p_pid, p->p_pproc->p_comm);
                } else {
                        snprintf(parent, sizeof(parent), "  -");
                }

#if defined(__VFS__) && defined(__GETCWD__)
                if (NULL != p->p_cwd) {
                        char cwd[256];
                        lookup_dirpath(p->p_cwd, cwd, sizeof(cwd));
                        iprintf(&buf, &size, " %3i  %-13s %-18s %-s\n",
                                p->p_pid, p->p_comm, parent, cwd);
                } else {
                        iprintf(&buf, &size, " %3i  %-13s %-18s -\n",
                                p->p_pid, p->p_comm, parent);
                }
#else
                iprintf(&buf, &size, " %3i  %-13s %-s\n",
                        p->p_pid, p->p_comm, parent);
#endif
        } list_iterate_end();
        return size;
}
