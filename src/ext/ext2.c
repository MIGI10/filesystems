#include "ext2.h"

static Superblock getSuperblock(int fd);
static int isInternalDirectory(char* name);
void getBlocks(int fd, int block_id, int** blocks, int* total_blocks_fetched, int total_blocks, int block_size, int level);
static int isLastEntry(int fd, int *next_entry, int *blocks, int *current_block, int total_blocks, int block_size);
static char* getNestFormat(EXTNest nest);
static EXTNest* clone(EXTNest *nest, int is_last);
static Inode* traverseDirectory(int fd, int inode_id, char *file_name, EXTNest *nest, Superblock sb);
void printBlockData(int fd, int block_id, long *bytes_read, long file_size, int block_size, int level);
static void showFile(int fd, Inode* inode, Superblock sb);

int EXT2_check(int fd) {

    uint16_t buffer;

    lseek(fd, SUPERBLOCK_OFFSET + 56, SEEK_SET);
    read(fd, &buffer, 2);

    return buffer == 0xEF53;
}

void EXT2_showInfo(int fd) {

    Superblock sb;
    time_t time;

    sb = getSuperblock(fd);

    printf("\nINODE INFO\n");
    printf("  Size: %d\n", sb.s_inode_size);
    printf("  Num Inodes: %d\n", sb.s_inodes_count);
    printf("  First Inode: %d\n", sb.s_first_ino);
    printf("  Inodes Group: %d\n", sb.s_inodes_per_group);
    printf("  Free Inodes: %d\n", sb.s_free_inodes_count);

    printf("\nINFO BLOCK\n");
    printf("  Block size: %d\n", 1024 << sb.s_log_block_size);
    printf("  Reserved blocks: %d\n", sb.s_r_blocks_count);
    printf("  Free blocks: %d\n", sb.s_free_blocks_count);
    printf("  Total blocks: %d\n", sb.s_blocks_count);
    printf("  First block: %d\n", sb.s_first_data_block);
    printf("  Group blocks: %d\n", sb.s_blocks_per_group);
    printf("  Group frags: %d\n", sb.s_frags_per_group);

    printf("\nINFO VOLUME\n");
    printf("  Volume Name: %s\n", sb.s_volume_name);
    time = sb.s_lastcheck;
    printf("  Last Checked: %s", ctime(&time));
    time = sb.s_mtime;
    printf("  Last Mounted: %s", ctime(&time));
    time = sb.s_wtime;
    printf("  Last Written: %s\n", ctime(&time));
}

void EXT2_showTree(int fd) {

    Superblock sb;
    EXTNest *nest;

    sb = getSuperblock(fd);

    nest = malloc(sizeof(EXTNest));
    nest->count = 0;
    nest->is_last = NULL;

    // Start traversal at root directory (inode nº2)
    traverseDirectory(fd, 2, NULL, nest, sb);
}

int EXT2_showFile(int fd, char *file_name) {

    Superblock sb;
    Inode* file_inode;

    sb = getSuperblock(fd);

    // Start traversal at root directory (inode nº2)
    file_inode = traverseDirectory(fd, 2, file_name, NULL, sb);

    if (file_inode == NULL) {
        return -1;
    }

    showFile(fd, file_inode, sb);

    free(file_inode);

    return 0;
}

static Superblock getSuperblock(int fd) {

    Superblock sb;

    lseek(fd, SUPERBLOCK_OFFSET, SEEK_SET);
    read(fd, &sb, SUPERBLOCK_SIZE);
    return sb;
}

static int isInternalDirectory(char* name) {
    return strcmp(name, ".") == 0 || strcmp(name, "..") == 0 || strcmp(name, "lost+found") == 0;
}

void getBlocks(int fd, int block_id, int** blocks, int* total_blocks_fetched, int total_blocks, int block_size, int level) {

    int block_read;

    if (level > 0) {

        block_read = 0;

        while (block_read < block_size) {
            
            if (*total_blocks_fetched == total_blocks) return;

            lseek(fd, block_id * block_size + block_read, SEEK_SET);
            read(fd, &block_id, 4);
            getBlocks(fd, block_id, blocks, total_blocks_fetched, total_blocks, block_size, level - 1);
            block_read += 4;
        }
    }
    else if (*total_blocks_fetched < total_blocks) {

        *blocks = realloc(*blocks, sizeof(int) * (*total_blocks_fetched + 1));
        *blocks[*total_blocks_fetched] = block_id;

        (*total_blocks_fetched)++;
    }
}

static int isLastEntry(int fd, int *next_entry, int *blocks, int *current_block, int total_blocks, int block_size) {

    EXTDirectoryEntry dir_entry;
    char *name = NULL;
    int check;

    do { 

        if (*next_entry % block_size == 0 && *current_block == total_blocks) {
            free(name);
            return 1;
        }

        lseek(fd, *next_entry, SEEK_SET);
        read(fd, &dir_entry, DIR_ENTRY_SIZE);

        name = realloc(name, dir_entry.name_len + 1);
        read(fd, name, dir_entry.name_len);
        name[dir_entry.name_len] = '\0';

        *next_entry += dir_entry.rec_len;

        check = 1;

        if (*next_entry % block_size == 0) {
            
            if (*current_block < total_blocks) {
                *next_entry = block_size * blocks[*current_block];
                (*current_block)++;

                check = 0;
            }
        }

    } while (dir_entry.inode == 0 || isInternalDirectory(name));

    if (check) *next_entry -= dir_entry.rec_len;

    free(name);

    return 0;
}

static char* getNestFormat(EXTNest nest) {

    char* nesting;
    int i = 0, nest_i = 0;

    if (nest.count == 0) return NULL;

    nesting = malloc((nest.count * 4) + 1);

    for (i = 0, nest_i = 0; nest_i < nest.count; nest_i++, i++) {

        if (!nest.is_last[nest_i]) {
            nesting[i] = 0xE2;
            nesting[i + 1] = 0x94;
            nesting[i + 2] = 0x82;
            i += 3;
        }

        nesting[i] = '\t';
    }

    nesting[i] = '\0';

    return realloc(nesting, strlen(nesting) + 1);
}

static EXTNest* clone(EXTNest *nest, int is_last) {

    EXTNest *nest_copy = malloc(sizeof(EXTNest));

    nest_copy->count = nest->count + 1;
    nest_copy->is_last = malloc(nest_copy->count * sizeof(int));

    for (int i = 0; i < nest->count; i++) {
        nest_copy->is_last[i] = nest->is_last[i];
    }
    nest_copy->is_last[nest->count] = is_last;

    return nest_copy;
}

static Inode* traverseDirectory(int fd, int inode_id, char *file_name, EXTNest *nest, Superblock sb) {

    GroupDescriptor gd;
    Inode inode, *ret_inode;
    EXTDirectoryEntry dir_entry;
    int block_size, block_group_index, inode_table_index, block_group_desc, directory_entry, current_block, total_blocks, is_last = 0;
    int *blocks = NULL, total_blocks_fetched = 0;
    char *name = NULL, *nesting;

    block_size = 1024 << sb.s_log_block_size;

    // Get inode's block group index knowing inode id and nº of inodes per block group
    block_group_index = (inode_id - 1) / sb.s_inodes_per_group;
    // Get inode's block group descriptor location (block group descriptor table is always at the block following superblock and each descriptor is 32 bytes)
    block_group_desc = (block_size * (sb.s_first_data_block + 1)) + (block_group_index * 32);

    // Get inode's block group descriptor
    lseek(fd, block_group_desc, SEEK_SET);
    read(fd, &gd, GROUP_DESC_SIZE);

    // Point to inode table
    lseek(fd, block_size * gd.bg_inode_table, SEEK_SET);

    // Get inode's table index knowing inode id and nº of inodes per block group
    inode_table_index = (inode_id - 1) % sb.s_inodes_per_group;

    // Get corresponding inode table entry
    lseek(fd, sb.s_inode_size * inode_table_index, SEEK_CUR);
    read(fd, &inode, INODE_SIZE);

    // Prepare inode's blocks
    total_blocks = inode.i_blocks / (block_size / 512);

    for (int i = 0; i < 12; i++) {
        getBlocks(fd, inode.i_block[i], &blocks, &total_blocks_fetched, total_blocks, block_size, 0);
    }
    getBlocks(fd, inode.i_block[12], &blocks, &total_blocks_fetched, total_blocks, block_size, 1);
    getBlocks(fd, inode.i_block[13], &blocks, &total_blocks_fetched, total_blocks, block_size, 2);
    getBlocks(fd, inode.i_block[14], &blocks, &total_blocks_fetched, total_blocks, block_size, 3);

    current_block = 0;
    directory_entry = block_size * blocks[current_block++];

    do {

        lseek(fd, directory_entry, SEEK_SET);
        read(fd, &dir_entry, DIR_ENTRY_SIZE);

        name = (char*) malloc(dir_entry.name_len + 1);
        read(fd, name, dir_entry.name_len);
        name[dir_entry.name_len] = '\0';

        directory_entry += dir_entry.rec_len;

        if (directory_entry % block_size == 0) {
            
            if (current_block < total_blocks) {
                
                directory_entry = block_size * blocks[current_block];
                current_block++;
            }
            else {
                is_last = 1;
            }
        }

        if (dir_entry.inode == 0 || isInternalDirectory(name)) {
            free(name);
            continue;
        }

        if (nest != NULL) {
        
            nesting = getNestFormat(*nest);

            if (isLastEntry(fd, &directory_entry, blocks, &current_block, total_blocks, block_size)) {
                printf("%s└ %s\n", (nesting != NULL) ? nesting : "", name);
                is_last = 1;
            }
            else {
                printf("%s├ %s\n", (nesting != NULL) ? nesting : "", name);
                is_last = 0;
            }

            if (nesting != NULL) free(nesting);
        }

        if (dir_entry.file_type == 2) {

            if (nest != NULL) {
                traverseDirectory(fd, dir_entry.inode, NULL, clone(nest, is_last), sb);
            }
            else {
                ret_inode = traverseDirectory(fd, dir_entry.inode, file_name, NULL, sb);

                if (ret_inode != NULL) {
                    free(name);
                    free(blocks);
                    return ret_inode;
                }
            }
        }
        else if (file_name != NULL && strcmp(file_name, name) == 0) {

            lseek(fd, block_size * gd.bg_inode_table, SEEK_SET);

            inode_table_index = (dir_entry.inode - 1) % sb.s_inodes_per_group;

            lseek(fd, sb.s_inode_size * inode_table_index, SEEK_CUR);

            ret_inode = malloc(INODE_SIZE);
            read(fd, ret_inode, INODE_SIZE);

            free(name);
            free(blocks);
            return ret_inode;
        }

        free(name);

    } while (!is_last);

    if (nest != NULL) free(nest->is_last);
    free(nest);
    free(blocks);
    return NULL;
}

void printBlockData(int fd, int block_id, long *bytes_read, long file_size, int block_size, int level) {

    char* data = NULL;
    int bytes_to_read, block_read;

    if (level > 0) {

        block_read = 0;

        while (block_read < block_size) {
            
            if (file_size == *bytes_read) return;

            lseek(fd, block_id * block_size + block_read, SEEK_SET);
            read(fd, &block_id, 4);
            printBlockData(fd, block_id, bytes_read, file_size, block_size, level - 1);
            block_read += 4;
        }
    }
    else {

        lseek(fd, block_id * block_size, SEEK_SET);

        bytes_to_read = (file_size - *bytes_read < block_size) ? file_size - *bytes_read : block_size;

        if (!bytes_to_read) return;

        data = malloc(bytes_to_read + 1);
        read(fd, data, bytes_to_read);
        data[bytes_to_read] = '\0';
        printf("%s", data);

        *bytes_read += bytes_to_read;

        free(data);
    }
}

static void showFile(int fd, Inode* inode, Superblock sb) {

    int block_size;
    long bytes_read, file_size;

    block_size = 1024 << sb.s_log_block_size;

    file_size = inode->i_dir_acl;
    file_size <<= 32;

    file_size &= 0xFFFFFFFF00000000;
    file_size |= (inode->i_size & 0xFFFFFFFF);

    bytes_read = 0;

    for (int i = 0; i < 12; i++) {
        printBlockData(fd, inode->i_block[i], &bytes_read, file_size, block_size, 0);
    }

    printBlockData(fd, inode->i_block[12], &bytes_read, file_size, block_size, 1);

    printBlockData(fd, inode->i_block[13], &bytes_read, file_size, block_size, 2);

    printBlockData(fd, inode->i_block[14], &bytes_read, file_size, block_size, 3);
}