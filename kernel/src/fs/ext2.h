#ifndef EXT2_H
#define EXT2_H

#include <stdint.h>
#include <stdbool.h>

// EXT2 Superblock Constants
#define EXT2_SUPER_MAGIC    0xEF53
#define EXT2_ROOT_INO       2

// EXT2 File Types
#define EXT2_FT_UNKNOWN     0
#define EXT2_FT_REG_FILE    1
#define EXT2_FT_DIR         2
#define EXT2_FT_CHRDEV      3
#define EXT2_FT_BLKDEV      4
#define EXT2_FT_FIFO        5
#define EXT2_FT_SOCK        6
#define EXT2_FT_SYMLINK     7

// EXT2 Inode Flags
#define EXT2_S_IFSOCK       0xC000
#define EXT2_S_IFLNK        0xA000
#define EXT2_S_IFREG        0x8000
#define EXT2_S_IFBLK        0x6000
#define EXT2_S_IFDIR        0x4000
#define EXT2_S_IFCHR        0x2000
#define EXT2_S_IFIFO        0x1000

// EXT2 Permission Flags
#define EXT2_S_ISUID        0x0800
#define EXT2_S_ISGID        0x0400
#define EXT2_S_ISVTX        0x0200
#define EXT2_S_IRUSR        0x0100
#define EXT2_S_IWUSR        0x0080
#define EXT2_S_IXUSR        0x0040
#define EXT2_S_IRGRP        0x0020
#define EXT2_S_IWGRP        0x0010
#define EXT2_S_IXGRP        0x0008
#define EXT2_S_IROTH        0x0004
#define EXT2_S_IWOTH        0x0002
#define EXT2_S_IXOTH        0x0001

// EXT2 Superblock Structure
struct ext2_superblock {
    uint32_t s_inodes_count;
    uint32_t s_blocks_count;
    uint32_t s_r_blocks_count;
    uint32_t s_free_blocks_count;
    uint32_t s_free_inodes_count;
    uint32_t s_first_data_block;
    uint32_t s_log_block_size;
    uint32_t s_log_frag_size;
    uint32_t s_blocks_per_group;
    uint32_t s_frags_per_group;
    uint32_t s_inodes_per_group;
    uint32_t s_mtime;
    uint32_t s_wtime;
    uint16_t s_mnt_count;
    uint16_t s_max_mnt_count;
    uint16_t s_magic;
    uint16_t s_state;
    uint16_t s_errors;
    uint16_t s_minor_rev_level;
    uint32_t s_lastcheck;
    uint32_t s_checkinterval;
    uint32_t s_creator_os;
    uint32_t s_rev_level;
    uint16_t s_def_resuid;
    uint16_t s_def_resgid;
} __attribute__((packed));

// EXT2 Block Group Descriptor
struct ext2_group_desc {
    uint32_t bg_block_bitmap;
    uint32_t bg_inode_bitmap;
    uint32_t bg_inode_table;
    uint16_t bg_free_blocks_count;
    uint16_t bg_free_inodes_count;
    uint16_t bg_used_dirs_count;
    uint16_t bg_pad;
    uint32_t bg_reserved[3];
} __attribute__((packed));

// EXT2 Inode Structure
struct ext2_inode {
    uint16_t i_mode;
    uint16_t i_uid;
    uint32_t i_size;
    uint32_t i_atime;
    uint32_t i_ctime;
    uint32_t i_mtime;
    uint32_t i_dtime;
    uint16_t i_gid;
    uint16_t i_links_count;
    uint32_t i_blocks;
    uint32_t i_flags;
    uint32_t i_osd1;
    uint32_t i_block[15];
    uint32_t i_generation;
    uint32_t i_file_acl;
    uint32_t i_dir_acl;
    uint32_t i_faddr;
    uint8_t  i_osd2[12];
} __attribute__((packed));

// EXT2 Directory Entry Structure
struct ext2_dir_entry {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  file_type;
    char     name[];
} __attribute__((packed));

// EXT2 filesystem instance structure
struct ext2_fs {
    struct ext2_superblock* superblock;
    struct ext2_group_desc* group_desc;
    uint32_t block_size;
    uint32_t groups_count;
    uint32_t inodes_per_block;
    uint32_t device_id;  // Reference to storage device
};

// Function declarations
bool ext2_init(uint32_t device_id);
void ext2_cleanup(void);

// Inode operations
struct ext2_inode* ext2_get_inode(uint32_t inode_num);
bool ext2_read_inode_block(struct ext2_inode* inode, uint32_t block_num, void* buffer);
bool ext2_write_inode_block(struct ext2_inode* inode, uint32_t block_num, const void* buffer);

// Directory operations
bool ext2_read_directory(uint32_t inode_num, void (*callback)(struct ext2_dir_entry*));
uint32_t ext2_find_file(uint32_t dir_inode, const char* name);
bool ext2_create_directory_entry(uint32_t dir_inode, const char* name,
                               uint32_t inode_num, uint8_t type);

// File operations
bool ext2_read_file(uint32_t inode_num, void* buffer, uint32_t offset, uint32_t size);
bool ext2_write_file(uint32_t inode_num, const void* buffer, uint32_t offset, uint32_t size);
uint32_t ext2_create_file(uint32_t parent_inode, const char* name, uint16_t mode);
bool ext2_delete_file(uint32_t parent_inode, const char* name);

// Block operations
bool ext2_read_block(uint32_t block_num, void* buffer);
bool ext2_write_block(uint32_t block_num, const void* buffer);
uint32_t ext2_allocate_block(void);
void ext2_free_block(uint32_t block_num);

// Bitmap operations
bool ext2_set_block_bitmap(uint32_t block_num, bool used);
bool ext2_set_inode_bitmap(uint32_t inode_num, bool used);
bool ext2_test_block_bitmap(uint32_t block_num);
bool ext2_test_inode_bitmap(uint32_t inode_num);

// Inode management
uint32_t ext2_allocate_inode(void);
bool ext2_write_inode(uint32_t inode_num, struct ext2_inode* inode);
bool free_inode_bitmap(uint32_t inode_num);

#endif // EXT2_H