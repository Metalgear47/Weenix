#include "kernel.h"
#include "globals.h"
#include "types.h"
#include "errno.h"

#include "util/string.h"
#include "util/printf.h"
#include "util/debug.h"

#include "fs/dirent.h"
#include "fs/fcntl.h"
#include "fs/stat.h"
#include "fs/vfs.h"
#include "fs/vnode.h"

#include "mm/kmalloc.h"
/*kmalloc is included by myself*/

/* This takes a base 'dir', a 'name', its 'len', and a result vnode.
 * Most of the work should be done by the vnode's implementation
 * specific lookup() function, but you may want to special case
 * "." and/or ".." here depnding on your implementation.
 *
 * If dir has no lookup(), return -ENOTDIR.
 *
 * Note: returns with the vnode refcount on *result incremented.
 */
int
lookup(vnode_t *dir, const char *name, size_t len, vnode_t **result)
{
    KASSERT(dir != NULL);
    KASSERT(name != NULL);

    dbg(DBG_VFS, "lookup: vnode 0x%p, name is: %s, namelen is: %u.\n", dir, name, len);

    KASSERT(dir->vn_ops);
    if (dir->vn_ops->lookup == NULL) {
        dbg(DBG_VFS, "lookup: vnode_t *dir is not a directory.\n");
        *result = NULL;
        return -ENOTDIR;
    }
    /*if (name_match(".", name, 1) == 0 || name_match("..", name, 2) == 0) {*/
    /*
     *if (name_match(".", name, 1) == 0) {
     *    dbg(DBG_VFS, "the name matches '.' \n");
     *    *result = dir;
     *    return 0;
     *}
     */

    dbg(DBG_VFS, "lookup: gonna call vnode's lookup function.\n");
    int err = dir->vn_ops->lookup(dir, name, len, result);
    if (err < 0) {
        dbg(DBG_VFS, "dir_vnode's lookup did not find out the vnode\n");
        *result = NULL;
        return err;
    }
    return err;

    /*don't know why I need to special case "." and ".."*/

        /*
         *NOT_YET_IMPLEMENTED("VFS: lookup");
         *return 0;
         */
}


/* When successful this function returns data in the following "out"-arguments:
 *  o res_vnode: the vnode of the parent directory of "name"
 *  o name: the `basename' (the element of the pathname)
 *  o namelen: the length of the basename
 *
 * For example: dir_namev("/s5fs/bin/ls", &namelen, &name, NULL,
 * &res_vnode) would put 2 in namelen, "ls" in name, and a pointer to the
 * vnode corresponding to "/s5fs/bin" in res_vnode.
 *
 * The "base" argument defines where we start resolving the path from:
 * A base value of NULL means to use the process's current working directory,
 * curproc->p_cwd.  If pathname[0] == '/', ignore base and start with
 * vfs_root_vn.  dir_namev() should call lookup() to take care of resolving each
 * piece of the pathname.
 *
 * Note: A successful call to this causes vnode refcount on *res_vnode to
 * be incremented.
 */
int
dir_namev(const char *pathname, size_t *namelen, const char **name,
          vnode_t *base, vnode_t **res_vnode)
{
    /*get the intermediate vnode and vput?*/

    KASSERT(pathname);
    KASSERT(namelen);
    KASSERT(name);
    KASSERT(res_vnode);

    dbg(DBG_VFS, "called with pathname %s\n", pathname);

    /*stores errno*/
    int err = 0;

    /*stores pointer to current searching dir*/
    vnode_t *curdir;
    
    /*index in pathname*/
    int i = 0;

    /*index in (base)name*/
    *namelen = 0;

    /*convert it to a non-const pointer*/
    char *basename = (char *)*name;

    if (pathname[0] == '\0') {
        dbg(DBG_VFS, "the pathname is empty\n");
        *res_vnode = NULL;
        return -EINVAL;
    }

    if (pathname[0] == '/') {
        curdir = vfs_root_vn;

        /*skip the first '/'*/
        while (pathname[i] == '/') {
            i++;
        }
        
        if (pathname[i] == '\0') {
            *res_vnode = vfs_root_vn;
            /*some doubt about it*/
            vref(*res_vnode);
            dbg(DBG_VFS, "pathname is just root.\n");
            return 0;
        }

        basename[(*namelen)++] = pathname[i++];
    } else {
        if (base != NULL) {
            curdir = base;
        } else {
            curdir = curproc->p_cwd;
        }
    }

    *res_vnode = curdir;
    vref(curdir);
    while (pathname[i] != '\0') {
        if (pathname[i] == '/') {
            /*do nothing for now*/
        } else {
            if (i == 0 || pathname[i-1] != '/'){
                if ((*namelen) >= NAME_LEN) {
                    vput(curdir);
                    dbg(DBG_VFS, "dir_namev: the name is too long.\n");
                    *res_vnode = NULL;
                    return -ENAMETOOLONG;
                }
                basename[*namelen] = pathname[i];
                (*namelen)++;
            } else {
                /*just for dbg printing in lookup*/
                basename[*namelen] = NULL;
                dbg(DBG_VFS, "dir_namev: start lookup.\n");
                if ((err = lookup(curdir, *name, *namelen, res_vnode)) < 0) {
                    dbg(DBG_VFS, "dir_namev: lookup fail, errno is: %d\n", err);
                    vput(curdir);
                    *res_vnode = NULL;
                    return err;
                }

                vput(curdir);
                curdir = *res_vnode;
                *namelen = 0;
                basename[*namelen] = pathname[i];
                (*namelen)++;
            }
        }
        i++;
    }

    basename[*namelen] = '\0';

    dbg(DBG_VFS, "dir_namev: successfully find the parent directory\n");
    dbg(DBG_VFS, "name: %s, namelen: %u\n", basename, *namelen);
    return 0;
        /*
         *NOT_YET_IMPLEMENTED("VFS: dir_namev");
         *return 0;
         */
}

/* This returns in res_vnode the vnode requested by the other parameters.
 * It makes use of dir_namev and lookup to find the specified vnode (if it
 * exists).  flag is right out of the parameters to open(2); see
 * <weenix/fnctl.h>.  If the O_CREAT flag is specified, and the file does
 * not exist call create() in the parent directory vnode.
 *
 * Note: Increments vnode refcount on *res_vnode.
 */
int
open_namev(const char *pathname, int flag, vnode_t **res_vnode, vnode_t *base)
{
    KASSERT(pathname);
    
    dbg(DBG_VFS, "called with pathname %s, flag 0x%12x\n", pathname, flag);

    size_t namelen;
    const char *name = (const char *)kmalloc(sizeof(char) * (NAME_LEN + 1));
    KASSERT(name && "Ran out of kernel memory.\n");
    vnode_t *vn_dir;
    int err = 0;

    if ((err = dir_namev(pathname, &namelen, &name, base, &vn_dir)) < 0) {
        kfree((void *)name);
        dbg(DBG_VFS, "The dir doesn't exist\n");
        *res_vnode = NULL;
        return err;
    }

    if (!S_ISDIR(vn_dir->vn_mode)) {
        kfree((void *)name);
        dbg(DBG_VFS, "it's not a directory.\n");
        vput(vn_dir);
        *res_vnode = NULL;
        return -ENOTDIR;
    }

    if ((err = lookup(vn_dir, name, namelen, res_vnode)) < 0) {
        /*examine errno?*/
        if (flag & O_CREAT) {
            dbg(DBG_VFS, "file doesn't exist, call vnode->create function\n");
            if ((err = vn_dir->vn_ops->create(vn_dir, name, namelen, res_vnode)) < 0) {
                vput(vn_dir);
                kfree((void *)name);
                dbg(DBG_VFS, "call vnode->create failed\n");
                *res_vnode = NULL;
                return err;
            }
        } else {
            vput(vn_dir);
            kfree((void *)name);
            dbg(DBG_VFS, "file doesn't exist, and no O_CREAT flag\n");
            *res_vnode = NULL;
            return err;
        }
    }

    kfree((void *)name);
    dbg(DBG_VFS, "Successfully open the file.\n");
    return 0;
        /*NOT_YET_IMPLEMENTED("VFS: open_namev");*/
        /*return 0;*/
}

#ifdef __GETCWD__
/* Finds the name of 'entry' in the directory 'dir'. The name is writen
 * to the given buffer. On success 0 is returned. If 'dir' does not
 * contain 'entry' then -ENOENT is returned. If the given buffer cannot
 * hold the result then it is filled with as many characters as possible
 * and a null terminator, -ERANGE is returned.
 *
 * Files can be uniquely identified within a file system by their
 * inode numbers. */
int
lookup_name(vnode_t *dir, vnode_t *entry, char *buf, size_t size)
{
        NOT_YET_IMPLEMENTED("GETCWD: lookup_name");
        return -ENOENT;
}


/* Used to find the absolute path of the directory 'dir'. Since
 * directories cannot have more than one link there is always
 * a unique solution. The path is writen to the given buffer.
 * On success 0 is returned. On error this function returns a
 * negative error code. See the man page for getcwd(3) for
 * possible errors. Even if an error code is returned the buffer
 * will be filled with a valid string which has some partial
 * information about the wanted path. */
ssize_t
lookup_dirpath(vnode_t *dir, char *buf, size_t osize)
{
        NOT_YET_IMPLEMENTED("GETCWD: lookup_dirpath");

        return -ENOENT;
}
#endif /* __GETCWD__ */
