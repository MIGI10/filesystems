#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>

#include "ext/ext2.h"
#include "fat/fat16.h"

int areEqual(char* str1, char* str2) {
    return strcmp(str1, str2) == 0;
}

int getOption(char** argv, int argc) {

    if (argc < 3) return -1;

    if (areEqual(argv[1], "--info")) {
        return 0;
    }
    else if (areEqual(argv[1], "--tree")) {
        return 1;
    }
    else if (areEqual(argv[1], "--cat")) {
        if (argc < 4) return -1;
        return 2;
    }
    else {
        return -1;
    }
}

void printInfoHeader(char* type) {
    printf("\n------ Filesystem Information ------\n");
    printf("\nFilesystem: %s\n", type);
}

void execInfo(int fd) {

    if (EXT2_check(fd)) {
        printInfoHeader("EXT2");
        EXT2_showInfo(fd);
    }
    else if (FAT16_check(fd)) {
        printInfoHeader("FAT16");
        FAT16_showInfo(fd);
    }
    else {
        printf("ERROR: Unknown filesystem. Only EXT2 and FAT16 are compatible.\n");
    }
}

void execTree(int fd) {

    if (EXT2_check(fd)) {
        EXT2_showTree(fd);
    }
    else if (FAT16_check(fd)) {
        FAT16_showTree(fd);
    }
    else {
        printf("ERROR: Unknown filesystem. Only EXT2 and FAT16 are compatible.\n");
    }
}

void execCat(int fd, char *file_name) {

    int return_val = 0;

    if (EXT2_check(fd)) {
        return_val = EXT2_showFile(fd, file_name);
    }
    else if (FAT16_check(fd)) {
        return_val = FAT16_showFile(fd, file_name);
    }
    else {
        printf("ERROR: Unknown filesystem. Only EXT2 and FAT16 are compatible.\n");
    }

    if (return_val == -1) {
        printf("ERROR: File not found.\n");
    }
}

int main(int argc, char* argv[]) {

    int option;
    int filesystem_fd = 0;

    option = getOption(argv, argc);

    if (option >= 0) {
        if ((filesystem_fd =  open(argv[2], O_RDONLY)) < 0) {
            printf("ERROR: Filesystem provided does not point to a file.\n");
            option = -2;
        }
    }

    switch (option) {
        case 0:
            execInfo(filesystem_fd);
            break;
        case 1:
            execTree(filesystem_fd);
            break;
        case 2:
            execCat(filesystem_fd, argv[3]);
            break;
        case -1:
            printf("Usage:\n\t./fsutils --info <filesystem>\n\t./fsutils --tree <filesystem>\n\t./fsutils --cat <filesystem> <filename>\n");
            break;
    }

    close(filesystem_fd);

    return 0;
}