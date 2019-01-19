/*
 *   FILE: s5fs_subr.c
 * AUTHOR: afenn
 *  DESCR:
 *  $Id: s5fs_subr.c,v 1.1.2.1 2006/06/04 01:02:15 afenn Exp $
 */

#include "kernel.h"
#include "util/debug.h"
#include "mm/kmalloc.h"
#include "globals.h"
#include "proc/sched.h"
#include "proc/kmutex.h"
#include "errno.h"
#include "util/string.h"
#include "util/printf.h"
#include "mm/pframe.h"
#include "mm/mmobj.h"
#include "drivers/dev.h"
#include "drivers/blockdev.h"
#include "fs/stat.h"
#include "fs/vfs.h"
#include "fs/vnode.h"
#include "fs/s5fs/s5fs_subr.h"
#include "fs/s5fs/s5fs.h"
#include "mm/mm.h"
#include "mm/page.h"

#define dprintf(...) dbg(DBG_S5FS, __VA_ARGS__)
#define S5_MAX_FILE_SIZE        S5_MAX_FILE_BLOCKS * S5_BLOCK_SIZE

#define s5_dirty_super(fs)                                           \
        do {                                                         \
                pframe_t *p;                                         \
                int err;                                             \
                pframe_get(S5FS_TO_VMOBJ(fs), S5_SUPER_BLOCK, &p);   \
                KASSERT(p);                                          \
                err = pframe_dirty(p);                               \
                KASSERT(!err                                         \
                        && "shouldn\'t fail for a page belonging "   \
                        "to a block device");                        \
        } while (0)


static void s5_free_block(s5fs_t *fs, int block);
static int s5_alloc_block(s5fs_t *);


/*
 * Return the disk-block number for the given seek pointer (aka file
 * position).
 *
 * If the seek pointer refers to a sparse block, and alloc is false,
 * then return 0. If the seek pointer refers to a sparse block, and
 * alloc is true, then allocate a new disk block (and make the inode
 * point to it) and return it.
 *
 * Be sure to handle indirect blocks!
 *
 * If there is an error, return -errno.
 *
 * You probably want to use pframe_get, pframe_pin, pframe_unpin, pframe_dirty.
 */
int
s5_seek_to_block(vnode_t *vnode, off_t seekptr, int alloc)
{
    if (seekptr < 0) {
        return -EINVAL;
    }

    /*the block number in the file*/
    uint32_t blocknum_file = S5_DATA_BLOCK(seekptr);
    dprintf("s5_seek_to_block is called, vnode is %p, seekptr is %u, alloc is %d, requesting for block No.%u\n", vnode, seekptr, alloc, blocknum_file);

    /*block number exceeds the max number*/
    if (blocknum_file >= S5_MAX_FILE_BLOCKS) {
        /*it should be checked on upper layer. But it would be nice if I also check here.*/
        return -EINVAL;
    }

    int blocknum = 0;
    /*get the file's corresponding inode*/
    s5_inode_t *inode = VNODE_TO_S5INODE(vnode);

    /*get the file system*/
    s5fs_t *fs = VNODE_TO_S5FS(vnode);

    if (blocknum_file < S5_NDIRECT_BLOCKS) {
        /*direct block*/

        /*get the actually block number on disk*/
        blocknum = inode->s5_direct_blocks[blocknum_file];
        if (blocknum == 0) {
            /*sparse block*/

            if (alloc == 0) {

                return 0;
            } else {

                blocknum = s5_alloc_block(fs);
                if (blocknum < 0) {
                    return blocknum;
                }

                inode->s5_direct_blocks[blocknum_file] = blocknum;
                s5_dirty_inode(fs, inode);
                return blocknum;
            }
        } else {
            /*not a sparse block*/

            return blocknum;
        }
    } else {
        /*indirect block*/

        /*not sure about check here*/
        if (((S5_TYPE_DATA == inode->s5_type)
             || (S5_TYPE_DIR == inode->s5_type))) {
            /*the index in indirect blocks*/
            uint32_t blocknum_indirect = blocknum_file - S5_NDIRECT_BLOCKS;

            if (inode->s5_indirect_block) {
                /*indirect block points to a block*/
                pframe_t *ibp;
                uint32_t *b;

                /*get the page frame*/
                pframe_get(S5FS_TO_VMOBJ(fs),
                           (inode->s5_indirect_block),
                           &ibp);

                b = (uint32_t *)(ibp->pf_addr);
                /*the block number on device*/
                blocknum = b[blocknum_indirect];

                if (blocknum == 0) {
                    /*sparse block*/
                    if (alloc == 0) {
                        return 0;
                    } else {
                        pframe_pin(ibp);
                        blocknum = s5_alloc_block(fs);
                        pframe_unpin(ibp);

                        if (blocknum < 0) {
                            return blocknum;
                        } else {
                            /*store it back in the indirect blocks array*/
                            b[blocknum_indirect] = blocknum;

                            pframe_pin(ibp);
                            /*since indirect block is modified, dirty it*/
                            int err = pframe_dirty(ibp);
                            pframe_unpin(ibp);
                            if (err < 0) {
                                return err;
                            }

                            return blocknum;
                        }
                    }
                } else {
                    return blocknum;
                }
            } else {
                if (alloc == 0) {
                    dprintf("no need to allocate, just return 0\n");
                    return 0;
                } else {
                    /*allocate the block for indirect block first*/
                    int indirect_block = s5_alloc_block(fs);
                    if (indirect_block < 0) {
                        dprintf("some error occured during s5_alloc_block, error number is %d\n", indirect_block);
                        return indirect_block;
                    }

                    mmobj_t *o = S5FS_TO_VMOBJ(fs);
                    
                    pframe_t *ibp = NULL;
                    /*load the page frame for indirect block*/
                    int err = pframe_get(o, (uint32_t)indirect_block, &ibp);
                    if (err < 0) {
                        s5_free_block(fs, (uint32_t)indirect_block);
                        panic("it should never fail for block_device vm_obj\n");
                        return err;
                    }

                    /*get the pointer to indirect blocks array*/
                    uint32_t *b = (uint32_t*)(ibp->pf_addr);
                    memset(b, 0, S5_BLOCK_SIZE);

                    pframe_pin(ibp);
                    /*allocate the block for the real data*/
                    blocknum = s5_alloc_block(fs);
                    /*also dirty the ibp because memsetting it*/
                    err = pframe_dirty(ibp);
                    pframe_unpin(ibp);

                    if (blocknum < 0) {
                        /*not sure about free the pframe*/
                        /*pframe_free(ibp);*/
                        s5_free_block(fs, (uint32_t)indirect_block);
                        return blocknum;
                    }

                    if (err < 0) {
                        /*
                         *I free the pframe here because I've already dirty it
                         */
                        /*not sure about free the pframe*/
                        /*pframe_free(ibp);*/
                        s5_free_block(fs, (uint32_t)indirect_block);
                        s5_free_block(fs, (uint32_t)blocknum);
                        return err;
                    }

                    /*update the indirect block number*/
                    inode->s5_indirect_block = (uint32_t)indirect_block;
                    /*update the block number in indirect block*/
                    b[blocknum_indirect] = blocknum;

                    pframe_pin(ibp);
                    /*dirty the page for indirect block*/
                    err = pframe_dirty(ibp);
                    /*dirty the inode*/
                    s5_dirty_inode(fs, inode);
                    pframe_unpin(ibp);
                    if (err < 0) {
                        inode->s5_indirect_block = 0;
                        s5_dirty_inode(fs, inode);
                        /*pframe_free(ibp);*/
                        s5_free_block(fs, (uint32_t)indirect_block);
                        s5_free_block(fs, (uint32_t)blocknum);
                        return err;
                    }

                    return blocknum;
                }
            }
        } else {
            panic("file is corrupted: not a dir or file but have idirect blocks?\n");
            return -EINVAL;
        }
    }
}


/*
 * Locks the mutex for the whole file system
 */
static void
lock_s5(s5fs_t *fs)
{
        kmutex_lock(&fs->s5f_mutex);
}

/*
 * Unlocks the mutex for the whole file system
 */
static void
unlock_s5(s5fs_t *fs)
{
        kmutex_unlock(&fs->s5f_mutex);
}


/*
 * Write len bytes to the given inode, starting at seek bytes from the
 * beginning of the inode. On success, return the number of bytes
 * actually written (which should be 'len', unless there's only enough
 * room for a partial write); on failure, return -errno.
 *
 * This function should allow writing to files or directories, treating
 * them identically.
 *
 * Writing to a sparse block of the file should cause that block to be
 * allocated.  Writing past the end of the file should increase the size
 * of the file. Blocks between the end and where you start writing will
 * be sparse.
 *
 * Do not call s5_seek_to_block() directly from this function.  You will
 * use the vnode's pframe functions, which will eventually result in a
 * call to s5_seek_to_block().
 *
 * You will need pframe_dirty(), pframe_get(), memcpy().
 */
int
s5_write_file(vnode_t *vnode, off_t seek, const char *bytes, size_t len)
{
    s5_inode_t *inode = VNODE_TO_S5INODE(vnode);

    if (seek < 0 || (unsigned)seek >= S5_MAX_FILE_SIZE) {
        dprintf("requesting a write to negative position or exceeding the maximum file size\n");
        return -EINVAL;
    }

    /*get the block number*/
    uint32_t block_start = S5_DATA_BLOCK(seek);
    off_t end = seek + len - 1;
    /*write to [start, end]*/
    /*int partial_write_flag = 0;*/
    if ((unsigned)end >= S5_MAX_FILE_SIZE) {
        end = S5_MAX_FILE_SIZE - 1;
        len = end - seek + 1;
    }
    uint32_t block_end = S5_DATA_BLOCK(end);
    
    /*get the offset inside block*/
    off_t offset_start = S5_DATA_OFFSET(seek);
    off_t offset_end = S5_DATA_OFFSET(end);
    
    /*write to only one block*/
    if (block_start == block_end) {
        pframe_t *block_pframe = NULL;
        int err = pframe_get(&vnode->vn_mmobj, block_start, &block_pframe);
        if (err < 0) {
            return err;
        }

        char *pf_off = (char *)block_pframe->pf_addr + offset_start;
        memcpy(pf_off, bytes, len);

        err = pframe_dirty(block_pframe);
        if (err < 0) {
            return err;
        }

        off_t file_length = end + 1;
        if (file_length > vnode->vn_len) {
            /*update len in vnode*/
            vnode->vn_len = file_length;
            /*update size in s5_inode*/
            inode->s5_size = (uint32_t)file_length;
        }
        /*dirty the inode*/
        s5_dirty_inode(VNODE_TO_S5FS(vnode), inode);

        return len;
    }

    /*write to the start block*/
    pframe_t *block_pframe = NULL;
    int err = pframe_get(&vnode->vn_mmobj, block_start, &block_pframe);
    if (err < 0) {
        return err;
    }

    char *pf_off = (char *)block_pframe->pf_addr + offset_start;
    memcpy(pf_off, bytes, (S5_BLOCK_SIZE - offset_start));
    err = pframe_dirty(block_pframe);
    if (err < 0) {
        return err;
    }
    bytes += S5_BLOCK_SIZE - offset_start;

    /*write to blocks till end between*/
    uint32_t i;
    for (i = block_start + 1 ; i <= block_end ; i++) {
        pframe_t *cur_pframe = NULL;
        err = pframe_get(&vnode->vn_mmobj, i, &cur_pframe);
        if (err < 0) {
            KASSERT(cur_pframe == NULL);
            return err;
        }

        if (i != block_end) {
            memcpy(cur_pframe->pf_addr, bytes, S5_BLOCK_SIZE);
            bytes += S5_BLOCK_SIZE;
        } else {
            memcpy(cur_pframe->pf_addr, bytes, offset_end + 1);
        }
        err = pframe_dirty(cur_pframe);
        if (err < 0) {
            return err;
        }
    }

    off_t file_length = end + 1;
    /*update len in vnode*/
    if (file_length > vnode->vn_len) {
        vnode->vn_len = file_length;
        /*update size in s5_inode*/
        inode->s5_size = (unsigned)file_length;
    }
    /*dirty the inode*/
    s5_dirty_inode(VNODE_TO_S5FS(vnode), inode);

    return len;
}

/*
 * Read up to len bytes from the given inode, starting at seek bytes
 * from the beginning of the inode. On success, return the number of
 * bytes actually read, or 0 if the end of the file has been reached; on
 * failure, return -errno.
 *
 * This function should allow reading from files or directories,
 * treating them identically.
 *
 * Reading from a sparse block of the file should act like reading
 * zeros; it should not cause the sparse blocks to be allocated.
 *
 * Similarly as in s5_write_file(), do not call s5_seek_to_block()
 * directly from this function.
 *
 * If the region to be read would extend past the end of the file, less
 * data will be read than was requested.
 *
 * You probably want to use pframe_get(), memcpy().
 */
int
s5_read_file(struct vnode *vnode, off_t seek, char *dest, size_t len)
{
    s5_inode_t *inode = VNODE_TO_S5INODE(vnode);

    if (seek < 0) {
        return -EINVAL;
    }

    if ((unsigned)seek >= inode->s5_size) {
        return 0;
    }

    /*get the block number*/
    uint32_t block_start = S5_DATA_BLOCK(seek);
    off_t end = seek + len - 1;
    /*read from [start, end]*/
    if ((unsigned)end >= inode->s5_size) {
        end = inode->s5_size - 1;
        len = end - seek + 1;
    }
    uint32_t block_end = S5_DATA_BLOCK(end);
    
    /*get the offset inside block*/
    off_t offset_start = S5_DATA_OFFSET(seek);
    off_t offset_end = S5_DATA_OFFSET(end);
    
    if (block_start == block_end) {
        pframe_t *block_pframe = NULL;
        int err = pframe_get(&vnode->vn_mmobj, block_start, &block_pframe);
        if (err < 0) {
            KASSERT(block_pframe == NULL);
            return err;
        }

        char *pf_off = (char *)block_pframe->pf_addr + offset_start;
        memcpy(dest, pf_off, len);

        return len;
    }

    /*read the start block*/
    pframe_t *block_pframe = NULL;
    int err = pframe_get(&vnode->vn_mmobj, block_start, &block_pframe);
    if (err < 0) {
        return err;
    }

    char *pf_off = (char *)block_pframe + offset_start;
    memcpy(dest, pf_off, (S5_BLOCK_SIZE - offset_start));
    dest += S5_BLOCK_SIZE - offset_start;

    /*read blocks till end*/
    uint32_t i;
    for (i = block_start + 1 ; i <= block_end ; i++) {
        pframe_t *cur_pframe = NULL;
        err = pframe_get(&vnode->vn_mmobj, i, &cur_pframe);
        if (err < 0) {
            return err;
        }

        if (i != block_end) {
            memcpy(dest, cur_pframe->pf_addr, S5_BLOCK_SIZE);
            dest += S5_BLOCK_SIZE;
        } else {
            memcpy(dest, cur_pframe->pf_addr, offset_end + 1);
        }
    }

    return len;
}

/*
 * Allocate a new disk-block off the block free list and return it. If
 * there are no free blocks, return -ENOSPC.
 *
 * This will not initialize the contents of an allocated block; these
 * contents are undefined.
 *
 * If the super block's s5s_nfree is 0, you need to refill 
 * s5s_free_blocks and reset s5s_nfree.  You need to read the contents 
 * of this page using the pframe system in order to obtain the next set of
 * free block numbers.
 *
 * Don't forget to dirty the appropriate blocks!
 *
 * You'll probably want to use lock_s5(), unlock_s5(), pframe_get(),
 * and s5_dirty_super()
 */
static int
s5_alloc_block(s5fs_t *fs)
{
    s5_super_t *s = fs->s5f_super;

    lock_s5(fs);

    if (s->s5s_nfree == 0 && s->s5s_free_blocks[S5_NBLKS_PER_FNODE - 1] == (uint32_t) -1) {
        unlock_s5(fs);
        return -ENOSPC;
    }

    int blocknum = 0;

    if (s->s5s_nfree == 0) {
        
        /*get the pframe where we will copy free block nums from*/
        pframe_t *next_free_blocks = NULL;
        blocknum = (int)s->s5s_free_blocks[S5_NBLKS_PER_FNODE - 1];
        
        pframe_get(&fs->s5f_bdev->bd_mmobj, (uint32_t)blocknum, &next_free_blocks);
        
        memcpy((void *)(s->s5s_free_blocks), next_free_blocks->pf_addr, 
                S5_NBLKS_PER_FNODE * sizeof(int));
        /*this will not initialize the contents of an allocated block*/
        /*the contents are undefined*/

        s->s5s_nfree = S5_NBLKS_PER_FNODE - 1;
    } else {
        blocknum = s->s5s_free_blocks[--(s->s5s_nfree)];
    }

    s5_dirty_super(fs);

    unlock_s5(fs);

    return blocknum;
        /*NOT_YET_IMPLEMENTED("S5FS: s5_alloc_block");*/
        /*return -1;*/
}


/*
 * Given a filesystem and a block number, frees the given block in the
 * filesystem.
 *
 * This function may potentially block.
 *
 * The caller is responsible for ensuring that the block being placed on
 * the free list is actually free and is not resident.
 */
static void
s5_free_block(s5fs_t *fs, int blockno)
{
        s5_super_t *s = fs->s5f_super;


        lock_s5(fs);

        KASSERT(S5_NBLKS_PER_FNODE > s->s5s_nfree);

        if ((S5_NBLKS_PER_FNODE - 1) == s->s5s_nfree) {
                /* get the pframe where we will store the free block nums */
                pframe_t *prev_free_blocks = NULL;
                KASSERT(fs->s5f_bdev);
                pframe_get(&fs->s5f_bdev->bd_mmobj, blockno, &prev_free_blocks);
                KASSERT(prev_free_blocks->pf_addr);

                /* copy from the superblock to the new block on disk */
                memcpy(prev_free_blocks->pf_addr, (void *)(s->s5s_free_blocks),
                       S5_NBLKS_PER_FNODE * sizeof(int));
                pframe_dirty(prev_free_blocks);

                /* reset s->s5s_nfree and s->s5s_free_blocks */
                s->s5s_nfree = 0;
                s->s5s_free_blocks[S5_NBLKS_PER_FNODE - 1] = blockno;
        } else {
                s->s5s_free_blocks[s->s5s_nfree++] = blockno;
        }

        s5_dirty_super(fs);

        unlock_s5(fs);
}

/*
 * Creates a new inode from the free list and initializes its fields.
 * Uses S5_INODE_BLOCK to get the page from which to create the inode
 *
 * This function may block.
 */
int
s5_alloc_inode(fs_t *fs, uint16_t type, devid_t devid)
{
        s5fs_t *s5fs = FS_TO_S5FS(fs);
        pframe_t *inodep;
        s5_inode_t *inode;
        int ret = -1;

        KASSERT((S5_TYPE_DATA == type)
                || (S5_TYPE_DIR == type)
                || (S5_TYPE_CHR == type)
                || (S5_TYPE_BLK == type));


        lock_s5(s5fs);

        if (s5fs->s5f_super->s5s_free_inode == (uint32_t) -1) {
                unlock_s5(s5fs);
                return -ENOSPC;
        }

        pframe_get(&s5fs->s5f_bdev->bd_mmobj,
                   S5_INODE_BLOCK(s5fs->s5f_super->s5s_free_inode),
                   &inodep);
        KASSERT(inodep);

        inode = (s5_inode_t *)(inodep->pf_addr)
                + S5_INODE_OFFSET(s5fs->s5f_super->s5s_free_inode);

        KASSERT(inode->s5_number == s5fs->s5f_super->s5s_free_inode);

        ret = inode->s5_number;

        /* reset s5s_free_inode; remove the inode from the inode free list: */
        s5fs->s5f_super->s5s_free_inode = inode->s5_next_free;
        pframe_pin(inodep);
        s5_dirty_super(s5fs);
        pframe_unpin(inodep);


        /* init the newly-allocated inode: */
        inode->s5_size = 0;
        inode->s5_type = type;
        inode->s5_linkcount = 0;
        dprintf("crazykeyword inode linkcount incremented, ino %d, linkcount now is: %d\n",inode->s5_number, inode->s5_linkcount);
        memset(inode->s5_direct_blocks, 0, S5_NDIRECT_BLOCKS * sizeof(int));
        if ((S5_TYPE_CHR == type) || (S5_TYPE_BLK == type))
                inode->s5_indirect_block = devid;
        else
                inode->s5_indirect_block = 0;

        s5_dirty_inode(s5fs, inode);

        unlock_s5(s5fs);

        return ret;
}


/*
 * Free an inode by freeing its disk blocks and putting it back on the
 * inode free list.
 *
 * You should also reset the inode to an unused state (eg. zero-ing its
 * list of blocks and setting its type to S5_FREE_TYPE).
 *
 * Don't forget to free the indirect block if it exists.
 *
 * You probably want to use s5_free_block().
 */
void
s5_free_inode(vnode_t *vnode)
{
        uint32_t i;
        s5_inode_t *inode = VNODE_TO_S5INODE(vnode);
        s5fs_t *fs = VNODE_TO_S5FS(vnode);

        KASSERT((S5_TYPE_DATA == inode->s5_type)
                || (S5_TYPE_DIR == inode->s5_type)
                || (S5_TYPE_CHR == inode->s5_type)
                || (S5_TYPE_BLK == inode->s5_type));

        /* free any direct blocks */
        for (i = 0; i < S5_NDIRECT_BLOCKS; ++i) {
                if (inode->s5_direct_blocks[i]) {
                        dprintf("freeing block %d\n", inode->s5_direct_blocks[i]);
                        s5_free_block(fs, inode->s5_direct_blocks[i]);

                        s5_dirty_inode(fs, inode);
                        inode->s5_direct_blocks[i] = 0;
                }
        }

        if (((S5_TYPE_DATA == inode->s5_type)
             || (S5_TYPE_DIR == inode->s5_type))
            && inode->s5_indirect_block) {
                pframe_t *ibp;
                uint32_t *b;

                pframe_get(S5FS_TO_VMOBJ(fs),
                           inode->s5_indirect_block,
                           &ibp);
                KASSERT(ibp
                        && "because never fails for block_device "
                        "vm_objects");
                pframe_pin(ibp);

                b = (uint32_t *)(ibp->pf_addr);
                for (i = 0; i < S5_NIDIRECT_BLOCKS; ++i) {
                        KASSERT(b[i] != inode->s5_indirect_block);
                        if (b[i])
                                s5_free_block(fs, b[i]);
                }

                pframe_unpin(ibp);

                s5_free_block(fs, inode->s5_indirect_block);
        }

        inode->s5_indirect_block = 0;
        inode->s5_type = S5_TYPE_FREE;
        s5_dirty_inode(fs, inode);

        lock_s5(fs);
        inode->s5_next_free = fs->s5f_super->s5s_free_inode;
        fs->s5f_super->s5s_free_inode = inode->s5_number;
        unlock_s5(fs);

        s5_dirty_inode(fs, inode);
        s5_dirty_super(fs);
}

/*
 * Locate the directory entry in the given inode with the given name,
 * and return its inode number. If there is no entry with the given
 * name, return -ENOENT.
 *
 * You'll probably want to use s5_read_file and name_match
 *
 * You can either read one dirent at a time or optimize and read more.
 * Either is fine.
 */
int
s5_find_dirent(vnode_t *vnode, const char *name, size_t namelen)
{
    s5_inode_t *inode = VNODE_TO_S5INODE(vnode);
    
    ((char *)name)[namelen] = 0;
    
    int err = 0;
    off_t offset = 0;
    off_t filesize = vnode->vn_len;
    s5_dirent_t dirent;

    /*scan thru every dirent*/
    while (offset < filesize) {
        err = s5_read_file(vnode, offset, (char *)(&dirent), sizeof(s5_dirent_t));
        if (err < 0) {
            dprintf("some error occurs, error number is %d\n", err);
            return err;
        }

        if (dirent.s5d_name[0] == '\0') {
            panic("Weird, there's some inconsistency between dirents and file size\n");
        }

        if name_match(dirent.s5d_name, name, namelen) {
            /*there exists inode number 0- the inode for the root*/
            /*KASSERT(dirent.s5d_inode);*/
            return dirent.s5d_inode;
        }

        offset += sizeof(s5_dirent_t);
    }

    return -ENOENT;
}

/*
 * Locate the directory entry in the given inode with the given name,
 * and delete it. If there is no entry with the given name, return
 * -ENOENT.
 *
 * In order to ensure that the directory entries are contiguous in the
 * directory file, you will need to move the last directory entry into
 * the remove dirent's place.
 *
 * When this function returns, the inode refcount on the removed file
 * should be decremented.
 *
 * It would be a nice extension to free blocks from the end of the
 * directory file which are no longer needed.
 *
 * Don't forget to dirty appropriate blocks!
 *
 * You probably want to use vget(), vput(), s5_read_file(),
 * s5_write_file(), and s5_dirty_inode().
 */
int
s5_remove_dirent(vnode_t *vnode, const char *name, size_t namelen)
{
    s5_inode_t *inode = VNODE_TO_S5INODE(vnode);
    
    ((char *)name)[namelen] = 0;
    
    int err = 0;
    int inodeno = 0;
    off_t offset = 0;
    off_t filesize = vnode->vn_len;
    s5_dirent_t dirent_target;

    /*search for the to-be-deleted dirent*/
    while (offset < filesize) {
        err = s5_read_file(vnode, offset, (char *)(&dirent_target), sizeof(s5_dirent_t));
        if (err < 0) {
            return err;
        }

        if (dirent_target.s5d_name[0] == '\0') {
            panic("Weird, there's some inconsistency between dirents and file size\n");
        }

        if name_match(dirent_target.s5d_name, name, namelen) {
            inodeno = dirent_target.s5d_inode;
            /*inodeno can be 0: inode for root*/
            /*KASSERT(inodeno);*/
            break;
        }

        KASSERT(err == sizeof(s5_dirent_t));
        offset += sizeof(s5_dirent_t);
    }

    /*get the last dirent*/
    s5_dirent_t dirent_last;
    err = s5_read_file(vnode, filesize - sizeof(s5_dirent_t), (char *)(&dirent_last), sizeof(s5_dirent_t));
    if (err < 0) {
        return err;
    }
    
    /*write the last dirent into the deleted dirent position*/
    err = s5_write_file(vnode, offset, (const char *)(&dirent_last), sizeof(s5_dirent_t));
    if (err < 0) {
        return err;
    }
    
    s5fs_t *fs = VNODE_TO_S5FS(vnode);
    vnode_t *vnode_deleted = vget(fs->s5f_fs, inodeno);
    if (vnode_deleted == NULL) {
        panic("don't know what to do here\n");
    }

    /*not sure about the maintainance of linkcount*/
    s5_inode_t *inode_deleted = VNODE_TO_S5INODE(vnode_deleted);
    inode_deleted->s5_linkcount--;
    dprintf("crazykeyword inode linkcount incremented, ino %d, linkcount now is: %d\n", vnode_deleted->vn_vno, inode_deleted->s5_linkcount);
    s5_dirty_inode(fs, inode_deleted);

    /*vnode_deleted did not outlive this function, so we only need to make sure that we have 1 vput corresponding to vget*/
    vput(vnode_deleted);

    /*modify the length*/
    vnode->vn_len -= sizeof(s5_dirent_t);
    inode->s5_size -= sizeof(s5_dirent_t);
    s5_dirty_inode(fs, inode);

    return 0;
}

/*
 * Create a new directory entry in directory 'parent' with the given name, which
 * refers to the same file as 'child'.
 *
 * When this function returns, the inode refcount on the file that was linked to
 * should be incremented.
 *
 * Remember to incrament the ref counts appropriately
 *
 * You probably want to use s5_find_dirent(), s5_write_file(), and s5_dirty_inode().
 */
int
s5_link(vnode_t *parent, vnode_t *child, const char *name, size_t namelen)
{
    s5_inode_t *inode_parent = VNODE_TO_S5INODE(parent);
    
    s5_inode_t *inode_child = VNODE_TO_S5INODE(child);
    
    ((char *)name)[namelen] = 0;
    
    /*examine if the dirent already exists*/
    int err = s5_find_dirent(parent, name, namelen);
    if (err > 0) {
        return -EEXIST;
    }

    if (err < 0 && err != -ENOENT) {
        return err;
    }
    
    /*construct the dirent*/
    s5_dirent_t dirent;
    strncpy(dirent.s5d_name, name, namelen);
    dirent.s5d_name[namelen] = '\0';
    dirent.s5d_inode = inode_child->s5_number;
    
    /*write it to the end of the file*/
    err = s5_write_file(parent, parent->vn_len, (const char *)(&dirent), sizeof(s5_dirent_t));
    if (err < 0) {
        return err;
    }
    
    /*special case '.'*/
    if (!name_match(".", name, namelen)) {
        /*increment the linkcount for inode*/
        inode_child->s5_linkcount++;
        s5_dirty_inode(VNODE_TO_S5FS(parent), inode_child);
    }

    return 0;
}

/*
 * Return the number of blocks that this inode has allocated on disk.
 * This should include the indirect block, but not include sparse
 * blocks.
 *
 * This is only used by s5fs_stat().
 *
 * You'll probably want to use pframe_get().
 */
int
s5_inode_blocks(vnode_t *vnode)
{
    s5_inode_t *inode = VNODE_TO_S5INODE(vnode);

    int result = 0;
    uint32_t i;
    for (i = 0 ; i < S5_NDIRECT_BLOCKS ; i++) {
        if (inode->s5_direct_blocks[i]) {
            result++;
        }
    }

    if (inode->s5_indirect_block == 0) {
        return result;
    } else {
        if (((S5_TYPE_DATA == inode->s5_type)
             || (S5_TYPE_DIR == inode->s5_type))
            && inode->s5_indirect_block) {
            pframe_t *ibp;
            uint32_t *b;

            s5fs_t *fs = VNODE_TO_S5FS(vnode);
            KASSERT(fs);

            /*get the page frame*/
            pframe_get(S5FS_TO_VMOBJ(fs),
                       (inode->s5_indirect_block),
                       &ibp);
            KASSERT(ibp
                    && "because never fails for block_device "
                    "vm_objects");

            b = (uint32_t *)(ibp->pf_addr);
            for (i = 0 ; i < S5_NIDIRECT_BLOCKS ; i++) {
                if (b[i]) {
                    result++;
                }
            }
        }
    }

    return result;
}
