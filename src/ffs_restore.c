#include <assert.h>
#include <string.h>
#include "hal/hal_flash.h"
#include "os/os_mempool.h"
#include "ffs/ffs.h"
#include "ffs_priv.h"

static int
ffs_delete_if_trash(struct ffs_base *base)
{
    struct ffs_inode *inode;
    struct ffs_block *block;

    switch (base->fb_type) {
    case FFS_OBJECT_TYPE_INODE:
        inode = (void *)base;
        if (inode->fi_flags & FFS_INODE_F_DELETED) {
            ffs_inode_delete_from_ram(inode);
            return 1;
        } else if (inode->fi_flags & FFS_INODE_F_DUMMY) {
            assert(0); // XXX: TESTING
            ffs_inode_delete_from_ram(inode);
            return 1;
        } else {
            return 0;
        }

    case FFS_OBJECT_TYPE_BLOCK:
        block = (void *)base;
        if (block->fb_flags & FFS_BLOCK_F_DELETED ||
            block->fb_inode == NULL) {

            ffs_block_delete_from_ram(block);
            return 1;
        } else {
            return 0;
        }

    default:
        assert(0);
        return 0;
    }
}

void
ffs_restore_sweep(void)
{
    struct ffs_base_list *list;
    struct ffs_inode *inode;
    struct ffs_base *base;
    struct ffs_base *next;
    int rc;
    int i;

    for (i = 0; i < FFS_HASH_SIZE; i++) {
        list = ffs_hash + i;

        base = SLIST_FIRST(list);
        while (base != NULL) {
            next = SLIST_NEXT(base, fb_hash_next);

            rc = ffs_delete_if_trash(base);
            if (rc == 0) {
                if (base->fb_type == FFS_OBJECT_TYPE_INODE) {
                    inode = (void *)base;
                    if (!(inode->fi_flags & FFS_INODE_F_DIRECTORY)) {
                        inode->fi_data_len = ffs_inode_calc_data_length(inode);
                    }
                }
            }

            base = next;
        }
    }
}

static int
ffs_restore_dummy_inode(struct ffs_inode **out_inode, uint32_t id, int is_dir)
{
    struct ffs_inode *inode;

    inode = ffs_inode_alloc();
    if (inode == NULL) {
        return FFS_ENOMEM;
    }
    inode->fi_base.fb_id = id;
    inode->fi_refcnt = 1;
    inode->fi_base.fb_sector_id = FFS_SECTOR_ID_SCRATCH;
    inode->fi_flags = FFS_INODE_F_DUMMY;
    if (is_dir) {
        inode->fi_flags |= FFS_INODE_F_DIRECTORY;
    }

    ffs_hash_insert(&inode->fi_base);

    *out_inode = inode;

    return 0;
}

static int
ffs_restore_inode_gets_replaced(int *out_should_replace,
                                const struct ffs_inode *old_inode,
                                const struct ffs_disk_inode *disk_inode)
{
    assert(old_inode->fi_base.fb_id == disk_inode->fdi_id);

    if (old_inode->fi_flags & FFS_INODE_F_DUMMY) {
        *out_should_replace = 1;
        return 0;
    }

    if (old_inode->fi_base.fb_seq < disk_inode->fdi_seq) {
        *out_should_replace = 1;
        return 0;
    }

    if (old_inode->fi_base.fb_seq == disk_inode->fdi_seq) {
        assert(0); // XXX TESTING
        return FFS_ECORRUPT;
    }

    *out_should_replace = 0;
    return 0;
}

static int
ffs_restore_inode(const struct ffs_disk_inode *disk_inode, uint16_t sector_id,
                  uint32_t offset)
{
    struct ffs_inode *parent;
    struct ffs_inode *inode;
    int new_inode;
    int do_add;
    int rc;

    new_inode = 0;

    rc = ffs_hash_find_inode(&inode, disk_inode->fdi_id);
    switch (rc) {
    case 0:
        rc = ffs_restore_inode_gets_replaced(&do_add, inode, disk_inode);
        if (rc != 0) {
            goto err;
        }

        if (do_add) {
            if (inode->fi_parent != NULL) {
                ffs_inode_remove_child(inode);
            }
            ffs_inode_from_disk(inode, disk_inode, sector_id, offset);
        }
        break;

    case FFS_ENOENT:
        inode = ffs_inode_alloc();
        if (inode == NULL) {
            rc = FFS_ENOMEM;
            goto err;
        }
        new_inode = 1;
        do_add = 1;

        rc = ffs_inode_from_disk(inode, disk_inode, sector_id, offset);
        if (rc != 0) {
            goto err;
        }
        inode->fi_refcnt = 1;

        ffs_hash_insert(&inode->fi_base);
        break;

    default:
        assert(0); // XXX TESTING
        rc = FFS_ECORRUPT;
        goto err;
    }

    if (do_add) {
        if (disk_inode->fdi_parent_id != FFS_ID_NONE) {
            rc = ffs_hash_find_inode(&parent, disk_inode->fdi_parent_id);
            if (rc == FFS_ENOENT) {
                rc = ffs_restore_dummy_inode(&parent,
                                             disk_inode->fdi_parent_id, 1);
            }
            if (rc != 0) {
                goto err;
            }

            rc = ffs_inode_add_child(parent, inode);
            if (rc != 0) {
                goto err;
            }
        } 


        if (ffs_inode_is_root(disk_inode)) {
            ffs_root_dir = inode;
        }
    }

    if (inode->fi_base.fb_id >= ffs_next_id) {
        ffs_next_id = inode->fi_base.fb_id + 1;
    }

    return 0;

err:
    if (new_inode) {
        ffs_inode_free(inode);
    }
    return rc;
}

static int
ffs_restore_block_gets_replaced(int *out_should_replace,
                                const struct ffs_block *old_block,
                                const struct ffs_disk_block *disk_block)
{
    assert(old_block->fb_base.fb_id == disk_block->fdb_id);

    if (old_block->fb_base.fb_seq < disk_block->fdb_seq) {
        *out_should_replace = 1;
        return 0;
    }

    if (old_block->fb_base.fb_seq == disk_block->fdb_seq) {
        assert(0); // XXX TESTING
        return FFS_ECORRUPT;
    }

    *out_should_replace = 0;
    return 0;
}

static int
ffs_restore_block(const struct ffs_disk_block *disk_block, uint16_t sector_id,
                  uint32_t offset)
{
    struct ffs_block *block;
    int do_replace;
    int new_block;
    int rc;

    new_block = 0;

    rc = ffs_hash_find_block(&block, disk_block->fdb_id);
    switch (rc) {
    case 0:
        rc = ffs_restore_block_gets_replaced(&do_replace, block, disk_block);
        if (rc != 0) {
            goto err;
        }

        if (do_replace) {
            ffs_block_from_disk(block, disk_block, sector_id, offset);
        }
        break;

    case FFS_ENOENT:
        block = ffs_block_alloc();
        if (block == NULL) {
            rc = FFS_ENOMEM;
            goto err;
        }
        new_block = 1;

        ffs_block_from_disk(block, disk_block, sector_id, offset);
        ffs_hash_insert(&block->fb_base);

        rc = ffs_hash_find_inode(&block->fb_inode, disk_block->fdb_inode_id);
        if (rc == FFS_ENOENT) {
            rc = ffs_restore_dummy_inode(&block->fb_inode,
                                         disk_block->fdb_inode_id, 0);
        }
        if (rc != 0) {
            goto err;
        }
        ffs_inode_insert_block(block->fb_inode, block);
        break;

    default:
        assert(0); // XXX TESTING
        rc = FFS_ECORRUPT;
        goto err;
    }

    if (block->fb_base.fb_id >= ffs_next_id) {
        ffs_next_id = block->fb_base.fb_id + 1;
    }

    return 0;

err:
    if (new_block) {
        ffs_block_free(block);
    }
    return rc;
}

static int
ffs_restore_object(const struct ffs_disk_object *disk_object)
{
    int rc;

    switch (disk_object->fdo_type) {
    case FFS_OBJECT_TYPE_INODE:
        rc = ffs_restore_inode(&disk_object->fdo_disk_inode,
                               disk_object->fdo_sector_id,
                               disk_object->fdo_offset);
        break;

    case FFS_OBJECT_TYPE_BLOCK:
        rc = ffs_restore_block(&disk_object->fdo_disk_block,
                               disk_object->fdo_sector_id,
                               disk_object->fdo_offset);
        break;

    default:
        assert(0);
        rc = FFS_EINVAL;
        break;
    }

    return rc;
}

static int
ffs_restore_disk_object(struct ffs_disk_object *out_disk_object,
                        int sector_id, uint32_t offset)
{
    uint32_t magic;
    int rc;

    rc = ffs_flash_read(sector_id, offset, &magic, sizeof magic);
    if (rc != 0) {
        return rc;
    }

    switch (magic) {
    case FFS_INODE_MAGIC:
        out_disk_object->fdo_type = FFS_OBJECT_TYPE_INODE;
        rc = ffs_inode_read_disk(&out_disk_object->fdo_disk_inode, NULL,
                                 sector_id, offset);
        break;

    case FFS_BLOCK_MAGIC:
        out_disk_object->fdo_type = FFS_OBJECT_TYPE_BLOCK;
        rc = ffs_block_read_disk(&out_disk_object->fdo_disk_block, sector_id,
                                 offset);
        break;

    case 0xffffffff:
        rc = FFS_EEMPTY;
        break;

    default:
        rc = FFS_ECORRUPT;
        break;
    }

    if (rc != 0) {
        return rc;
    }

    out_disk_object->fdo_sector_id = sector_id;
    out_disk_object->fdo_offset = offset;

    return 0;
}

static int
ffs_restore_disk_object_size(const struct ffs_disk_object *disk_object)
{
    switch (disk_object->fdo_type) {
    case FFS_OBJECT_TYPE_INODE:
        return sizeof disk_object->fdo_disk_inode +
                      disk_object->fdo_disk_inode.fdi_filename_len;

    case FFS_OBJECT_TYPE_BLOCK:
        return sizeof disk_object->fdo_disk_block +
                      disk_object->fdo_disk_block.fdb_data_len;

    default:
        assert(0);
        return 1;
    }
}

static int
ffs_restore_sector(int sector_id)
{
    struct ffs_sector *sector;
    struct ffs_disk_sector disk_sector;
    struct ffs_disk_object disk_object;
    int rc;

    sector = ffs_sectors + sector_id;

    sector->fs_cur = sizeof disk_sector;
    while (1) {
        rc = ffs_restore_disk_object(&disk_object, sector_id, sector->fs_cur);
        switch (rc) {
        case 0:
            ffs_restore_object(&disk_object);
            sector->fs_cur += ffs_restore_disk_object_size(&disk_object);
            break;

        case FFS_EEMPTY:
        case FFS_ERANGE:
            return 0;

        default:
            return rc;
        }
    }
}

static int
ffs_restore_detect_one_sector(int *out_is_scratch, uint32_t sector_offset)
{
    struct ffs_disk_sector disk_sector;
    int rc;

    /* Parse sector header. */
    rc = flash_read(&disk_sector, sector_offset, sizeof disk_sector);
    if (rc != 0) {
        return FFS_EFLASH_ERROR;
    }

    if (!ffs_sector_magic_is_set(&disk_sector)) {
        return FFS_ECORRUPT;
    }

    if (disk_sector.fds_is_scratch != 0 &&
        disk_sector.fds_is_scratch != 0xff) {

        return FFS_ECORRUPT;
    }

    *out_is_scratch = disk_sector.fds_is_scratch == 0xff;

    return 0;
}

/**
 * Searches for a valid ffs file system among the specified sectors.  This
 * function succeeds if a file system is detected among any subset of the
 * supplied sectors.  If the sector set does not contain a valid file system,
 * a new one can be created via a call to ffs_format().
 *
 * @param sector_descs      The sector set to search.  This array must be
 *                          terminated with a 0-length sector.
 *
 * @return                  0 on success;
 *                          FFS_ECORRUPT if no valid file system was detected;
 *                          other nonzero on error.
 */
int
ffs_restore_full(const struct ffs_sector_desc *sector_descs)
{
    int use_sector;
    int is_scratch;
    int rc;
    int i;

    /* XXX: Ensure scratch sector is big enough. */
    /* XXX: Ensure block size is a factor of all sector sizes. */

    ffs_scratch_sector_id = FFS_SECTOR_ID_SCRATCH;
    ffs_num_sectors = 0;

    for (i = 0; sector_descs[i].fsd_length != 0; i++) {
        rc = ffs_restore_detect_one_sector(&is_scratch,
                                           sector_descs[i].fsd_offset);
        switch (rc) {
        case 0:
            use_sector = 1;
            break;

        case FFS_ECORRUPT:
            use_sector = 0;
            break;

        default:
            goto err;
        }

        if (use_sector) {
            if (is_scratch && ffs_scratch_sector_id != FFS_SECTOR_ID_SCRATCH) {
                /* Don't use more than one scratch sector. */
                use_sector = 0;
            }
        }

        if (use_sector) {
            ffs_sectors[ffs_num_sectors].fs_offset =
                sector_descs[i].fsd_offset;
            ffs_sectors[ffs_num_sectors].fs_length =
                sector_descs[i].fsd_length;
            ffs_sectors[ffs_num_sectors].fs_cur = 0;

            ffs_num_sectors++;

            if (is_scratch) {
                ffs_scratch_sector_id = ffs_num_sectors - 1;
            } else {
                ffs_restore_sector(ffs_num_sectors - 1);
            }
        }
    }

    rc = ffs_misc_validate_scratch();
    if (rc != 0) {
        return rc;
    }

    ffs_restore_sweep();

    rc = ffs_misc_validate_root();
    if (rc != 0) {
        return rc;
    }

    return 0;

err:
    ffs_scratch_sector_id = FFS_SECTOR_ID_SCRATCH;
    ffs_num_sectors = 0;
    return rc;
}

