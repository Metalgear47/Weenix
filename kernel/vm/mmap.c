#include "globals.h"
#include "errno.h"
#include "types.h"

#include "mm/mm.h"
#include "mm/tlb.h"
#include "mm/mman.h"
#include "mm/page.h"

#include "proc/proc.h"

#include "util/string.h"
#include "util/debug.h"

#include "fs/vnode.h"
#include "fs/vfs.h"
#include "fs/file.h"

#include "vm/vmmap.h"
#include "vm/mmap.h"

/*
 *my own include
 */
#include "fs/fcntl.h"
#include "fs/stat.h"

#define VALID_ADDR(addr) ((uint32_t)addr >= USER_MEM_LOW && \
                            (uint32_t)addr < USER_MEM_HIGH)
#define LEN_TO_PAGES(len) len / PAGE_SIZE + ((len % PAGE_SIZE == 0) ? 0 : 1)
    /*EAGAIN*/
    /*The file has been locked, or too much memory has been locked (see setrlimit(2)).*/
    /*ENFILE*/
    /*The system limit on the total number of open files has been reached.*/
    /*ENODEV*/
    /*The underlying file system of the specified file does not support memory mapping.*/
    /*ENOMEM*/
    /*No memory is available, or the process's maximum number of mappings would have been exceeded.*/
    /*EPERM*/
    /*The prot argument asks for PROT_EXEC but the mapped area belongs to a file on a file system that was mounted no-exec.*/
    /*ETXTBSY*/
    /*MAP_DENYWRITE was set but the object specified by fd is open for writing.*/
    /*EOVERFLOW*/
    /*On 32-bit architecture together with the large file extension (i.e., using 64-bit off_t): the number of pages used for length plus number of pages used for offset would overflow unsigned long (32 bits).*/

/*
 * This function implements the mmap(2) syscall, but only
 * supports the MAP_SHARED, MAP_PRIVATE, MAP_FIXED, and
 * MAP_ANON flags.
 *
 * Add a mapping to the current process's address space.
 * You need to do some error checking; see the ERRORS section
 * of the manpage for the problems you should anticipate.
 * After error checking most of the work of this function is
 * done by vmmap_map(), but remember to clear the TLB.
 */
int
do_mmap(void *addr, size_t len, int prot, int flags,
        int fd, off_t off, void **ret)
{
    dbg(DBG_VM, "do_mmap function hook\n");
    /*EINVAL*/
    /*We don't like addr, length, or offset (e.g., they are too large, or not aligned on a page boundary).*/
    /*EINVAL*/
    /*(since Linux 2.6.12) length was 0.*/
    if (!PAGE_ALIGNED(addr) || !PAGE_ALIGNED(off) || len == 0) {
        return -EINVAL;
    }

    /*
     *if (!VALID_ADDR(addr) || !VALID_ADDR((uint32_t)addr + len)) {
     *    return -EINVAL;
     *}
     */

    /*EINVAL*/
    /*flags contained neither MAP_PRIVATE or MAP_SHARED, or contained both of these values.*/
    int map_type = flags & MAP_TYPE;
    if (map_type == 0 || map_type == MAP_TYPE) {
        return -EINVAL;
    }

    file_t *file = NULL;
    vnode_t *vnode = NULL;
    int err = 0;

    /*EBADF*/
    /*fd is not a valid file descriptor (and MAP_ANONYMOUS was not set).*/
    if ((flags & MAP_ANON) == 0)  {
        file = fget(fd);
        if (file == NULL) {
            return -EBADF;
        }
    } else {
        goto CheckDone;
    }

    /*EACCES*/
    /*A file descriptor refers to a non-regular file. Or MAP_PRIVATE was requested, but fd is not open for reading. Or MAP_SHARED was requested and PROT_WRITE is set, but fd is not open in read/write (O_RDWR) mode. Or PROT_WRITE is set, but the file is append-only.*/
    KASSERT(file);
    vnode = file->f_vnode;
    KASSERT(vnode);
    /*
     *if (!S_ISREG(vnode->vn_mode)) {
     *    fput(file);
     *    return -EACCES;
     *}
     */
    int frwmode = file->f_mode & 0x7;
    if (map_type == MAP_PRIVATE && !(frwmode & FMODE_READ)) {
        /*not open for reading*/
        fput(file);
        return -EACCES;
    }
    if (map_type == MAP_SHARED && (prot & PROT_WRITE) &&
            (!(frwmode & FMODE_READ) || !(frwmode &FMODE_WRITE))) {
        fput(file);
        return -EACCES;
    }
    if ((prot & PROT_WRITE) && (frwmode == FMODE_APPEND)) {
        fput(file);
        return -EACCES;
    }

    /*
     *if (off + (signed)len >= vnode->vn_len) {
     *    [>exceeding the file size<]
     *    fput(file);
     *    return -EINVAL;
     *}
     */

    /*
     *TODO: handling MAP_ANON
     */

    /*TODO: MAP_FIXED?*/

CheckDone:
    err = 0;
    vmarea_t *area;
    err = vmmap_map(curproc->p_vmmap, vnode, ADDR_TO_PN(addr), LEN_TO_PAGES(len),
                        prot, flags, off, VMMAP_DIR_HILO, &area);
    if (file) {
        fput(file);
    }
    if (err < 0) {
        return (int)MAP_FAILED;
    }

    if (addr == NULL) {
        uintptr_t newaddr = (uintptr_t)PN_TO_ADDR(area->vma_start);
        if (ret) {
            *ret = (void *)newaddr;
        }
        tlb_flush_range(newaddr, LEN_TO_PAGES(len));
    } else {
        if (ret) {
            *ret = addr;
        }
        tlb_flush_range((uintptr_t)addr, LEN_TO_PAGES(len));
    }
    return 0;
        /*NOT_YET_IMPLEMENTED("VM: do_mmap");*/
        /*return -1;*/
}


/*
 * This function implements the munmap(2) syscall.
 *
 * As with do_mmap() it should perform the required error checking,
 * before calling upon vmmap_remove() to do most of the work.
 * Remember to clear the TLB.
 */
int
do_munmap(void *addr, size_t len)
{
    uintptr_t vaddr = (uintptr_t)addr;
    if (!PAGE_ALIGNED(vaddr)) {
        return -EINVAL;
    }

    uint32_t lopage = ADDR_TO_PN(vaddr);
    uint32_t npages = LEN_TO_PAGES(len);

    int ret = vmmap_remove(curproc->p_vmmap, lopage, npages);
    tlb_flush_range(vaddr, npages);
    pagedir_t *pd = pt_get();
    pt_unmap_range(pd, vaddr, (uintptr_t)PN_TO_ADDR(lopage + npages));
    return ret;
        /*NOT_YET_IMPLEMENTED("VM: do_munmap");*/
        /*return -1;*/
}

