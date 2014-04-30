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

#define VALID_ADDR(addr) ((uint32_t)addr >= USER_MEM_LOW && \
                            (uint32_t)addr < USER_MEM_HIGH)
    /*EACCES*/
    /*A file descriptor refers to a non-regular file. Or MAP_PRIVATE was requested, but fd is not open for reading. Or MAP_SHARED was requested and PROT_WRITE is set, but fd is not open in read/write (O_RDWR) mode. Or PROT_WRITE is set, but the file is append-only.*/
    /*EAGAIN*/
    /*The file has been locked, or too much memory has been locked (see setrlimit(2)).*/
    /*EBADF*/
    /*fd is not a valid file descriptor (and MAP_ANONYMOUS was not set).*/
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
    /*EINVAL*/
    /*We don't like addr, length, or offset (e.g., they are too large, or not aligned on a page boundary).*/
    /*EINVAL*/
    /*(since Linux 2.6.12) length was 0.*/
    if (!PAGE_ALIGNED(addr) || !PAGE_ALIGNED(len) || !PAGE_ALIGNED(off) || len == 0) {
        return -EINVAL;
    }

    if (!VALID_ADDR(addr) || !VALID_ADDR((uint32_t)addr + len)) {
        return -EINVAL;
    }

    /*EINVAL*/
    /*flags contained neither MAP_PRIVATE or MAP_SHARED, or contained both of these values.*/
    int map_type = flags & MAP_TYPE;
    if (map_type == 0 || map_type == MAP_TYPE) {
        return -EINVAL;
    }


        NOT_YET_IMPLEMENTED("VM: do_mmap");
        return -1;
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
        NOT_YET_IMPLEMENTED("VM: do_munmap");
        return -1;
}

