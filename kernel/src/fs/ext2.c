#include <fs/ext2.h>
#include <mm/heap.h>
#include <utils/mem.h>
#include <utils/str.h>
#include <core/drivers/storage/nvme.h>
#include <utils/io.h>
#include <core/syscalls.h>

// Base timestamp (initialized at mount time)
static uint32_t base_time = 0;
// Current timestamp (updated on operations)
static uint32_t current_time = 0;
// Timer tick counter for maintaining time
static volatile uint64_t timer_ticks = 0;

// UNIX timestamp for January 1, 2024 00:00:00 UTC
#define DEFAULT_TIMESTAMP 1704067200

// RTC port addresses
#define RTC_INDEX_PORT   0x70
#define RTC_DATA_PORT    0x71

// RTC registers
#define RTC_SECONDS      0x00
#define RTC_MINUTES      0x02
#define RTC_HOURS        0x04
#define RTC_DAY          0x07
#define RTC_MONTH        0x08
#define RTC_YEAR         0x09

// Helper function to read RTC register
static uint8_t read_rtc_reg(uint8_t reg) {
    outb(RTC_INDEX_PORT, reg);
    return inb(RTC_DATA_PORT);
}

// Convert BCD to binary
static uint8_t bcd_to_binary(uint8_t bcd) {
    return ((bcd & 0xF0) >> 4) * 10 + (bcd & 0x0F);
}

// Days per month (non-leap year)
static const uint8_t days_in_month[] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

// Check if year is leap year
static bool is_leap_year(uint32_t year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

// Get timestamp from RTC
static uint32_t get_rtc_timestamp(void) {
    uint8_t second = bcd_to_binary(read_rtc_reg(RTC_SECONDS));
    uint8_t minute = bcd_to_binary(read_rtc_reg(RTC_MINUTES));
    uint8_t hour = bcd_to_binary(read_rtc_reg(RTC_HOURS));
    uint8_t day = bcd_to_binary(read_rtc_reg(RTC_DAY));
    uint8_t month = bcd_to_binary(read_rtc_reg(RTC_MONTH));
    uint16_t year = bcd_to_binary(read_rtc_reg(RTC_YEAR)) + 2000; // Assume 20xx

    // Calculate days since epoch (January 1, 1970)
    uint32_t days = 0;

    // Add days for each year from 1970 to current year
    for (uint32_t y = 1970; y < year; y++) {
        days += is_leap_year(y) ? 366 : 365;
    }

    // Add days for months in current year
    for (uint8_t m = 1; m < month; m++) {
        days += days_in_month[m - 1];
        if (m == 2 && is_leap_year(year)) {
            days++;
        }
    }

    // Add days in current month
    days += day - 1;

    // Convert to seconds and add time
    uint32_t timestamp = days * 86400;    // Seconds per day
    timestamp += hour * 3600;             // Seconds per hour
    timestamp += minute * 60;             // Seconds per minute
    timestamp += second;

    return timestamp;
}

// Initialize filesystem time
void ext2_time_init(void) {
    // Try to get time from RTC
    uint32_t rtc_time = get_rtc_timestamp();

    // Use RTC time if valid, otherwise use default
    base_time = (rtc_time > DEFAULT_TIMESTAMP) ? rtc_time : DEFAULT_TIMESTAMP;
    current_time = base_time;
    timer_ticks = 0;
}

// Update time (called by timer interrupt)
void ext2_timer_tick(void) {
    timer_ticks++;
    if (timer_ticks % 100 == 0) {  // Assuming 100Hz timer
        current_time = base_time + (timer_ticks / 100);
    }
}

// Get current time
uint32_t ext2_get_current_time(void) {
    return current_time;
}

// Update access time for inode
void ext2_update_atime(struct ext2_inode* inode) {
    if (inode) {
        inode->i_atime = current_time;
    }
}

// Update modification time for inode
void ext2_update_mtime(struct ext2_inode* inode) {
    if (inode) {
        inode->i_mtime = current_time;
    }
}

// Update change time for inode
void ext2_update_ctime(struct ext2_inode* inode) {
    if (inode) {
        inode->i_ctime = current_time;
    }
}

// Update all times for inode
void ext2_update_times(struct ext2_inode* inode) {
    if (inode) {
        uint32_t now = current_time;
        inode->i_atime = now;
        inode->i_mtime = now;
        inode->i_ctime = now;
    }
}

// Global EXT2 filesystem instance
static struct ext2_fs* ext2_instance = NULL;
static nvme_device_t* nvme_dev = NULL;

// Convert block number to LBA
static uint64_t block_to_lba(uint32_t block_num) {
    if (!ext2_instance) return 0;
    // Assuming standard 512-byte sectors, convert block size to sectors
    return block_num * (ext2_instance->block_size / 512);
}

// Helper function to read blocks from NVMe device
static bool read_blocks(uint32_t start_block, uint32_t block_count, void* buffer) {
    if (!nvme_dev || !buffer) return false;

    uint64_t lba = block_to_lba(start_block);
    uint32_t sectors = (block_count * ext2_instance->block_size) / 512;

    return nvme_read(nvme_dev, 1, lba, sectors, buffer) == NVME_SUCCESS;
}

// Helper function to write blocks to NVMe device
static bool write_blocks(uint32_t start_block, uint32_t block_count, const void* buffer) {
    if (!nvme_dev || !buffer) return false;

    uint64_t lba = block_to_lba(start_block);
    uint32_t sectors = (block_count * ext2_instance->block_size) / 512;

    return nvme_write(nvme_dev, 1, lba, sectors, buffer) == NVME_SUCCESS;
}

bool ext2_init(uint32_t device_id) {
    // Initialize time handling
    ext2_time_init();

    // Get NVMe device
    nvme_dev = nvme_get_device(device_id);  // Get specified NVMe device
    if (!nvme_dev) {
        return false;
    }

    // Allocate filesystem instance
    ext2_instance = malloc(sizeof(struct ext2_fs));
    if (!ext2_instance) {
        return false;
    }

    // Initialize filesystem instance
    memset(ext2_instance, 0, sizeof(struct ext2_fs));
    ext2_instance->device_id = device_id;

    // Read superblock (always at block 1)
    ext2_instance->superblock = malloc(sizeof(struct ext2_superblock));
    if (!ext2_instance->superblock) {
        free(ext2_instance);
        ext2_instance = NULL;
        return false;
    }

    // Read the superblock from disk
    if (!read_blocks(1, 1, ext2_instance->superblock)) {
        free(ext2_instance->superblock);
        free(ext2_instance);
        ext2_instance = NULL;
        return false;
    }

    // Verify magic number
    if (ext2_instance->superblock->s_magic != EXT2_SUPER_MAGIC) {
        free(ext2_instance->superblock);
        free(ext2_instance);
        ext2_instance = NULL;
        return false;
    }

    // Calculate filesystem parameters
    ext2_instance->block_size = 1024 << ext2_instance->superblock->s_log_block_size;
    ext2_instance->groups_count = (ext2_instance->superblock->s_blocks_count +
                                 ext2_instance->superblock->s_blocks_per_group - 1) /
                                 ext2_instance->superblock->s_blocks_per_group;
    ext2_instance->inodes_per_block = ext2_instance->block_size / sizeof(struct ext2_inode);

    // Allocate group descriptor array
    uint32_t group_desc_size = sizeof(struct ext2_group_desc) * ext2_instance->groups_count;
    uint32_t group_desc_blocks = (group_desc_size + ext2_instance->block_size - 1) / ext2_instance->block_size;

    ext2_instance->group_desc = malloc(group_desc_blocks * ext2_instance->block_size);
    if (!ext2_instance->group_desc) {
        free(ext2_instance->superblock);
        free(ext2_instance);
        ext2_instance = NULL;
        return false;
    }

    // Read group descriptors (start at block 2)
    if (!read_blocks(2, group_desc_blocks, ext2_instance->group_desc)) {
        free(ext2_instance->group_desc);
        free(ext2_instance->superblock);
        free(ext2_instance);
        ext2_instance = NULL;
        return false;
    }

    // Verify group descriptor sanity
    for (uint32_t i = 0; i < ext2_instance->groups_count; i++) {
        struct ext2_group_desc* desc = &ext2_instance->group_desc[i];

        // Check if block bitmap, inode bitmap, and inode table blocks are within bounds
        if (desc->bg_block_bitmap >= ext2_instance->superblock->s_blocks_count ||
            desc->bg_inode_bitmap >= ext2_instance->superblock->s_blocks_count ||
            desc->bg_inode_table >= ext2_instance->superblock->s_blocks_count) {

            free(ext2_instance->group_desc);
            free(ext2_instance->superblock);
            free(ext2_instance);
            ext2_instance = NULL;
            return false;
        }
    }

    // Update mount count and time
    ext2_instance->superblock->s_mnt_count++;
    ext2_instance->superblock->s_mtime = current_time;

    // Write back superblock
    if (!write_blocks(1, 1, ext2_instance->superblock)) {
        free(ext2_instance->group_desc);
        free(ext2_instance->superblock);
        free(ext2_instance);
        ext2_instance = NULL;
        return false;
    }

    // Read root inode to verify basic filesystem access
    struct ext2_inode* root_inode = ext2_get_inode(EXT2_ROOT_INO);
    if (!root_inode || !(root_inode->i_mode & EXT2_S_IFDIR)) {
        if (root_inode) free(root_inode);
        free(ext2_instance->group_desc);
        free(ext2_instance->superblock);
        free(ext2_instance);
        ext2_instance = NULL;
        return false;
    }
    free(root_inode);

    return true;
}

void ext2_cleanup(void) {
    if (ext2_instance) {
        if (ext2_instance->superblock) {
            free(ext2_instance->superblock);
        }
        if (ext2_instance->group_desc) {
            free(ext2_instance->group_desc);
        }
        free(ext2_instance);
        ext2_instance = NULL;
    }
}

struct ext2_inode* ext2_get_inode(uint32_t inode_num) {
    if (!ext2_instance || inode_num == 0) {
        return NULL;
    }

    // Calculate group and index
    uint32_t group = (inode_num - 1) / ext2_instance->superblock->s_inodes_per_group;
    uint32_t index = (inode_num - 1) % ext2_instance->superblock->s_inodes_per_group;

    // Get block containing inode
    uint32_t block = ext2_instance->group_desc[group].bg_inode_table +
                    (index * sizeof(struct ext2_inode)) / ext2_instance->block_size;
    uint32_t offset = (index * sizeof(struct ext2_inode)) % ext2_instance->block_size;

    // Allocate buffer for block
    void* buffer = malloc(ext2_instance->block_size);
    if (!buffer) {
        return NULL;
    }

    // Read block containing inode
    if (!read_blocks(block, 1, buffer)) {
        free(buffer);
        return NULL;
    }

    // Allocate and copy inode
    struct ext2_inode* inode = malloc(sizeof(struct ext2_inode));
    if (!inode) {
        free(buffer);
        return NULL;
    }

    memcpy(inode, buffer + offset, sizeof(struct ext2_inode));
    free(buffer);

    return inode;
}

bool ext2_read_block(uint32_t block_num, void* buffer) {
    if (!ext2_instance || !buffer) {
        return false;
    }

    return read_blocks(block_num, 1, buffer);
}

bool ext2_write_block(uint32_t block_num, const void* buffer) {
    if (!ext2_instance || !buffer) {
        return false;
    }

    return write_blocks(block_num, 1, buffer);
}

// Allocate a new inode from the bitmap
uint32_t ext2_allocate_inode(void) {
    if (!ext2_instance) return 0;

    // Search through each block group
    for (uint32_t group = 0; group < ext2_instance->groups_count; group++) {
        struct ext2_group_desc* desc = &ext2_instance->group_desc[group];
        if (desc->bg_free_inodes_count == 0) continue;

        // Read inode bitmap
        void* bitmap = malloc(ext2_instance->block_size);
        if (!bitmap) return 0;

        if (!ext2_read_block(desc->bg_inode_bitmap, bitmap)) {
            free(bitmap);
            continue;
        }

        // Find first free bit
        for (uint32_t bit = 0; bit < ext2_instance->superblock->s_inodes_per_group; bit++) {
            uint32_t byte_offset = bit / 8;
            uint8_t bit_offset = bit % 8;

            if (!(((uint8_t*)bitmap)[byte_offset] & (1 << bit_offset))) {
                // Found free inode
                ((uint8_t*)bitmap)[byte_offset] |= (1 << bit_offset);

                // Write bitmap back
                if (ext2_write_block(desc->bg_inode_bitmap, bitmap)) {
                    // Update superblock and group descriptor
                    desc->bg_free_inodes_count--;
                    ext2_instance->superblock->s_free_inodes_count--;

                    // Calculate inode number
                    uint32_t inode = group * ext2_instance->superblock->s_inodes_per_group + bit + 1;
                    free(bitmap);
                    return inode;
                }
            }
        }

        free(bitmap);
    }

    return 0;  // No free inodes found
}

// Write inode data to disk
bool ext2_write_inode(uint32_t inode_num, struct ext2_inode* inode) {
    if (!ext2_instance || !inode || inode_num == 0) return false;

    // Calculate group and index
    uint32_t group = (inode_num - 1) / ext2_instance->superblock->s_inodes_per_group;
    uint32_t index = (inode_num - 1) % ext2_instance->superblock->s_inodes_per_group;

    // Get block containing inode
    uint32_t block = ext2_instance->group_desc[group].bg_inode_table +
                    (index * sizeof(struct ext2_inode)) / ext2_instance->block_size;
    uint32_t offset = (index * sizeof(struct ext2_inode)) % ext2_instance->block_size;

    // Read block
    void* buffer = malloc(ext2_instance->block_size);
    if (!buffer) return false;

    if (!ext2_read_block(block, buffer)) {
        free(buffer);
        return false;
    }

    // Update inode data
    memcpy(buffer + offset, inode, sizeof(struct ext2_inode));

    // Write block back
    bool success = ext2_write_block(block, buffer);
    free(buffer);

    return success;
}

// Write data to a file
bool ext2_write_file(uint32_t inode_num, const void* buffer, uint32_t offset, uint32_t size) {
    struct ext2_inode* inode = ext2_get_inode(inode_num);
    if (!inode) return false;

    // Calculate block range to write
    uint32_t start_block = offset / ext2_instance->block_size;
    uint32_t end_block = (offset + size - 1) / ext2_instance->block_size;
    uint32_t start_offset = offset % ext2_instance->block_size;

    // Allocate buffer for block operations
    void* block_buffer = malloc(ext2_instance->block_size);
    if (!block_buffer) {
        free(inode);
        return false;
    }

    bool success = true;
    uint32_t bytes_written = 0;
    const uint8_t* write_ptr = (const uint8_t*)buffer;

    // Write blocks
    for (uint32_t block = start_block; success && block <= end_block; block++) {
        // Read existing block if partial write
        if (block == start_block ||
            (block == end_block && (offset + size) % ext2_instance->block_size != 0)) {
            success = ext2_read_inode_block(inode, block, block_buffer);
        }

        if (success) {
            uint32_t block_offset = (block == start_block) ? start_offset : 0;
            uint32_t bytes_to_write = ext2_instance->block_size - block_offset;

            if (bytes_to_write > size - bytes_written) {
                bytes_to_write = size - bytes_written;
            }

            memcpy((uint8_t*)block_buffer + block_offset, write_ptr, bytes_to_write);
            success = ext2_write_inode_block(inode, block, block_buffer);

            write_ptr += bytes_to_write;
            bytes_written += bytes_to_write;
        }
    }

    // Update inode size if necessary
    if (success && offset + size > inode->i_size) {
        inode->i_size = offset + size;
        success = ext2_write_inode(inode_num, inode);
    }

    free(block_buffer);
    free(inode);
    return success;
}

// Free an inode in the bitmap
bool free_inode_bitmap(uint32_t inode_num) {
    if (!ext2_instance || inode_num == 0) return false;

    // Calculate group and bit offset
    uint32_t group = (inode_num - 1) / ext2_instance->superblock->s_inodes_per_group;
    uint32_t index = (inode_num - 1) % ext2_instance->superblock->s_inodes_per_group;

    // Read bitmap block
    void* bitmap = malloc(ext2_instance->block_size);
    if (!bitmap) return false;

    if (!ext2_read_block(ext2_instance->group_desc[group].bg_inode_bitmap, bitmap)) {
        free(bitmap);
        return false;
    }

    // Clear bit in bitmap
    uint32_t byte_offset = index / 8;
    uint8_t bit_offset = index % 8;
    ((uint8_t*)bitmap)[byte_offset] &= ~(1 << bit_offset);

    // Write bitmap back
    bool success = ext2_write_block(ext2_instance->group_desc[group].bg_inode_bitmap, bitmap);
    if (success) {
        // Update counts
        ext2_instance->group_desc[group].bg_free_inodes_count++;
        ext2_instance->superblock->s_free_inodes_count++;
    }

    free(bitmap);
    return success;
}

// Create a directory entry
bool ext2_create_directory_entry(uint32_t dir_inode, const char* name,
                               uint32_t inode_num, uint8_t type) {
    struct ext2_inode* dir = ext2_get_inode(dir_inode);
    if (!dir) return false;

    void* block_buffer = malloc(ext2_instance->block_size);
    if (!block_buffer) {
        free(dir);
        return false;
    }

    bool success = false;
    uint32_t name_len = strlen(name);
    uint32_t entry_size = sizeof(struct ext2_dir_entry) + name_len;
    entry_size = (entry_size + 3) & ~3;  // Round up to 4-byte boundary

    // Search for space in directory blocks
    for (uint32_t i = 0; !success && i < dir->i_blocks; i++) {
        if (ext2_read_inode_block(dir, i, block_buffer)) {
            uint32_t offset = 0;
            struct ext2_dir_entry* entry;

            // Search through entries in block
            while (offset < ext2_instance->block_size) {
                entry = (struct ext2_dir_entry*)((uint8_t*)block_buffer + offset);

                // Check if we can fit our entry here
                if (entry->rec_len >= entry_size + ((entry->name_len + 3) & ~3)) {
                    // Split existing entry
                    uint32_t new_rec_len = entry->rec_len - entry_size;
                    entry->rec_len = (entry->name_len + 8 + 3) & ~3;

                    // Create new entry
                    entry = (struct ext2_dir_entry*)((uint8_t*)block_buffer + offset + entry->rec_len);
                    entry->inode = inode_num;
                    entry->rec_len = new_rec_len;
                    entry->name_len = name_len;
                    entry->file_type = type;
                    memcpy(entry->name, name, name_len);

                    success = ext2_write_inode_block(dir, i, block_buffer);
                    break;
                }

                offset += entry->rec_len;
                if (offset >= ext2_instance->block_size || entry->rec_len == 0) {
                    break;
                }
            }
        }
    }

    free(block_buffer);
    free(dir);
    return success;
}

// Allocate a new block
uint32_t ext2_allocate_block(void) {
    if (!ext2_instance) return 0;

    // Search through each block group
    for (uint32_t group = 0; group < ext2_instance->groups_count; group++) {
        struct ext2_group_desc* desc = &ext2_instance->group_desc[group];
        if (desc->bg_free_blocks_count == 0) continue;

        // Read block bitmap
        void* bitmap = malloc(ext2_instance->block_size);
        if (!bitmap) return 0;

        if (!ext2_read_block(desc->bg_block_bitmap, bitmap)) {
            free(bitmap);
            continue;
        }

        // Find first free block
        for (uint32_t bit = 0; bit < ext2_instance->superblock->s_blocks_per_group; bit++) {
            uint32_t byte_offset = bit / 8;
            uint8_t bit_offset = bit % 8;

            if (!(((uint8_t*)bitmap)[byte_offset] & (1 << bit_offset))) {
                // Found free block
                ((uint8_t*)bitmap)[byte_offset] |= (1 << bit_offset);

                // Write bitmap back
                if (ext2_write_block(desc->bg_block_bitmap, bitmap)) {
                    // Update superblock and group descriptor
                    desc->bg_free_blocks_count--;
                    ext2_instance->superblock->s_free_blocks_count--;

                    // Calculate block number
                    uint32_t block = group * ext2_instance->superblock->s_blocks_per_group + bit;
                    free(bitmap);
                    return block;
                }
            }
        }

        free(bitmap);
    }

    return 0;  // No free blocks found
}

bool ext2_read_inode_block(struct ext2_inode* inode, uint32_t block_num, void* buffer) {
    if (!ext2_instance || !inode || !buffer) {
        return false;
    }

    // Handle direct blocks
    if (block_num < 12) {
        return ext2_read_block(inode->i_block[block_num], buffer);
    }

    // Handle indirect blocks
    uint32_t* block_buffer = malloc(ext2_instance->block_size);
    if (!block_buffer) {
        return false;
    }

    bool success = false;
    block_num -= 12;

    // Single indirect
    if (block_num < ext2_instance->block_size / 4) {
        if (ext2_read_block(inode->i_block[12], block_buffer)) {
            success = ext2_read_block(block_buffer[block_num], buffer);
        }
    }
    // Double indirect
    else if (block_num < (ext2_instance->block_size / 4) * (ext2_instance->block_size / 4)) {
        block_num -= ext2_instance->block_size / 4;
        uint32_t indirect_block = block_num / (ext2_instance->block_size / 4);
        uint32_t indirect_offset = block_num % (ext2_instance->block_size / 4);

        if (ext2_read_block(inode->i_block[13], block_buffer)) {
            uint32_t* indirect_ptr = block_buffer;
            if (ext2_read_block(indirect_ptr[indirect_block], block_buffer)) {
                success = ext2_read_block(block_buffer[indirect_offset], buffer);
            }
        }
    }
    // Triple indirect
    else {
        block_num -= (ext2_instance->block_size / 4) * (ext2_instance->block_size / 4);
        uint32_t double_indirect = block_num / ((ext2_instance->block_size / 4) * (ext2_instance->block_size / 4));
        block_num %= (ext2_instance->block_size / 4) * (ext2_instance->block_size / 4);
        uint32_t indirect_block = block_num / (ext2_instance->block_size / 4);
        uint32_t indirect_offset = block_num % (ext2_instance->block_size / 4);

        if (ext2_read_block(inode->i_block[14], block_buffer)) {
            uint32_t* triple_ptr = block_buffer;
            if (ext2_read_block(triple_ptr[double_indirect], block_buffer)) {
                uint32_t* double_ptr = block_buffer;
                if (ext2_read_block(double_ptr[indirect_block], block_buffer)) {
                    success = ext2_read_block(block_buffer[indirect_offset], buffer);
                }
            }
        }
    }

    free(block_buffer);
    return success;
}

void ext2_free_block(uint32_t block_num) {
    if (!ext2_instance || block_num < ext2_instance->superblock->s_first_data_block ||
        block_num >= ext2_instance->superblock->s_blocks_count) {
        return;
    }

    // Calculate group and index
    uint32_t group = (block_num - ext2_instance->superblock->s_first_data_block) /
                    ext2_instance->superblock->s_blocks_per_group;
    uint32_t index = (block_num - ext2_instance->superblock->s_first_data_block) %
                    ext2_instance->superblock->s_blocks_per_group;

    // Read block bitmap
    void* bitmap = malloc(ext2_instance->block_size);
    if (!bitmap) return;

    if (!ext2_read_block(ext2_instance->group_desc[group].bg_block_bitmap, bitmap)) {
        free(bitmap);
        return;
    }

    // Clear bit in bitmap
    uint32_t byte_offset = index / 8;
    uint8_t bit_offset = index % 8;
    ((uint8_t*)bitmap)[byte_offset] &= ~(1 << bit_offset);

    // Write bitmap back
    if (ext2_write_block(ext2_instance->group_desc[group].bg_block_bitmap, bitmap)) {
        // Update superblock and group descriptor
        ext2_instance->group_desc[group].bg_free_blocks_count++;
        ext2_instance->superblock->s_free_blocks_count++;

        // Zero out the freed block (optional but safer)
        void* zero_block = malloc(ext2_instance->block_size);
        if (zero_block) {
            memset(zero_block, 0, ext2_instance->block_size);
            ext2_write_block(block_num, zero_block);
            free(zero_block);
        }
    }

    free(bitmap);
}

bool ext2_write_inode_block(struct ext2_inode* inode, uint32_t block_num, const void* buffer) {
    if (!ext2_instance || !inode || !buffer) {
        return false;
    }

    // Handle direct blocks
    if (block_num < 12) {
        return ext2_write_block(inode->i_block[block_num], buffer);
    }

    // Handle indirect blocks
    uint32_t* block_buffer = malloc(ext2_instance->block_size);
    if (!block_buffer) {
        return false;
    }

    bool success = false;
    block_num -= 12;

    // Single indirect
    if (block_num < ext2_instance->block_size / 4) {
        if (ext2_read_block(inode->i_block[12], block_buffer)) {
            success = ext2_write_block(block_buffer[block_num], buffer);
        }
    }
    // Double indirect
    else if (block_num < (ext2_instance->block_size / 4) * (ext2_instance->block_size / 4)) {
        block_num -= ext2_instance->block_size / 4;
        uint32_t indirect_block = block_num / (ext2_instance->block_size / 4);
        uint32_t indirect_offset = block_num % (ext2_instance->block_size / 4);

        if (ext2_read_block(inode->i_block[13], block_buffer)) {
            uint32_t* indirect_ptr = block_buffer;
            if (ext2_read_block(indirect_ptr[indirect_block], block_buffer)) {
                success = ext2_write_block(block_buffer[indirect_offset], buffer);
            }
        }
    }
    // Triple indirect
    else {
        block_num -= (ext2_instance->block_size / 4) * (ext2_instance->block_size / 4);
        uint32_t double_indirect = block_num / ((ext2_instance->block_size / 4) * (ext2_instance->block_size / 4));
        block_num %= (ext2_instance->block_size / 4) * (ext2_instance->block_size / 4);
        uint32_t indirect_block = block_num / (ext2_instance->block_size / 4);
        uint32_t indirect_offset = block_num % (ext2_instance->block_size / 4);

        if (ext2_read_block(inode->i_block[14], block_buffer)) {
            uint32_t* triple_ptr = block_buffer;
            if (ext2_read_block(triple_ptr[double_indirect], block_buffer)) {
                uint32_t* double_ptr = block_buffer;
                if (ext2_read_block(double_ptr[indirect_block], block_buffer)) {
                    success = ext2_write_block(block_buffer[indirect_offset], buffer);
                }
            }
        }
    }

    free(block_buffer);
    return success;
}

bool ext2_read_directory(uint32_t inode_num, void (*callback)(struct ext2_dir_entry*)) {
    struct ext2_inode* inode = ext2_get_inode(inode_num);
    if (!inode || !(inode->i_mode & EXT2_S_IFDIR)) {
        free(inode);
        return false;
    }

    void* block_buffer = malloc(ext2_instance->block_size);
    if (!block_buffer) {
        free(inode);
        return false;
    }

    bool success = true;
    uint32_t block_index = 0;

    while (success && block_index * ext2_instance->block_size < inode->i_size) {
        success = ext2_read_inode_block(inode, block_index, block_buffer);
        if (success) {
            uint32_t pos = 0;
            while (pos < ext2_instance->block_size) {
                struct ext2_dir_entry* entry = (struct ext2_dir_entry*)(block_buffer + pos);
                if (entry->inode != 0) {  // Valid entry
                    callback(entry);
                }
                pos += entry->rec_len;
                if (pos >= ext2_instance->block_size || entry->rec_len == 0) {
                    break;
                }
            }
        }
        block_index++;
    }

    free(block_buffer);
    free(inode);
    return success;
}

bool ext2_read_file(uint32_t inode_num, void* buffer, uint32_t offset, uint32_t size) {
    struct ext2_inode* inode = ext2_get_inode(inode_num);
    if (!inode) {
        return false;
    }

    // Ensure offset and size are valid
    if (offset >= inode->i_size || size == 0) {
        free(inode);
        return false;
    }

    // Adjust size if it would read past end of file
    if (offset + size > inode->i_size) {
        size = inode->i_size - offset;
    }

    // Calculate block range to read
    uint32_t start_block = offset / ext2_instance->block_size;
    uint32_t end_block = (offset + size - 1) / ext2_instance->block_size;
    uint32_t start_offset = offset % ext2_instance->block_size;

    // Allocate temporary buffer for block reads
    void* block_buffer = malloc(ext2_instance->block_size);
    if (!block_buffer) {
        free(inode);
        return false;
    }

    bool success = true;
    uint32_t bytes_read = 0;
    uint8_t* write_ptr = (uint8_t*)buffer;

    // Read blocks
    for (uint32_t block = start_block; success && block <= end_block; block++) {
        success = ext2_read_inode_block(inode, block, block_buffer);
        if (success) {
            uint32_t block_offset = (block == start_block) ? start_offset : 0;
            uint32_t bytes_to_copy = ext2_instance->block_size - block_offset;

            if (bytes_to_copy > size - bytes_read) {
                bytes_to_copy = size - bytes_read;
            }

            memcpy(write_ptr, (uint8_t*)block_buffer + block_offset, bytes_to_copy);
            write_ptr += bytes_to_copy;
            bytes_read += bytes_to_copy;
        }
    }

    free(block_buffer);
    free(inode);
    return success;
}

uint32_t ext2_find_file(uint32_t dir_inode, const char* name) {
    struct ext2_inode* inode = ext2_get_inode(dir_inode);
    if (!inode || !(inode->i_mode & EXT2_S_IFDIR)) {
        free(inode);
        return 0;
    }

    void* block_buffer = malloc(ext2_instance->block_size);
    if (!block_buffer) {
        free(inode);
        return 0;
    }

    uint32_t found_inode = 0;
    uint32_t block_index = 0;

    while (block_index * ext2_instance->block_size < inode->i_size) {
        if (ext2_read_inode_block(inode, block_index, block_buffer)) {
            uint32_t pos = 0;
            while (pos < ext2_instance->block_size) {
                struct ext2_dir_entry* entry = (struct ext2_dir_entry*)(block_buffer + pos);
                if (entry->inode != 0) {  // Valid entry
                    if (strlen(name) == entry->name_len &&
                        memcmp(name, entry->name, entry->name_len) == 0) {
                        found_inode = entry->inode;
                        goto cleanup;
                    }
                }
                pos += entry->rec_len;
                if (pos >= ext2_instance->block_size || entry->rec_len == 0) {
                    break;
                }
            }
        }
        block_index++;
    }

cleanup:
    free(block_buffer);
    free(inode);
    return found_inode;
}

uint32_t ext2_create_file(uint32_t parent_inode, const char* name, uint16_t mode) {
    // First check if file already exists
    if (ext2_find_file(parent_inode, name)) {
        return 0;
    }

    // Allocate new inode
    uint32_t inode_num = ext2_allocate_inode();
    if (!inode_num) {
        return 0;
    }

    // Initialize inode
    struct ext2_inode inode = {0};
    inode.i_mode = mode;
    inode.i_uid = 0;  // Root user
    inode.i_size = 0;
    inode.i_atime = current_time;
    inode.i_ctime = current_time;
    inode.i_mtime = current_time;
    inode.i_dtime = 0;
    inode.i_gid = 0;  // Root group
    inode.i_links_count = 1;
    inode.i_blocks = 0;
    inode.i_flags = 0;

    // Write inode
    if (!ext2_write_inode(inode_num, &inode)) {
        free_inode_bitmap(inode_num);
        return 0;
    }

    // Create directory entry
    uint8_t type = (mode & EXT2_S_IFDIR) ? EXT2_FT_DIR : EXT2_FT_REG_FILE;
    if (!ext2_create_directory_entry(parent_inode, name, inode_num, type)) {
        free_inode_bitmap(inode_num);
        return 0;
    }

    // If directory, create . and .. entries
    if (mode & EXT2_S_IFDIR) {
        // Allocate first block for directory
        uint32_t block = ext2_allocate_block();
        if (!block) {
            free_inode_bitmap(inode_num);
            return 0;
        }

        // Initialize block with . and .. entries
        void* block_buffer = malloc(ext2_instance->block_size);
        if (!block_buffer) {
            ext2_free_block(block);
            free_inode_bitmap(inode_num);
            return 0;
        }

        memset(block_buffer, 0, ext2_instance->block_size);
        struct ext2_dir_entry* entry = (struct ext2_dir_entry*)block_buffer;

        // . entry
        entry->inode = inode_num;
        entry->rec_len = 12;
        entry->name_len = 1;
        entry->file_type = EXT2_FT_DIR;
        entry->name[0] = '.';

        // .. entry
        entry = (struct ext2_dir_entry*)((uint8_t*)block_buffer + 12);
        entry->inode = parent_inode;
        entry->rec_len = ext2_instance->block_size - 12;  // Rest of block
        entry->name_len = 2;
        entry->file_type = EXT2_FT_DIR;
        entry->name[0] = '.';
        entry->name[1] = '.';

        // Write block
        if (!ext2_write_block(block, block_buffer)) {
            free(block_buffer);
            ext2_free_block(block);
            free_inode_bitmap(inode_num);
            return 0;
        }

        free(block_buffer);

        // Update inode
        struct ext2_inode* dir_inode = ext2_get_inode(inode_num);
        if (dir_inode) {
            dir_inode->i_block[0] = block;
            dir_inode->i_size = ext2_instance->block_size;
            dir_inode->i_blocks = ext2_instance->block_size / 512;
            ext2_write_inode(inode_num, dir_inode);
            free(dir_inode);
        }
    }

    return inode_num;
}

bool ext2_delete_file(uint32_t parent_inode, const char* name) {
    uint32_t inode_num = ext2_find_file(parent_inode, name);
    if (!inode_num) {
        return false;
    }

    struct ext2_inode* inode = ext2_get_inode(inode_num);
    if (!inode) {
        return false;
    }

    // Don't delete non-empty directories
    if ((inode->i_mode & EXT2_S_IFDIR) && inode->i_size > ext2_instance->block_size) {
        free(inode);
        return false;
    }

    // Free all direct blocks
    for (int i = 0; i < 12 && inode->i_block[i]; i++) {
        ext2_free_block(inode->i_block[i]);
    }

    // Free single indirect blocks
    if (inode->i_block[12]) {
        void* indirect = malloc(ext2_instance->block_size);
        if (indirect) {
            if (ext2_read_block(inode->i_block[12], indirect)) {
                uint32_t* blocks = (uint32_t*)indirect;
                for (uint32_t i = 0; i < ext2_instance->block_size / 4 && blocks[i]; i++) {
                    ext2_free_block(blocks[i]);
                }
            }
            free(indirect);
        }
        ext2_free_block(inode->i_block[12]);
    }

    // Free double indirect blocks
    if (inode->i_block[13]) {
        void* double_indirect = malloc(ext2_instance->block_size);
        if (double_indirect) {
            if (ext2_read_block(inode->i_block[13], double_indirect)) {
                uint32_t* indirect_blocks = (uint32_t*)double_indirect;
                void* indirect = malloc(ext2_instance->block_size);
                if (indirect) {
                    for (uint32_t i = 0; i < ext2_instance->block_size / 4 && indirect_blocks[i]; i++) {
                        if (ext2_read_block(indirect_blocks[i], indirect)) {
                            uint32_t* blocks = (uint32_t*)indirect;
                            for (uint32_t j = 0; j < ext2_instance->block_size / 4 && blocks[j]; j++) {
                                ext2_free_block(blocks[j]);
                            }
                        }
                        ext2_free_block(indirect_blocks[i]);
                    }
                    free(indirect);
                }
            }
            free(double_indirect);
        }
        ext2_free_block(inode->i_block[13]);
    }

    // Free triple indirect blocks
    if (inode->i_block[14]) {
        void* triple_indirect = malloc(ext2_instance->block_size);
        if (triple_indirect) {
            if (ext2_read_block(inode->i_block[14], triple_indirect)) {
                uint32_t* double_indirect_blocks = (uint32_t*)triple_indirect;
                void* double_indirect = malloc(ext2_instance->block_size);
                if (double_indirect) {
                    for (uint32_t i = 0; i < ext2_instance->block_size / 4 && double_indirect_blocks[i]; i++) {
                        if (ext2_read_block(double_indirect_blocks[i], double_indirect)) {
                            uint32_t* indirect_blocks = (uint32_t*)double_indirect;
                            void* indirect = malloc(ext2_instance->block_size);
                            if (indirect) {
                                for (uint32_t j = 0; j < ext2_instance->block_size / 4 && indirect_blocks[j]; j++) {
                                    if (ext2_read_block(indirect_blocks[j], indirect)) {
                                        uint32_t* blocks = (uint32_t*)indirect;
                                        for (uint32_t k = 0; k < ext2_instance->block_size / 4 && blocks[k]; k++) {
                                            ext2_free_block(blocks[k]);
                                        }
                                    }
                                    ext2_free_block(indirect_blocks[j]);
                                }
                                free(indirect);
                            }
                        }
                        ext2_free_block(double_indirect_blocks[i]);
                    }
                    free(double_indirect);
                }
            }
            free(triple_indirect);
        }
        ext2_free_block(inode->i_block[14]);
    }

    // Free the inode itself
    free_inode_bitmap(inode_num);
    free(inode);

    // Remove directory entry
    struct ext2_inode* parent = ext2_get_inode(parent_inode);
    if (!parent) {
        return false;
    }

    bool found = false;
    void* block_buffer = malloc(ext2_instance->block_size);
    if (!block_buffer) {
        free(parent);
        return false;
    }

    for (uint32_t i = 0; i < parent->i_size / ext2_instance->block_size; i++) {
        if (ext2_read_inode_block(parent, i, block_buffer)) {
            struct ext2_dir_entry* prev = NULL;
            uint32_t offset = 0;

            while (offset < ext2_instance->block_size) {
                struct ext2_dir_entry* entry = (struct ext2_dir_entry*)((uint8_t*)block_buffer + offset);

                if (entry->inode == inode_num) {
                    // Merge with previous entry if possible
                    if (prev) {
                        prev->rec_len += entry->rec_len;
                    } else {
                        // Mark as deleted
                        entry->inode = 0;
                    }

                    if (ext2_write_inode_block(parent, i, block_buffer)) {
                        found = true;
                    }
                    break;
                }

                prev = entry;
                offset += entry->rec_len;
                if (offset >= ext2_instance->block_size || entry->rec_len == 0) {
                    break;
                }
            }

            if (found) break;
        }
    }

    free(block_buffer);
    free(parent);

    return found;
}