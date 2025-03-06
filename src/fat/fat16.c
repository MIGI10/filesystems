#include "fat16.h"

BootSector getBootSector(int fd);
void getNextCluster(int fd, int *current_cluster, BootSector bs);
static int isInternalFile(char* name, int attr);
static int isLastEntry(int fd, int *next_entry, int *cluster_id, int *neighbour_cluster, BootSector bs);
void cleanName(char (*dest)[12], uint8_t* name);
static char* getNestFormat(FATNest nest);
FATNest* clone(FATNest *nest, int is_last);
FATDirectoryEntry* traverseDirectory(int fd, int cluster_id, char* file_name, FATNest *nest, BootSector bs);
static void showFile(int fd, FATDirectoryEntry *file_entry, BootSector bs);

int FAT16_check(int fd) {

    BootSector bs;
    int root_dir_sectors, data_sectors, cluster_count; 

    bs = getBootSector(fd);

    root_dir_sectors = ((bs.BPB_RootEntCnt * DIRECTORY_ENTRY_SIZE) + (bs.BPB_BytsPerSec - 1)) / bs.BPB_BytsPerSec;

    data_sectors = bs.BPB_TotSec16 - (bs.BPB_RsvdSecCnt + (bs.BPB_NumFATs * bs.BPB_FATSz16) + root_dir_sectors);
    cluster_count = data_sectors / bs.BPB_SecPerClus;

    return (cluster_count >= 4085 && cluster_count < 65525);
}

void FAT16_showInfo(int fd) {

    BootSector bs;
    char system_name[9], label[12];

    bs = getBootSector(fd);

    memcpy(system_name, (char*) bs.BS_OEMName, 8);
    memcpy(label, (char*) bs.BS_VolLab, 11);
    system_name[8] = '\0';
    label[11] = '\0';

    printf("\nSystem name: %s\n", system_name);
    printf("Sector size: %d\n", bs.BPB_BytsPerSec);
    printf("Sectors per cluster: %d\n", bs.BPB_SecPerClus);
    printf("Reserved sectors: %d\n", bs.BPB_RsvdSecCnt);
    printf("# of FATs: %d\n", bs.BPB_NumFATs);
    printf("Max root entries: %d\n", bs.BPB_RootEntCnt);
    printf("Sector per FAT: %d\n", bs.BPB_FATSz16);
    printf("Label: %s\n\n", label);
}

void FAT16_showTree(int fd) {

    BootSector bs;
    FATNest *nest;

    bs = getBootSector(fd);

    nest = malloc(sizeof(FATNest));
    nest->count = 0;
    nest->is_last = NULL;

    traverseDirectory(fd, 0, NULL, nest, bs);
}

int FAT16_showFile(int fd, char *file_name) {

    BootSector bs;
    FATDirectoryEntry *file_entry;
    
    bs = getBootSector(fd);

    file_entry = traverseDirectory(fd, 0, file_name, NULL, bs);

    if (file_entry == NULL) {
        return -1;
    }

    showFile(fd, file_entry, bs);

    free(file_entry);

    return 0;
}

BootSector getBootSector(int fd) {

    BootSector bs;

    lseek(fd, 0, SEEK_SET);
    read(fd, &bs, BOOT_SECTOR_SIZE);
    return bs;
}

void getNextCluster(int fd, int *current_cluster, BootSector bs) {

    int fat_offset;
    
    fat_offset = bs.BPB_BytsPerSec * bs.BPB_RsvdSecCnt;

    lseek(fd, fat_offset + (*current_cluster * 2), SEEK_SET);
    read(fd, current_cluster, 2);

    // fff0-fff6: reserved, fff7: bad cluster, fff8-ffff: last cluster
    if ((*current_cluster & 0xFFF0) == 0xFFF0) *current_cluster = -1;
}

static int isInternalFile(char* name, int attr) {
    return (attr & 0x08) == 0x08 || strstr(name, ".") == name || strstr(name, "..") == name;
}

static int isLastEntry(int fd, int *next_entry, int *cluster_id, int *neighbour_cluster, BootSector bs) {

    FATDirectoryEntry dir_entry;
    int cluster_size, data_offset, check;

    cluster_size = bs.BPB_SecPerClus * bs.BPB_BytsPerSec;
    data_offset = bs.BPB_BytsPerSec * (bs.BPB_RsvdSecCnt + (bs.BPB_NumFATs * bs.BPB_FATSz16)) + (bs.BPB_RootEntCnt * DIRECTORY_ENTRY_SIZE);

    do {

        if (*cluster_id == -1) return 1;

        lseek(fd, *next_entry, SEEK_SET);
        read(fd, &dir_entry, DIRECTORY_ENTRY_SIZE);
        dir_entry.DIR_Name[10] = '\0';

        *next_entry += DIRECTORY_ENTRY_SIZE;

        check = 1;
        
        if (*next_entry == *neighbour_cluster) {

            getNextCluster(fd, cluster_id, bs);

            *next_entry = data_offset + ((*cluster_id - 2) * cluster_size);
            *neighbour_cluster = *next_entry + cluster_size;

            check = 0;
        }

    } while (dir_entry.DIR_Name[0] == 0xE5 || isInternalFile((char*) dir_entry.DIR_Name, dir_entry.DIR_Attr));

    if (check) *next_entry -= DIRECTORY_ENTRY_SIZE;

    return dir_entry.DIR_Name[0] == 0x00;
}

void cleanName(char (*dest)[12], uint8_t* name) {

    int i;
    for (i = 0; i < 8; i++) {

        if (name[i] == '~' && name[i + 1] == '1') goto end_name;

        if (name[i] == ' ') break;

        (*dest)[i] = name[i];
        if (name[i] >= 65 && name[i] <= 90) (*dest)[i] += 32;
    }

    if (name[8] == ' ') goto end_name;
    
    (*dest)[i++] = '.';

    for (int j = 8; j < 11; j++, i++) {

        if (name[j] == ' ') break;

        (*dest)[i] = name[j];
        if (name[j] >= 65 && name[j] <= 90) (*dest)[i] += 32;
    }

    end_name:
    (*dest)[i] = '\0';
}

static char* getNestFormat(FATNest nest) {

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

FATNest* clone(FATNest *nest, int is_last) {

    FATNest* nest_copy = malloc(sizeof(FATNest));

    nest_copy->count = nest->count + 1;
    nest_copy->is_last = malloc(nest_copy->count * sizeof(int));

    for (int i = 0; i < nest->count; i++) {
        nest_copy->is_last[i] = nest->is_last[i];
    }
    nest_copy->is_last[nest->count] = is_last;

    return nest_copy;
}

FATDirectoryEntry* traverseDirectory(int fd, int cluster_id, char* file_name, FATNest *nest, BootSector bs) {

    FATDirectoryEntry *dir_entry, *ret_dir_entry;
    int is_last, cluster_size, data_offset, next_entry, neighbour_cluster;
    char name[12], *nesting;

    // If 0 provided, read root directory
    if (cluster_id == 0) {

        next_entry = bs.BPB_BytsPerSec * (bs.BPB_RsvdSecCnt + (bs.BPB_NumFATs * bs.BPB_FATSz16));
        // Check not required for Root Directory region
        neighbour_cluster = -1;
    }
    else {

        cluster_size = bs.BPB_SecPerClus * bs.BPB_BytsPerSec;
        data_offset = bs.BPB_BytsPerSec * (bs.BPB_RsvdSecCnt + (bs.BPB_NumFATs * bs.BPB_FATSz16)) + (bs.BPB_RootEntCnt * DIRECTORY_ENTRY_SIZE);

        next_entry = data_offset + ((cluster_id - 2) * cluster_size);
        neighbour_cluster = next_entry + cluster_size;
    }

    dir_entry = malloc(sizeof(FATDirectoryEntry));

    do {

        lseek(fd, next_entry, SEEK_SET);
        read(fd, dir_entry, DIRECTORY_ENTRY_SIZE);

        next_entry += DIRECTORY_ENTRY_SIZE;

        if (next_entry == neighbour_cluster) {

            getNextCluster(fd, &cluster_id, bs);

            next_entry = data_offset + ((cluster_id - 2) * cluster_size);
            neighbour_cluster = next_entry + cluster_size;
        }
        
        if (dir_entry->DIR_Name[0] == 0xE5) continue;
        if (dir_entry->DIR_Name[0] == 0x00) break;
        if (dir_entry->DIR_Name[0] == 0x05) dir_entry->DIR_Name[0] = 0xE5;

        cleanName(&name, dir_entry->DIR_Name);
        
        if (isInternalFile(name, dir_entry->DIR_Attr)) continue;

        if (nest != NULL) {
        
            nesting = getNestFormat(*nest);

            if (isLastEntry(fd, &next_entry, &cluster_id, &neighbour_cluster, bs)) {
                printf("%s└ %s\n", (nesting != NULL) ? nesting : "", name);
                is_last = 1;
            }
            else {
                printf("%s├ %s\n", (nesting != NULL) ? nesting : "", name);
                is_last = 0;
            }

            if (nesting != NULL) free(nesting);
        }

        if ((dir_entry->DIR_Attr & 0x30) == 0x10) {

            if (nest == NULL) {

                ret_dir_entry = traverseDirectory(fd, dir_entry->DIR_FstClusLO, file_name, NULL, bs);

                if (ret_dir_entry != NULL) {
                    free(dir_entry);
                    return ret_dir_entry;
                }
            }
            else {
                traverseDirectory(fd, dir_entry->DIR_FstClusLO, NULL, clone(nest, is_last), bs);
            }
        }
        else if (file_name != NULL && strcmp(name, file_name) == 0) {
            return dir_entry;
        }

    } while (cluster_id != -1);

    if (nest != NULL) free(nest->is_last);
    free(nest);
    free(dir_entry);
    return NULL;
}

static void showFile(int fd, FATDirectoryEntry *file_entry, BootSector bs) {
    
    int cluster_size, data_offset, next_entry, neighbour_cluster, cluster_id, file_size, i, bytes_to_read;
    char* data = NULL;

    cluster_size = bs.BPB_SecPerClus * bs.BPB_BytsPerSec;
    data_offset = bs.BPB_BytsPerSec * (bs.BPB_RsvdSecCnt + (bs.BPB_NumFATs * bs.BPB_FATSz16)) + (bs.BPB_RootEntCnt * DIRECTORY_ENTRY_SIZE);

    cluster_id = file_entry->DIR_FstClusLO;
    file_size = file_entry->DIR_FileSize;

    next_entry = data_offset + ((cluster_id - 2) * cluster_size);
    neighbour_cluster = next_entry + cluster_size;

    lseek(fd, next_entry, SEEK_SET);

    i = 0;
    
    while (i < file_size) {
            
        if (next_entry == neighbour_cluster) {

            getNextCluster(fd, &cluster_id, bs);

            if (cluster_id == -1) break;

            next_entry = data_offset + ((cluster_id - 2) * cluster_size);
            neighbour_cluster = next_entry + cluster_size;

            lseek(fd, next_entry, SEEK_SET);
        }

        bytes_to_read = ((file_size - i) < cluster_size) ? file_size - i : cluster_size;

        data = realloc(data, bytes_to_read + 1);
        read(fd, data, bytes_to_read);
        data[bytes_to_read] = '\0';
        printf("%s", data);

        i += bytes_to_read;
    }

    free(data);
}