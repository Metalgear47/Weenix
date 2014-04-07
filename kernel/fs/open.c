/*
 *  FILE: open.c
 *  AUTH: mcc | jal
 *  DESC:
 *  DATE: Mon Apr  6 19:27:49 1998
 */

#include "globals.h"
#include "errno.h"
#include "fs/fcntl.h"
#include "util/string.h"
#include "util/printf.h"
#include "fs/vfs.h"
#include "fs/vnode.h"
#include "fs/file.h"
#include "fs/vfs_syscall.h"
#include "fs/open.h"
#include "fs/stat.h"
#include "util/debug.h"

/* find empty index in p->p_files[] */
int
get_empty_fd(proc_t *p)
{
        int fd;

        for (fd = 0; fd < NFILES; fd++) {
                if (!p->p_files[fd])
                        return fd;
        }

        dbg(DBG_ERROR | DBG_VFS, "ERROR: get_empty_fd: out of file descriptors "
            "for pid %d\n", curproc->p_pid);
        return -EMFILE;
}

/*
 * There a number of steps to opening a file:
 *      1. Get the next empty file descriptor.
 *      2. Call fget to get a fresh file_t.
 *      3. Save the file_t in curproc's file descriptor table.
 *      4. Set file_t->f_mode to OR of FMODE_(READ|WRITE|APPEND) based on
 *         oflags, which can be O_RDONLY, O_WRONLY or O_RDWR, possibly OR'd with
 *         O_APPEND.
 *      5. Use open_namev() to get the vnode for the file_t.
 *      6. Fill in the fields of the file_t.
 *      7. Return new fd.
 *
 * If anything goes wrong at any point (specifically if the call to open_namev
 * fails), be sure to remove the fd from curproc, fput the file_t and return an
 * error.
 *
 * Error cases you must handle for this function at the VFS level:
 *      x EINVAL
 *        oflags is not valid.
 *      x EMFILE
 *        The process already has the maximum number of files open.
 *      x ENOMEM
 *        Insufficient kernel memory was available.
 *      x ENAMETOOLONG
 *        A component of filename was too long.
 *      x ENOENT
 *        O_CREAT is not set and the named file does not exist.  Or, a
 *        directory component in pathname does not exist.
 *      x EISDIR
 *        pathname refers to a directory and the access requested involved
 *        writing (that is, O_WRONLY or O_RDWR is set).
 *      o ENXIO
 *        pathname refers to a device special file and no corresponding device
 *        exists.
 */

int
do_open(const char *filename, int oflags)
{
    KASSERT(filename);

    dbg(DBG_VFS, "called with filename: %s, oflags: 0x%12x\n", filename, oflags);

    /*validate oflags*/
    int lower_mask = 0x100 - 1;
    int higher_mask = ~0x7FF;
    if (oflags < 0 || (oflags & lower_mask) > 2 || (oflags & higher_mask)) {
        dbg(DBG_VFS, "oflags are invalid\n");
        return -EINVAL;
    }

    /*get file descriptor*/
    int fd;
    if ((fd = get_empty_fd(curproc)) == -EMFILE) {
        dbg(DBG_VFS, "too many open files.\n");
        return -EMFILE;
    }

    /*get a fresh file_t*/
    file_t *f = fget(-1);
    if (f == NULL) {
        dbg(DBG_VFS, "not enough memory\n");
        return -ENOMEM;
    }

    /*save file_t in file descriptor table*/
    curproc->p_files[fd] = f;

    /*set f_mode*/
    int rw = oflags & lower_mask;
    if (rw == O_RDONLY) {
        f->f_mode |= FMODE_READ;
    } else if (rw == O_WRONLY) {
        f->f_mode |= FMODE_WRITE;
    } else {
        f->f_mode |= FMODE_READ | FMODE_WRITE;
    }

    if (oflags & O_APPEND) {
        f->f_mode |= FMODE_APPEND;
    }

    /*get the vnode*/
    vnode_t *vn;
    dbg(DBG_VFS, "about to call open_namev\n");
    int err = open_namev(filename, oflags, &vn, NULL);
    if (err < 0) {
        /*clean up*/
        curproc->p_files[fd] = NULL;
        fput(f);
        return err;
    }

    /*handle if it's a directory*/
    if (S_ISDIR(vn->vn_mode)) {
        if ((oflags & O_WRONLY) || (oflags & O_RDWR)) {
            vput(vn);
            fput(f);
            curproc->p_files[fd] = NULL;
            dbg(DBG_VFS, "it's a directory and write flag set\n");
            return -EISDIR;
        }
    }

    /*initialize fields of file_t*/

    /*f_pos*/
    f->f_pos = 0;

    /*f_mode: already set before*/

    /*f_refcount: already incremented in fget*/

    /*f_vnode*/
    f->f_vnode = vn;

    dbg(DBG_VFS, "succeed, the file discriptor is %d\n", fd);
    return fd;
        /*
         *NOT_YET_IMPLEMENTED("VFS: do_open");
         *return -1;
         */
}
