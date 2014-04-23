#include "types.h"
#include "globals.h"
#include "kernel.h"

#include "util/gdb.h"
#include "util/init.h"
#include "util/debug.h"
#include "util/string.h"
#include "util/printf.h"

#include "mm/mm.h"
#include "mm/page.h"
#include "mm/pagetable.h"
#include "mm/pframe.h"

#include "vm/vmmap.h"
#include "vm/shadowd.h"
#include "vm/shadow.h"
#include "vm/anon.h"

#include "main/acpi.h"
#include "main/apic.h"
#include "main/interrupt.h"
#include "main/gdt.h"

#include "proc/sched.h"
#include "proc/proc.h"
#include "proc/kthread.h"

#include "drivers/dev.h"
#include "drivers/blockdev.h"
#include "drivers/disk/ata.h"
#include "drivers/tty/virtterm.h"
#include "drivers/pci.h"

#include "api/exec.h"
#include "api/syscall.h"

#include "fs/vfs.h"
#include "fs/vnode.h"
#include "fs/vfs_syscall.h"
#include "fs/fcntl.h"
#include "fs/stat.h"
#include "fs/file.h"

#include "test/kshell/kshell.h"
/*#include "test/vfstest/vfstest.h"*/

GDB_DEFINE_HOOK(boot)
GDB_DEFINE_HOOK(initialized)
GDB_DEFINE_HOOK(shutdown)

static void      *bootstrap(int arg1, void *arg2);
static void      *idleproc_run(int arg1, void *arg2);
static kthread_t *initproc_create(void);
static void      *initproc_run(int arg1, void *arg2);
static void       hard_shutdown(void);

/*proc_tests*/
/*run 3 different proceses*/
static void create_proc(char *proc_name, context_func_t func, int arg1, void *arg2);
static void *run_procs(int arg1, void *arg2);
static void print_proc_list(void);

/*test about mutex*/
static void *run_kmutex_test(int arg1, void *arg2);

/*terminate out of order*/
static void *terminate_out_of_order(int arg1, void *arg2);
/*proc_tests*/

/*drivers_tests*/
static void *alternately_read(int arg1, void *arg2);
static void *alternately_write(int arg1, void *arg2);
/*static void *write_then_read(int arg1, void *arg2);*/
static void *multi_verify(int arg1, void *arg2);
/*drivers_tests*/

/*vfs_tests*/
void vfs_test();
void running_inode();
/*vfs_tests*/

static context_t bootstrap_context;

/**
 * This is the first real C function ever called. It performs a lot of
 * hardware-specific initialization, then creates a pseudo-context to
 * execute the bootstrap function in.
 */
void
kmain()
{
        GDB_CALL_HOOK(boot);

        dbg_init();
        dbg(DBG_CORE, "Kernel binary:\n");
        dbgq(DBG_CORE, "  text: 0x%p-0x%p\n", &kernel_start_text, &kernel_end_text);
        dbgq(DBG_CORE, "  data: 0x%p-0x%p\n", &kernel_start_data, &kernel_end_data);
        dbgq(DBG_CORE, "  bss:  0x%p-0x%p\n", &kernel_start_bss, &kernel_end_bss);

        page_init();

        pt_init();
        slab_init();
        pframe_init();

        acpi_init();
        apic_init();
	    pci_init();
        intr_init();

        gdt_init();

        /* initialize slab allocators */
#ifdef __VM__
        anon_init();
        shadow_init();
#endif
        vmmap_init();
        proc_init();
        kthread_init();

#ifdef __DRIVERS__
        bytedev_init();
        blockdev_init();
#endif

        void *bstack = page_alloc();
        pagedir_t *bpdir = pt_get();
        KASSERT(NULL != bstack && "Ran out of memory while booting.");
        context_setup(&bootstrap_context, bootstrap, 0, NULL, bstack, PAGE_SIZE, bpdir);
        context_make_active(&bootstrap_context);

        panic("\nReturned to kmain()!!!\n");
}

/**
 * This function is called from kmain, however it is not running in a
 * thread context yet. It should create the idle process which will
 * start executing idleproc_run() in a real thread context.  To start
 * executing in the new process's context call context_make_active(),
 * passing in the appropriate context. This function should _NOT_
 * return.
 *
 * Note: Don't forget to set curproc and curthr appropriately.
 *
 * @param arg1 the first argument (unused)
 * @param arg2 the second argument (unused)
 */
static void *
bootstrap(int arg1, void *arg2)
{
        /* necessary to finalize page table information */
        pt_template_init();

    char *idleproc_name = "Idle process";
    proc_t *idle_proc;
    idle_proc = proc_create(idleproc_name);
    curproc = idle_proc;

    kthread_t *idle_thr;
    idle_thr = kthread_create(idle_proc, idleproc_run, 0, NULL);
    curthr = idle_thr;

    dbg(DBG_THR, "Before context_make_active\n");
    context_make_active(&idle_thr->kt_ctx);

        /*NOT_YET_IMPLEMENTED("PROCS: bootstrap");*/
        panic("weenix returned to bootstrap()!!! BAD!!!\n");
        return NULL;
}

/**
 * Once we're inside of idleproc_run(), we are executing in the context of the
 * first process-- a real context, so we can finally begin running
 * meaningful code.
 *
 * This is the body of process 0. It should initialize all that we didn't
 * already initialize in kmain(), launch the init process (initproc_run),
 * wait for the init process to exit, then halt the machine.
 *
 * @param arg1 the first argument (unused)
 * @param arg2 the second argument (unused)
 */
static void *
idleproc_run(int arg1, void *arg2)
{
    dbg(DBG_PROC, "Start idleproc_run.\n");

        int status;
        pid_t child;

        /* create init proc */
        kthread_t *initthr = initproc_create();
        init_call_all();
        GDB_CALL_HOOK(initialized);

        /* Create other kernel threads (in order) */

#ifdef __VFS__
        /* Once you have VFS remember to set the current working directory
         * of the idle and init processes */
        curproc->p_cwd = vfs_root_vn;

        initthr->kt_proc->p_cwd = vfs_root_vn;
        vref(vfs_root_vn);

        /* Here you need to make the null, zero, and tty devices using mknod */
        /* You can't do this until you have VFS, check the include/drivers/dev.h
         * file for macros with the device ID's you will need to pass to mknod */
        char *devpath = "/dev";
        do_mkdir(devpath);
        /*still need to figure out how to set the vnode for special device*/
        do_mknod("/dev/null", S_IFCHR, MEM_NULL_DEVID);
        do_mknod("/dev/zero", S_IFCHR, MEM_ZERO_DEVID);
        int nterms = vt_num_terminals();
        int i = 0;
        char *path = "/dev/tty0";
        for (i = 0 ; i < nterms ; i++) {
            path[8] = '0' + i;
            do_mknod((const char *)path, S_IFCHR, MKDEVID(2, i));
        }
#endif

        /* Finally, enable interrupts (we want to make sure interrupts
         * are enabled AFTER all drivers are initialized) */
        intr_enable();

        /* Run initproc */
        sched_make_runnable(initthr);
        /* Now wait for it */
        child = do_waitpid(PID_INIT, 0, &status);
        KASSERT(PID_INIT == child);
        dbg(DBG_PROC, "The return value is %d\n", status);

#ifdef __MTP__
        kthread_reapd_shutdown();
#endif


#ifdef __SHADOWD__
        /* wait for shadowd to shutdown */
        shadowd_shutdown();
#endif

#ifdef __VFS__
        /* Shutdown the vfs: */
        dbg_print("weenix: vfs shutdown...\n");
        vput(curproc->p_cwd);
        if (vfs_shutdown())
                panic("vfs shutdown FAILED!!\n");

#endif

        /* Shutdown the pframe system */
#ifdef __S5FS__
        pframe_shutdown();
#endif

        dbg_print("\nweenix: halted cleanly!\n");
        GDB_CALL_HOOK(shutdown);
        hard_shutdown();
        return NULL;
}

/**
 * This function, called by the idle process (within 'idleproc_run'), creates the
 * process commonly refered to as the "init" process, which should have PID 1.
 *
 * The init process should contain a thread which begins execution in
 * initproc_run().
 *
 * @return a pointer to a newly created thread which will execute
 * initproc_run when it begins executing
 */
static kthread_t *
initproc_create(void)
{
    char *initproc_name = "Init process";
    proc_t *init_proc = proc_create(initproc_name);

    kthread_t *init_thr = kthread_create(init_proc, initproc_run, NULL, NULL);
    return init_thr;

        /*NOT_YET_IMPLEMENTED("PROCS: initproc_create");*/
}

/**
 * The init thread's function changes depending on how far along your Weenix is
 * developed. Before VM/FI, you'll probably just want to have this run whatever
 * tests you've written (possibly in a new process). After VM/FI, you'll just
 * exec "/bin/init".
 *
 * Both arguments are unused.
 *
 * @param arg1 the first argument (unused)
 * @param arg2 the second argument (unused)
 */
static void *
initproc_run(int arg1, void *arg2)
{
    dbg(DBG_THR, "Going into initproc.\n");
    /*while(1)*/
        /*do_waitpid(-1, 0, NULL);*/
    /*run tests*/
/*
 *    create_proc("run procs", run_procs, NULL, NULL);
 *    do_waitpid(-1, 0, NULL);
 *    print_proc_list();
 *
 *    create_proc("mutex test", run_kmutex_test, NULL, NULL);
 *    do_waitpid(-1, 0, NULL);
 *
 *    create_proc("out of order", terminate_out_of_order, NULL, NULL);
 *    do_waitpid(-1, 0, NULL);
 */
    /*end tests*/

    /*
     *kshell_t *ksh = kshell_create(0);
     *KASSERT(NULL != ksh);
     *while (kshell_execute_next(ksh));
     *kshell_destroy(ksh);
     */

    /*
     *create_proc("Alternately reading", alternately_read, 0, 0);
     *do_waitpid(-1, 0, NULL);
     *print_proc_list();
     */

    /*
     *create_proc("Alternately writing", alternately_write, 0, 0);
     *do_waitpid(-1, 0, NULL);
     *print_proc_list();
     */

    /*
     *create_proc("Multi thread verifying", multi_verify, 0, 0);
     *do_waitpid(-1, 0, NULL);
     */
     /*========================*/
    /*vfstest_main(1, NULL);*/
    /*vfs_test();*/
    /*running_inode();*/
     /*========================*/

    do_exit(0);

    panic("initproc won't go here because it has exited.\n");
    return NULL;

        /*NOT_YET_IMPLEMENTED("PROCS: initproc_run");*/
}

/**
 * Clears all interrupts and halts, meaning that we will never run
 * again.
 */
static void
hard_shutdown()
{
#ifdef __DRIVERS__
        vt_print_shutdown();
#endif
        __asm__ volatile("cli; hlt");
}

/*---------------------TEST-------------------------*/
/*---------------------PROC-------------------------*/
static void *
print_proc_info(int arg1, void *arg2)
{
    KASSERT(NULL != curproc);
    dbg(DBG_TEST, "Printing info of curproc:\n");
    dbginfo(DBG_TEST, proc_info, curproc);

    kthread_exit((void *)0);

    panic("Should not be here\n");
    return NULL;
}

static void
create_proc(char *proc_name, context_func_t func, int arg1, void *arg2)
{
    proc_t *test_proc = proc_create(proc_name);

    kthread_t *test_thr = kthread_create(test_proc, func, arg1, arg2);
    sched_make_runnable(test_thr);
    return;
}

static void
print_proc_list(void)
{
    dbg(DBG_TEST, "Printing proc_list:\n");
    dbginfo(DBG_TEST, proc_list_info, NULL);
}

static void *
run_procs(int arg1, void *arg2)
{
    dbg(DBG_TEST, "Starting testing\n");

    char *proc_name = "Test1";
    create_proc(proc_name, print_proc_info, NULL, NULL);
    proc_name = "Test2";
    create_proc(proc_name, print_proc_info, NULL, NULL);
    proc_name = "Test3";
    create_proc(proc_name, print_proc_info, NULL, NULL);

    print_proc_list();

    int i = 0;
    do_waitpid(-1, 0, NULL);
    dbg(DBG_TEST, "1\n");
    do_waitpid(-1, 0, NULL);
    dbg(DBG_TEST, "2\n");
    do_waitpid(-1, 0, NULL);
    dbg(DBG_TEST, "3\n");
    dbg(DBG_TEST, "After wait for 3 processes.\n");
    print_proc_list();
    do_exit(1);

    panic("Should not be here\n");
    return NULL;
}

static void *
lock_and_switch(int arg1, void *arg2) {
    kmutex_t *mtx = (kmutex_t *)arg2;

    kmutex_lock(mtx);
    dbg(DBG_TEST, "This proc acquire the lock and will give up the processor.\n");
    sched_make_runnable(curthr);
    sched_switch();
    kmutex_unlock(mtx);
    dbg(DBG_TEST, "Now unlock the mutex and exit.\n");

    kthread_exit((void *)0);

    panic("Should not be here.\n");
    return NULL;
}

static void *
just_lock(int arg1, void *arg2) {
    kmutex_t *mtx = (kmutex_t *)arg2;

    dbg(DBG_TEST, "Just lock trying to acquire the lock.\n");
    kmutex_lock(mtx);

    dbg(DBG_TEST, "Just lock acquire the lock and unlock it.\n");
    kmutex_unlock(mtx);

    kthread_exit((void *)0);

    panic("Should not be here.\n");
    return NULL;
}

static void *
run_kmutex_test(int arg1, void *arg2)
{
    dbg(DBG_TEST, "Start testing kmutex\n");
    kmutex_t *mtx = (kmutex_t *)kmalloc(sizeof(kmutex_t));
    kmutex_init(mtx);
    create_proc("lock and switch No.1", lock_and_switch, NULL, (void *)mtx);
    create_proc("lock and switch No.2", lock_and_switch, NULL, (void *)mtx);
    create_proc("just lock No.1", just_lock, NULL, (void *)mtx);
    create_proc("just lock No.2", just_lock, NULL, (void *)mtx);

    print_proc_list();

    /*sched_make_runnable(curthr);*/
    do_waitpid(-1, 0, NULL);
    do_waitpid(-1, 0, NULL);
    do_waitpid(-1, 0, NULL);
    do_waitpid(-1, 0, NULL);

    kfree(mtx);
    do_exit(0);

    panic("Should not be here.\n");
    return NULL;
}

static void *
switch_then_exit(int arg1, void *arg2)
{
    sched_make_runnable(curthr);
    sched_switch();

    do_exit(0);

    panic("Should not go to here.\n");
    return NULL;
}

static void *
just_exit(int arg1, void *arg2)
{
    do_exit(0);

    panic("Should not go to here.\n");
    return NULL;
}

static void *
terminate_out_of_order(int arg1, void *arg2)
{
    create_proc("switch_then_exit No.1", switch_then_exit, NULL, NULL);
    create_proc("just_exit No.1", just_exit, NULL, NULL);
    create_proc("switch_then_exit No.2", switch_then_exit, NULL, NULL);
    create_proc("just_exit No.2", just_exit, NULL, NULL);

    do_waitpid(-1, 0, NULL);
    do_waitpid(-1, 0, NULL);
    do_waitpid(-1, 0, NULL);
    do_waitpid(-1, 0, NULL);

    do_exit(0);

    panic("Should not be here.\n");
    return NULL;
}

/*---------------------DRIVERS-------------------------*/
static void *
read_from_terminal(int arg1, void *arg2)
{
    bytedev_t *bd = bytedev_lookup(MKDEVID(2, 0));
    char *buff = (char *)kmalloc(sizeof(char) * 128);
    int size;
    /*
     *while ((size = bd->cd_ops->read(bd, 0, buff, 100)) != 0) {
     *    dbg(DBG_TEST, "Reading size: %d\n", size);
     *    dbg(DBG_TEST, "Thread: %s\n", curproc->p_comm);
     *    dbg(DBG_TEST, "Read: %s", buff);
     *}
     */
    dbg(DBG_TEST, "Exiting.\n");
    do_exit(0);
    return 0;
}

static void *
alternately_read(int arg1, void *arg2)
{
    create_proc("read No.1", read_from_terminal, 0, 0);
    create_proc("read No.2", read_from_terminal, 0, 0);
    do_waitpid(-1, 0, NULL);
    do_waitpid(-1, 0, NULL);
    return 0;
}

static void *
write_to_terminal(int arg1, void *arg2)
{
    bytedev_t *bd = bytedev_lookup(MKDEVID(2, 0));
    char *buff = (char *)kmalloc(sizeof(char) * 128);
    buff = strcpy(buff, curproc->p_comm);
    int i;
    /*
     *for (i = 0 ; i < 3 ; i++) {
     *    bd->cd_ops->write(bd, 0, buff, 100);
     *    sched_make_runnable(curthr);
     *    sched_switch();
     *}
     */
    do_exit(0);
    return 0;
}

static void *
alternately_write(int arg1, void *arg2)
{
    create_proc("write No.1", write_to_terminal, 0, 0);
    create_proc("write No.2", write_to_terminal, 0, 0);
    do_waitpid(-1, 0, NULL);
    do_waitpid(-1, 0, NULL);
    return 0;
}

static void *
write_then_read(int arg1, void *arg2)
{
    int i;
    for (i = 0 ; i < 3 ; i++) {
        char *data = (char *)page_alloc();
        memset(data, 0, PAGE_SIZE);
        data = strcat(data, curproc->p_comm);
        blockdev_t *bd = blockdev_lookup(MKDEVID(1, 0));

        dbg(DBG_TEST, "%s currently writing...\n", curproc->p_comm);
        bd->bd_ops->write_block(bd, data, arg1, 1);
        
        char *out = (char *)page_alloc();
        dbg(DBG_TEST, "%s currently reading...\n", curproc->p_comm);
        bd->bd_ops->read_block(bd, out, arg1, 1);
        KASSERT(strcmp(out, curproc->p_comm) == 0);
        dbg(DBG_TEST, "Verifying succeed,\n");

        page_free(data);

        sched_make_runnable(curthr);
        sched_switch();
    }
    return 0;
}

static void *
multi_verify(int arg1, void *arg2)
{
    create_proc("Verify No.1", write_then_read, 0, 0);
    create_proc("Verify No.2", write_then_read, 1, 0);
    create_proc("Verify No.3", write_then_read, 2, 0);
    do_waitpid(-1, 0, NULL);
    do_waitpid(-1, 0, NULL);
    do_waitpid(-1, 0, NULL);
    return 0;
}

/*---------------------VFS-------------------------*/

void
vfs_test()
{
    do_mkdir("123");
    do_open("123/4", O_RDONLY | O_CREAT);
    do_unlink("123/4");
    do_rmdir("123");
    /*
     *do_open("/1", O_RDONLY);
     *do_mkdir("/1");
     *do_mkdir("/2");
     *do_mkdir("/3");
     */
    /*do_mkdir("/4");*/
    /*do_mkdir("/4/1");*/
    /*do_rmdir("/4/1");*/
    /*do_mkdir("/4/1");*/
    /*do_rmdir("/4/1");*/
    /*do_mkdir("/4/1");*/
    /*do_rmdir("/4/1");*/
    /*panic("stop it");*/
    
    /*int fd = do_open("/1", O_RDONLY);*/
    /*do_open("/1/noexist", O_RDONLY);*/

    /*file_t *dir_file = curproc->p_files[fd];*/
    /*KASSERT(dir_file->f_refcount == 1);*/
    /*KASSERT(dir_file->f_vnode->vn_refcount == 1);*/

    /*do_mkdir("/1/2");*/
    /*int fd_dir1 = do_open("/1/2", O_RDONLY);*/
    /*KASSERT(dir_file->f_refcount == 1);*/
    /*KASSERT(dir_file->f_vnode->vn_refcount == 1);*/
    /*KASSERT(curproc->p_files[fd_dir1]->f_refcount == 1);*/
    /*KASSERT(curproc->p_files[fd_dir1]->f_vnode->vn_refcount == 1);*/
    /*do_close(do_open("1/2/./.", O_RDONLY));*/
    /*do_close(do_open("1/2/./././././../../1", O_RDONLY));*/

    /*do_close(fd_dir1);*/
    /*do_close(fd);*/
    /*do_close(do_open("/2", O_RDONLY));*/
    /*do_close(do_open("/2", O_RDONLY));*/
    /*do_close(do_open("/2", O_RDONLY));*/
    /*do_close(do_open("/2", O_RDONLY));*/
    /*do_close(do_open("/2", O_RDONLY));*/
    /*do_close(do_open("/2/././", O_RDONLY));*/
}

void running_inode()
{
    int err = 0;
    int i = 0;
    char filename[10];
    filename[9] = '\0';
    for (i = 0 ; i < 240 ; i++) {
        sprintf(filename, "/%d", i);
        err = do_open(filename, O_CREAT);
        if (err < 0) {
            dbg(DBG_TEST, "error number is %d \n", err);
        }
        do_close(err);
    }
}
