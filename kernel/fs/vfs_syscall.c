/*
 *  FILE: vfs_syscall.c
 *  AUTH: mcc | jal
 *  DESC:
 *  DATE: Wed Apr  8 02:46:19 1998
 *  $Id: vfs_syscall.c,v 1.9.2.2 2006/06/04 01:02:32 afenn Exp $
 */

#include "kernel.h"
#include "errno.h"
#include "globals.h"
#include "fs/vfs.h"
#include "fs/file.h"
#include "fs/vnode.h"
#include "fs/vfs_syscall.h"
#include "fs/open.h"
#include "fs/fcntl.h"
#include "fs/lseek.h"
#include "mm/kmalloc.h"
#include "util/string.h"
#include "util/printf.h"
#include "fs/stat.h"
#include "util/debug.h"

/* To read a file:
 *      o fget(fd)
 *      o call its virtual read f_op
 *      o update f_pos
 *      o fput() it
 *      o return the number of bytes read, or an error
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EBADF
 *        fd is not a valid file descriptor or is not open for reading.
 *      o EISDIR
 *        fd refers to a directory.
 *
 * In all cases, be sure you do not leak file refcounts by returning before
 * you fput() a file that you fget()'ed.
 */
int
do_read(int fd, void *buf, size_t nbytes)
{
    KASSERT(buf);

    if (fd <= -1 || fd >= NFILES) {
        return -EBADF;
    }

    file_t *f;
    f = fget(fd);

    if (f == NULL) {
        /*not a valid fd*/
        return -EBADF;
        /*no need to fput*/
    }

    /*how about not open for reading*/
    if ((f->f_mode & FMODE_READ) == 0) {
        fput(f);
        return -EBADF;
    }

    /*examine if it's dir*/
    if (S_ISDIR(f->f_vnode->vn_mode)) {
        fput(f);
        return -EISDIR;
    }

    /*call virtual read op*/
    int readlen = f->f_vnode->vn_ops->read(f->f_vnode, f->f_pos, buf, nbytes);
    f->f_pos += readlen;

    /*fput it*/
    fput(f);

    return readlen;
        /*NOT_YET_IMPLEMENTED("VFS: do_read");*/
        /*return -1;*/
}

/* Very similar to do_read.  Check f_mode to be sure the file is writable.  If
 * f_mode & FMODE_APPEND, do_lseek() to the end of the file, call the write
 * f_op, and fput the file.  As always, be mindful of refcount leaks.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EBADF
 *        fd is not a valid file descriptor or is not open for writing.
 */
int
do_write(int fd, const void *buf, size_t nbytes)
{
    KASSERT(buf);

    if (fd < 0) {
        return -EBADF;
    }

    file_t *f;
    f = fget(fd);

    if (f == NULL) {
        return -EBADF;
    }

    if ((f->f_mode & FMODE_WRITE) == 0) {
        fput(f);
        return -EBADF;
    }

    if (f->f_mode & FMODE_APPEND) {
        do_lseek(fd, 0, SEEK_END);
    }

    int writelen = f->f_vnode->vn_ops->write(f->f_vnode, f->f_pos, buf, nbytes);
    f->f_pos += writelen;

    fput(f);

    return writelen;
        /*NOT_YET_IMPLEMENTED("VFS: do_write");*/
        /*return -1;*/
}

/*
 * Zero curproc->p_files[fd], and fput() the file. Return 0 on success
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EBADF
 *        fd isn't a valid open file descriptor.
 */
int
do_close(int fd)
{
    if (fd < 0 || fd >= NFILES ) {
        return -EBADF;
    }

    file_t *f = curproc->p_files[fd];
    if (f == NULL) {
        return -EBADF;
    }

    curproc->p_files[fd] = NULL;
    fput(f);

    return 0;
        /*NOT_YET_IMPLEMENTED("VFS: do_close");*/
        /*return -1;*/
}

/* To dup a file:
 *      o fget(fd) to up fd's refcount
 *      o get_empty_fd()
 *      o point the new fd to the same file_t* as the given fd
 *      o return the new file descriptor
 *
 * Don't fput() the fd unless something goes wrong.  Since we are creating
 * another reference to the file_t*, we want to up the refcount.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EBADF
 *        fd isn't an open file descriptor.
 *      o EMFILE
 *        The process already has the maximum number of file descriptors open
 *        and tried to open a new one.
 */
int
do_dup(int fd)
{
    if (fd == -1) {
        return -EBADF;
    }

    file_t *f = fget(fd);
    if (f == NULL) {
        return -EBADF;
    }

    int newfd = get_empty_fd(curproc);
    if (newfd < 0) {
        fput(f);
        return -EMFILE;
    }

    curproc->p_files[newfd] = f;

    return newfd;
        /*NOT_YET_IMPLEMENTED("VFS: do_dup");*/
        /*return -1;*/
}

/* Same as do_dup, but insted of using get_empty_fd() to get the new fd,
 * they give it to us in 'nfd'.  If nfd is in use (and not the same as ofd)
 * do_close() it first.  Then return the new file descriptor.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EBADF
 *        ofd isn't an open file descriptor, or nfd is out of the allowed
 *        range for file descriptors.
 */
int
do_dup2(int ofd, int nfd)
{
    if (ofd == -1) {
        return -EBADF;
    }

    file_t *f = fget(ofd);
    if (f == NULL) {
        return -EBADF;
    }

    if (nfd < 0 || nfd >= NFILES) {
        fput(f);
        return -EBADF;
    }

    if (ofd == nfd) {
        return nfd;
    }

    /*look it up in the table or fget?*/
    file_t *nf = curproc->p_files[nfd];
    if (nf) {
        do_close(nfd);
    }

    curproc->p_files[nfd] = f;

    return nfd;
        /*NOT_YET_IMPLEMENTED("VFS: do_dup2");*/
        /*return -1;*/
}

/*
 * This routine creates a special file of the type specified by 'mode' at
 * the location specified by 'path'. 'mode' should be one of S_IFCHR or
 * S_IFBLK (you might note that mknod(2) normally allows one to create
 * regular files as well-- for simplicity this is not the case in Weenix).
 * 'devid', as you might expect, is the device identifier of the device
 * that the new special file should represent.
 *
 * You might use a combination of dir_namev, lookup, and the fs-specific
 * mknod (that is, the containing directory's 'mknod' vnode operation).
 * Return the result of the fs-specific mknod, or an error.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EINVAL
 *        mode requested creation of something other than a device special
 *        file.
 *      o EEXIST
 *        path already exists.
 *      o ENOENT
 *        A directory component in path does not exist.
 *      o ENOTDIR
 *        A component used as a directory in path is not, in fact, a directory.
 *      o ENAMETOOLONG
 *        A component of path was too long.
 */
int
do_mknod(const char *path, int mode, unsigned devid)
{
    KASSERT(path);
    dbg(DBG_VFS, "called with path %s, mode 0x%12x\n", path, mode);

    if (!S_ISCHR(mode) && !S_ISBLK(mode)) {
        dbg(DBG_VFS, "invalid mode argument\n");
        return -EINVAL;
    }

    int err = 0;
    size_t namelen;
    const char *name = (const char *) kmalloc(sizeof(char) * (NAME_LEN + 1));
    KASSERT(name && "Ran out of kernel memory.\n");
    vnode_t *dir_vnode;

    dbg(DBG_VFS, "about to call dir_namev\n");
    err = dir_namev(path, &namelen, &name, NULL, &dir_vnode);
    if (err < 0) {
        dbg(DBG_VFS, "dir_namev failed, errno is %d.\n", err);
        return err;
    }

    vnode_t *file_vnode;
    dbg(DBG_VFS, "about to call lookup\n");
    err = lookup(dir_vnode, name, namelen, &file_vnode);
    if (err == 0) {
        vput(dir_vnode);
        vput(file_vnode);
        dbg(DBG_VFS, "the file already exists\n");
        return -EEXIST;
    }

    dbg(DBG_VFS, "about to call vnode->mknod\n");
    err = dir_vnode->vn_ops->mknod(dir_vnode, name, namelen, mode, devid);
    if (err < 0) {
        vput(dir_vnode);
        dbg(DBG_VFS, "vnode->mknod failed, errno is %d\n", err);
        return err;
    }
    vput(dir_vnode);
    dbg(DBG_VFS, "vnode->mknod succeed\n");
    return err;
        /*
         *NOT_YET_IMPLEMENTED("VFS: do_mknod");
         *return -1;
         */
}

/* Use dir_namev() to find the vnode of the dir we want to make the new
 * directory in.  Then use lookup() to make sure it doesn't already exist.
 * Finally call the dir's mkdir vn_ops. Return what it returns.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EEXIST
 *        path already exists.
 *      o ENOENT
 *        A directory component in path does not exist.
 *      o ENOTDIR
 *        A component used as a directory in path is not, in fact, a directory.
 *      o ENAMETOOLONG
 *        A component of path was too long.
 */
int
do_mkdir(const char *path)
{
    KASSERT(path);

    int err = 0;
    size_t namelen;
    const char *name = (const char*) kmalloc(sizeof(char) * (NAME_LEN + 1));
    KASSERT(name && "Ran out of kernel memory.\n");
    vnode_t *dir_vnode;

    dbg(DBG_VFS, "do_mkdir: call dir_namev with path: %s.\n", path);
    err = dir_namev(path, &namelen, &name, NULL, &dir_vnode);
    if (err < 0) {
        /*seems no need to worry about vput(dir_vnode) here*/
        kfree((void *)name);
        return err;
    }

    vnode_t *file_vnode;
    dbg(DBG_VFS, "do_mkdir: call lookup.\n");
    err = lookup(dir_vnode, name, namelen, &file_vnode);
    if (err == 0) {
        KASSERT(file_vnode);
        vput(dir_vnode);
        vput(file_vnode);
        kfree((void *)name);
        return -EEXIST;
    }

    dbg(DBG_VFS, "The err no for lookup is: %d\n", err);
    if (err != -ENOENT) {
        vput(dir_vnode);
        return err;
    }
    KASSERT(err == -ENOENT);

    dbg(DBG_VFS, "do_mkdir: call vnode's mkdir\n");
    err = dir_vnode->vn_ops->mkdir(dir_vnode, name, namelen);
    if (err < 0) {
        vput(dir_vnode);
        kfree((void *)name);
        dbg(DBG_VFS, "call vnode->mkdir failed, errno is %d\n", err);
        return err;
    }

    kfree((void *)name);
    vput(dir_vnode);
    dbg(DBG_VFS, "mkdir succeed.\n");
    return err;
        /*NOT_YET_IMPLEMENTED("VFS: do_mkdir");*/
        /*return -1;*/
}

/* Use dir_namev() to find the vnode of the directory containing the dir to be
 * removed. Then call the containing dir's rmdir v_op.  The rmdir v_op will
 * return an error if the dir to be removed does not exist or is not empty, so
 * you don't need to worry about that here. Return the value of the v_op,
 * or an error.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EINVAL
 *        path has "." as its final component.
 *      o ENOTEMPTY
 *        path has ".." as its final component.
 *      o ENOENT
 *        A directory component in path does not exist.
 *      o ENOTDIR
 *        A component used as a directory in path is not, in fact, a directory.
 *      o ENAMETOOLONG
 *        A component of path was too long.
 */
int
do_rmdir(const char *path)
{
    KASSERT(path);

    int err = 0;
    size_t namelen;
    const char *name = (const char *) kmalloc(sizeof(char) * (NAME_LEN + 1));
    KASSERT(name && "Ran out of kernel memory.\n");
    vnode_t *dir_vnode;

    err = dir_namev(path, &namelen, &name, NULL, &dir_vnode);
    if (err < 0) {
        kfree((void *)name);
        return err;
    }

    if name_match(".", name, namelen) {
        vput(dir_vnode);
        kfree((void *)name);
        return -EINVAL;
    }

    if name_match("..", name, namelen) {
        vput(dir_vnode);
        kfree((void *)name);
        return -ENOTEMPTY;
    }

    vnode_t *child_vnode;
    err = lookup(dir_vnode, name, namelen, &child_vnode);
    if (err < 0) {
        vput(dir_vnode);
        kfree((void *)name);
        return err;
    }

    if (!S_ISDIR(child_vnode->vn_mode)) {
        vput(dir_vnode);
        kfree((void *)name);
        return -ENOTDIR;
    }

    err = dir_vnode->vn_ops->rmdir(dir_vnode, name, namelen);
    kfree((void *)name);
    vput(dir_vnode);
    return err;
        /*NOT_YET_IMPLEMENTED("VFS: do_rmdir");*/
        /*return -1;*/
}

/*
 * Same as do_rmdir, but for files.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EISDIR
 *        path refers to a directory.
 *      o ENOENT
 *        A component in path does not exist.
 *      o ENOTDIR
 *        A component used as a directory in path is not, in fact, a directory.
 *      o ENAMETOOLONG
 *        A component of path was too long.
 */
int
do_unlink(const char *path)
{
    KASSERT(path);

    int err = 0;
    size_t namelen;
    const char *name = (const char *) kmalloc(sizeof(char) * (NAME_LEN + 1));
    KASSERT(name && "Ran out of kernel memory.\n");
    vnode_t *dir_vnode;

    err = dir_namev(path, &namelen, &name, NULL, &dir_vnode);
    if (err < 0) {
        kfree((void *)name);
        return err;
    }

    vnode_t *file_vnode;
    err = lookup(dir_vnode, name, namelen, &file_vnode);
    if (err < 0) {
        vput(dir_vnode);
        kfree((void *)name);
        return err;
    }
    if (S_ISDIR(file_vnode->vn_mode)) {
        vput(dir_vnode);
        kfree((void *)name);
        vput(file_vnode);
        return -EPERM;
    }

    err = dir_vnode->vn_ops->unlink(dir_vnode, name, namelen);
    kfree((void *)name);
    vput(dir_vnode);
    return err;
        /*NOT_YET_IMPLEMENTED("VFS: do_unlink");*/
        /*return -1;*/
}

/* To link:
 *      o open_namev(from)
 *      o dir_namev(to)
 *      o call the destination dir's (to) link vn_ops.
 *      o return the result of link, or an error
 *
 * Remember to vput the vnodes returned from open_namev and dir_namev.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EEXIST
 *        to already exists.
 *      o ENOENT
 *        A directory component in from or to does not exist.
 *      o ENOTDIR
 *        A component used as a directory in from or to is not, in fact, a
 *        directory.
 *      o ENAMETOOLONG
 *        A component of from or to was too long.
 */
int
do_link(const char *from, const char *to)
{
    KASSERT(from);
    KASSERT(to);

    int err = 0;
    vnode_t *from_vnode;

    /*get the vnode for from*/
    err = open_namev(from, O_RDONLY, &from_vnode, NULL);
    if (err < 0) {
        return err;
    }

    size_t namelen;
    const char *name = (const char *)kmalloc(sizeof(char) * (NAME_LEN + 1));
    KASSERT(name && "Ran out of kernel memory.\n");
    vnode_t *todir_vnode;

    /*get the vnode for to's containing dir*/
    err = dir_namev(to, &namelen, &name, NULL, &todir_vnode);
    if (err < 0) {
        vput(from_vnode);
        kfree((void *)name);

        return err;
    }

    vnode_t *to_vnode;
    err = lookup(todir_vnode, name, namelen, &to_vnode);
    if (err == 0) {
        KASSERT(to_vnode);

        vput(from_vnode);
        vput(todir_vnode);
        vput(to_vnode);
        kfree((void *)name);

        return -EEXIST;
    }

    err = todir_vnode->vn_ops->link(from_vnode, todir_vnode, name, namelen);

    vput(from_vnode);
    vput(todir_vnode);
    kfree((void *)name);

    return err;
        /*NOT_YET_IMPLEMENTED("VFS: do_link");*/
        /*return -1;*/
}

/*      o link newname to oldname
 *      o unlink oldname
 *      o return the value of unlink, or an error
 *
 * Note that this does not provide the same behavior as the
 * Linux system call (if unlink fails then two links to the
 * file could exist).
 */
int
do_rename(const char *oldname, const char *newname)
{
    KASSERT(oldname);
    KASSERT(newname);

    int err = 0;

    err = do_link(oldname, newname);
    if (err < 0) {
        return err;
    }

    return do_unlink(oldname);
        /*NOT_YET_IMPLEMENTED("VFS: do_rename");*/
        /*return -1;*/
}

/* Make the named directory the current process's cwd (current working
 * directory).  Don't forget to down the refcount to the old cwd (vput()) and
 * up the refcount to the new cwd (open_namev() or vget()). Return 0 on
 * success.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o ENOENT
 *        path does not exist.
 *      o ENAMETOOLONG
 *        A component of path was too long.
 *      o ENOTDIR
 *        A component of path is not a directory.
 */
int
do_chdir(const char *path)
{
    KASSERT(path);

    int err = 0;
    vnode_t *new_vnode;
    
    err = open_namev(path, O_RDONLY, &new_vnode, NULL);
    if (err < 0) {
        return err;
    }

    if (!S_ISDIR(new_vnode->vn_mode)) {
        vput(new_vnode);
        return -ENOTDIR;
    }

    vput(curproc->p_cwd);
    curproc->p_cwd = new_vnode;

    return 0;
        /*NOT_YET_IMPLEMENTED("VFS: do_chdir");*/
        /*return -1;*/
}

/* Call the readdir f_op on the given fd, filling in the given dirent_t*.
 * If the readdir f_op is successful, it will return a positive value which
 * is the number of bytes copied to the dirent_t.  You need to increment the
 * file_t's f_pos by this amount.  As always, be aware of refcounts, check
 * the return value of the fget and the virtual function, and be sure the
 * virtual function exists (is not null) before calling it.
 *
 * Return either 0 or sizeof(dirent_t), or -errno.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EBADF
 *        Invalid file descriptor fd.
 *      o ENOTDIR
 *        File descriptor does not refer to a directory.
 */
int
do_getdent(int fd, struct dirent *dirp)
{
    KASSERT(dirp);
    if (fd == -1) {
        dbg(DBG_VFS, "Bad file descriptor\n");
        return -EBADF;
    }
    
    file_t *f = fget(fd);
    if (f == NULL) {
        dbg(DBG_VFS, "Bad file descriptor\n");
        return -EBADF;
    }
    dbg(DBG_VFS, "the fd is %d\n", fd);

    vnode_t *dir_vn = f->f_vnode;
    KASSERT(dir_vn);
    if (!S_ISDIR(dir_vn->vn_mode)) {
        fput(f);
        dbg(DBG_VFS, "fd %d is not pointing to a directory.\n", fd);
        return -ENOTDIR;
    }

    if (dir_vn->vn_ops->readdir == NULL) {
        fput(f);
        dbg(DBG_VFS, "The readdir function pointer is NULL\n");
        return -ENOTDIR;
    }

    int offset = 0;
    offset = dir_vn->vn_ops->readdir(dir_vn, f->f_pos, dirp);
    /*offset = dir_vn->vn_ops->readdir(dir_vn, 0, dirp);*/
    f->f_pos += offset;
    dbg(DBG_VFS, "the returning offset is: %d\n", offset);

    fput(f);
    if (offset == 0) {
        return 0;
    }
    return sizeof(dirent_t);
        /*NOT_YET_IMPLEMENTED("VFS: do_getdent");*/
        /*return -1;*/
}

/*
 * Modify f_pos according to offset and whence.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EBADF
 *        fd is not an open file descriptor.
 *      o EINVAL
 *        whence is not one of SEEK_SET, SEEK_CUR, SEEK_END; or the resulting
 *        file offset would be negative.
 */
int
do_lseek(int fd, int offset, int whence)
{
    if (fd < 0 || fd >= NFILES) {
        return -EBADF;
    }

    file_t *f;
    f = fget(fd);

    if (f == NULL) {
        /*not an open fd*/
        return -EBADF;
    }

    /*invalid whence*/
    if (whence < 0 || whence > 2) {
        fput(f);
        return -EINVAL;
    }

    /*compute new offset according to whence*/
    int result;
    if (whence == SEEK_SET) {
        result = offset;
    }
    if (whence == SEEK_CUR) {
        result = f->f_pos + offset;
    }
    if (whence == SEEK_END) {
        result = f->f_vnode->vn_len + offset;
    }

    /*invalid offset*/
    if (result < 0) {
        fput(f);
        return -EINVAL;
    }

    /*update offset and return*/
    f->f_pos = result;
    fput(f);
    return result;
        /*NOT_YET_IMPLEMENTED("VFS: do_lseek");*/
        /*return -1;*/
}

/*
 * Find the vnode associated with the path, and call the stat() vnode operation.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o ENOENT
 *        A component of path does not exist.
 *      o ENOTDIR
 *        A component of the path prefix of path is not a directory.
 *      o ENAMETOOLONG
 *        A component of path was too long.
 */
int
do_stat(const char *path, struct stat *buf)
{
    KASSERT(path);
    KASSERT(buf);

    vnode_t *vnode;
    int err = 0;

    err = open_namev(path, O_RDONLY, &vnode, NULL);
    if (err < 0) {
        return err;
    }

    return vnode->vn_ops->stat(vnode, buf);
        /*NOT_YET_IMPLEMENTED("VFS: do_stat");*/
        /*return -1;*/
}

#ifdef __MOUNTING__
/*
 * Implementing this function is not required and strongly discouraged unless
 * you are absolutely sure your Weenix is perfect.
 *
 * This is the syscall entry point into vfs for mounting. You will need to
 * create the fs_t struct and populate its fs_dev and fs_type fields before
 * calling vfs's mountfunc(). mountfunc() will use the fields you populated
 * in order to determine which underlying filesystem's mount function should
 * be run, then it will finish setting up the fs_t struct. At this point you
 * have a fully functioning file system, however it is not mounted on the
 * virtual file system, you will need to call vfs_mount to do this.
 *
 * There are lots of things which can go wrong here. Make sure you have good
 * error handling. Remember the fs_dev and fs_type buffers have limited size
 * so you should not write arbitrary length strings to them.
 */
int
do_mount(const char *source, const char *target, const char *type)
{
        NOT_YET_IMPLEMENTED("MOUNTING: do_mount");
        return -EINVAL;
}

/*
 * Implementing this function is not required and strongly discouraged unless
 * you are absolutley sure your Weenix is perfect.
 *
 * This function delegates all of the real work to vfs_umount. You should not worry
 * about freeing the fs_t struct here, that is done in vfs_umount. All this function
 * does is figure out which file system to pass to vfs_umount and do good error
 * checking.
 */
int
do_umount(const char *target)
{
        NOT_YET_IMPLEMENTED("MOUNTING: do_umount");
        return -EINVAL;
}
#endif
