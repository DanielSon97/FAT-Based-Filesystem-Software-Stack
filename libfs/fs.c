#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "disk.h"
#include "fs.h"

#include <stdbool.h>
#define FS_DEBUG false

/*define constants*/
#define BLOCK_BYTES 4096
#define NUM_ROOTDIR_ENTRIES 128
#define SIGNATURE_CHECK "ECS150FS"
#define FILENAME_MAX_SIZE 16
#define MAX_OPEN_FILE_DESCRIPTORS 32

/*define data structures for meta-information blocks*/
//packed data structure for superblock
struct __attribute__((__packed__)) superBlock {
    int8_t signature[8];                        //Signature (must be equal to “ECS150FS”)
    uint16_t numBlocks;                         //Total amount of blocks of virtual disk
    uint16_t rootIndex;                         //Root directory block index
    uint16_t dataIndex;                         //Data block start index
    uint16_t numDBlocks;                        //Amount of data blocks
    uint8_t numFBlocks;                         //Number of blocks for FAT
    int8_t unused[4079];                        //Unused/Padding
};

//packed data structure for file information
struct __attribute__((__packed__)) fileInfo {
    int8_t filename[FILENAME_MAX_SIZE];         //Filename (including NULL character)
    uint32_t size;                              //Size of the file (in bytes)
    uint16_t firstIndex;                        //Index of first data block
    int8_t padding[10];                         //Unused/Padding
};

//packed data structure for root directory
struct __attribute__((__packed__)) rootDirectory {
    struct fileInfo files[NUM_ROOTDIR_ENTRIES];         //entries of file informations
};

//packed structure for basic file descriptor
struct __attribute__((__packed__)) fileDescriptor {
    int8_t filename[FILENAME_MAX_SIZE];
    int offset;
};

/*intialize variables for meta-information blocks*/
struct superBlock *sb;
uint16_t *fat;
struct rootDirectory *root;
struct fileDescriptor openedFiles[MAX_OPEN_FILE_DESCRIPTORS];

/*functions*/
//mounts the passed file system
int fs_mount(const char *diskname)
{
    //check if disk can be opened
    if (block_disk_open(diskname) == -1) {
        return -1;
    }

	//if disk is successfully opened, then intialize meta-information
    sb = (struct superBlock*)malloc(sizeof(struct superBlock));

    /*SUPERBLOCK*/
    //check if superblock can be read
    if (block_read(0, sb) == -1) {
        return -1;
    }
    //checking signature
    for (int i = 0; SIGNATURE_CHECK[i] != '\0'; i++) { 
        if ((char)(sb->signature[i]) != SIGNATURE_CHECK[i]) {
            return -1;
        }
    }
    //checking total amount of blocks of virtual disk
    if (sb->numBlocks != block_disk_count()) {
        return -1;
    }
    //checking if numFBlocks is correct
    int expectedFB = (sb->numDBlocks * 2) / 4096;
    if ((sb->numDBlocks * 2) % 4096 > 0) {
        expectedFB++;
    }
    if (sb->numFBlocks != expectedFB) {
        return -1;
    }
    //checking if rootIndex is correct
    if (sb->rootIndex != 1 + sb->numFBlocks) {
        return -1;
    }
    //checking if rootIndex is correct
    if (sb->dataIndex != 1 + sb->rootIndex) {
        return -1;
    }
    //checking if numDBlocks is correct
    if (sb->numDBlocks != sb->numBlocks - sb->dataIndex) {
        return -1;
    }
    
    /*FILE ALLOCATION TABLE*/
    fat = (uint16_t*)malloc(sizeof(struct superBlock) * sb->numFBlocks);
    //check if file allocation table can be read
    //cycle through each FAT block and read
    int fatIndex = 1;
    for (int i = 0; i < sb->numFBlocks; i++) {
        if (block_read(fatIndex, fat + ((BLOCK_BYTES / 2) * i)) == -1) {
            return -1;
        }
        fatIndex++;
    }

    /*ROOT DIRECTORY*/
    root = (struct rootDirectory*)malloc(sizeof(struct rootDirectory));
    //check if root directory can be read
    if (block_read(sb->rootIndex, root) == -1) {
        return -1;
    }
    //return 0 if successfully mounted
    return 0;
}

int fs_umount(void)
{
    /*WRITING BACK TO DISK*/
	//write superblock back to disk
    if (block_write(0, sb) == -1) {
        return -1;
    }
       
    //write file allocation table data back to disk
    int fatIndex = 1;
    for (int i = 0; i < sb->numFBlocks; i++) {
        if (block_write(fatIndex, fat + ((BLOCK_BYTES / 2) * i)) == -1) {
            return -1;
        }
        fatIndex++;
    }
    //write root directory back to disk
    if (block_write(sb->rootIndex, root) == -1) {
        return -1;
    }
        
    /*FREEING VARIABLES*/
    free(sb);
    free(fat);
    free(root);

    //close disk
    if (block_disk_close() == -1) {
        return -1;
    }

    //return 0 if successfully unmounted
    return 0;
}

int fs_info(void)
{
	//check if a virtual disk was opened
    if (sb == NULL) {
        return -1;
    }

    //printing basic info
    printf("FS Info:\n");
    printf("total_blk_count=%d\n", sb->numBlocks);
    printf("fat_blk_count=%d\n", sb->numFBlocks);
    printf("rdir_blk=%d\n", sb->rootIndex);
    printf("data_blk=%d\n", sb->dataIndex);
    printf("data_blk_count=%d\n", sb->numDBlocks);

    //calculating fat free ratio
    int freeFat = 0;
    for (int i = 0; i < sb->numDBlocks; i++) {
        if (fat[i] == 0) {
            freeFat++;
        }
    }
    printf("fat_free_ratio=%d/%d\n", freeFat, sb->numDBlocks);

    //calculate rdir free ratio
    //set variable as max possible. cycle through and decrement for each empty fd
    int freeFd = 0;
    for (int i = 0; i < NUM_ROOTDIR_ENTRIES; i++) {
        if ((root->files[i].filename[0]) == '\0') {
            freeFd++;
        }
    }
    printf("rdir_free_ratio=%d/%d\n", freeFd, NUM_ROOTDIR_ENTRIES);

    //return 0 if info has been successfully printed
    return 0;
}

int fs_create(const char *filename)
{
    /*FILENAME CHECKING*/
    //check if filename is valid or too long
    if (strlen(filename) > FILENAME_MAX_SIZE || filename == NULL) {
        return -1;
    }
    //check if filename is a duplicate
    char *tempname;
	for (int i = 0; i < NUM_ROOTDIR_ENTRIES; i++) {
        tempname = (char*)root->files[i].filename;
        if(strcmp(filename, tempname) == 0) {
            return -1;
        }
    }

    /*SEARCHING FOR OPEN ROOT DIRECTORY ENTRY*/
    //if checks pass, then find open root directory entry
    int freeEntryIndex = -1;
    for (int i = 0; i < NUM_ROOTDIR_ENTRIES; i++) {
        if ((root->files[i].filename[0]) == '\0') {
            freeEntryIndex = i;
            break;
        }
    }
    //if no entries were open, then return -1
    if (freeEntryIndex == -1) {
        return -1;
    }



    /*MANAGING INFO IN NEW ENTRY*/
    //updating filename and size for new entry
    strcpy((char*)root->files[freeEntryIndex].filename, filename);
    root->files[freeEntryIndex].size = 0;
    
    //find an empty spot in FAT to set to firstIndex
    int freeFATIndex = -1;
    for (int i = 0; i < sb->numDBlocks; i++) {
        if (fat[i] == 0) {
            freeFATIndex = i;
            fat[i] = 0xFFFF;
            break;
        }
    }
    //if no space in FAT is open
    if (freeFATIndex == -1) {
        return -1;
    }
    root->files[freeEntryIndex].firstIndex = freeFATIndex;

    //return 0 if successfully created file
    return 0;
}

int fs_delete(const char *filename)
{
	/*FILENAME CHECKING*/
    //check if filename is valid
    if (filename == NULL) {
        return -1;
    }

    /*FINDING FILE WITH THE FILENAME*/
    //check if filename exists
    char *tempname;
    int fileIndex = -1;
    for (int i = 0; i < NUM_ROOTDIR_ENTRIES; i++) {
        tempname = (char*)root->files[i].filename;
        if(strcmp(filename, tempname) == 0) {
            fileIndex = i;
            break;
        }
    }
    //if filename doesn't exist, return -1
    if (fileIndex == -1) {
        return -1;
    }

    /*CHECK IF FILE IS OPEN*/
    //cycle through and check opened file descriptors
    for (int i = 0; i < MAX_OPEN_FILE_DESCRIPTORS; i++) {
        tempname = (char*)openedFiles[i].filename;
        if(strcmp(filename, tempname) == 0) {
            return -1;
        }
    }

    /*DELETE FILE*/
    //remove data from FAT
    uint16_t tempFATIndex = root->files[fileIndex].firstIndex;
    uint16_t temp;
    while (tempFATIndex != 0xFFFF) {
        temp = tempFATIndex;
        tempFATIndex = fat[tempFATIndex];
        fat[temp] = 0;
    }
    //remove data from root directory
    root->files[fileIndex].filename[0] = '\0';

    //return 0 if successfully deleted file
    return 0;
}

int fs_printFileBlocks() 
{
    //check if a virtual disk was opened
    if (sb == NULL) {
        return -1;
    }

    /*PRINTING*/
    //print first line prompt
    printf("FS Ls w/ blocks:\n");
    //for loop to print details of each file
    char *tempname;
    for (int i = 0; i < NUM_ROOTDIR_ENTRIES; i++) {
        if ((root->files[i].filename[0]) != '\0') {
            tempname = (char*)root->files[i].filename;
            printf("file: %s, size: %d, data_blk: %d\n", tempname, 
                root->files[i].size, root->files[i].firstIndex);
            uint16_t index = fat[root->files[i].firstIndex];
            int i = 2;
            while (index != 0xFFFF) {
                fprintf(stderr, "\tBlock[%d]=%d\n", i, index);
                index = fat[index];
                ++i;
            }
        }
    }

    //return 0 if listed files
    return 0;   
}

int fs_ls(void)
{
	//check if a virtual disk was opened
    if (sb == NULL) {
        return -1;
    }

    /*PRINTING*/
    //print first line prompt
    printf("FS Ls:\n");
    //for loop to print details of each file
    char *tempname;
    for (int i = 0; i < NUM_ROOTDIR_ENTRIES; i++) {
        if ((root->files[i].filename[0]) != '\0') {
            tempname = (char*)root->files[i].filename;
            printf("file: %s, size: %d, data_blk: %d\n", tempname, 
                root->files[i].size, root->files[i].firstIndex);
        }
    }

    if (FS_DEBUG) fs_printFileBlocks();
    //return 0 if listed files
    return 0;
}

int fs_open(const char *filename)
{
	/*FILENAME/MAX OPEN CHECKING*/
    //check if filename is valid
    if (filename == NULL) {
        return -1;
    }
    //check if there are already max number of files opened
    int numOpened = 0;
    for (int i = 0; i < MAX_OPEN_FILE_DESCRIPTORS; i++) {
        if (openedFiles[i].filename[0] != '\0') {
            numOpened++;
        }
    }
    //if there are max number of files opened, reteurn -1
    if(numOpened == MAX_OPEN_FILE_DESCRIPTORS) {
        return -1;
    }
    //check if filename exists
    char *tempname;
    int fileIndex = -1;
    for (int i = 0; i < NUM_ROOTDIR_ENTRIES; i++) {
        tempname = (char*)root->files[i].filename;
        if(strcmp(filename, tempname) == 0) {
            fileIndex = i;
            break;
        }
    }
    //if filename doesn't exist, return -1
    if (fileIndex == -1) {
        return -1;
    }
    
    /*OPENING FILE*/
    //search for first entry that is free in openedFiles
    int freeEntryIndex = -1;
    for (int i = 0; i < MAX_OPEN_FILE_DESCRIPTORS; i++) {
        if (openedFiles[i].filename[0] == '\0') {
            freeEntryIndex = i;
            break;
        }
    }
    //make new file descriptor
    strcpy((char*)openedFiles[freeEntryIndex].filename, filename);
    openedFiles[freeEntryIndex].offset = 0;

    //return file descriptor when file is successfully opened
    return freeEntryIndex;
}

int fs_close(int fd)
{
	/*CHECKING IF FD IS VALID*/
    //return -1 if fd is out of bounds
    if (fd < 0 || fd > MAX_OPEN_FILE_DESCRIPTORS - 1) {
        return -1;
    }
    //return -1 if fd is not opened
    if (openedFiles[fd].filename[0] == '\0') {
        return -1;
    }

    /*CLOSING FILE */
    openedFiles[fd].filename[0] = '\0';
    openedFiles[fd].offset = 0;

    //return 0 when file is successfully closed
    return 0;
}

int fs_stat(int fd)
{
	/*CHECKING IF FD IS VALID*/
    //return -1 if fd is out of bounds
    if (fd < 0 || fd > MAX_OPEN_FILE_DESCRIPTORS - 1) {
        return -1;
    }
    //return -1 if fd is not opened
    if (openedFiles[fd].filename[0] == '\0') {
        return -1;
    }

    /*GETTING SIZE*/
    //search through root directory and find file index
    char *filename = (char*)openedFiles[fd].filename;
    char *tempname;
    int fileIndex = -1;
    for (int i = 0; i < NUM_ROOTDIR_ENTRIES; i++) {
        tempname = (char*)root->files[i].filename;
        if(strcmp(filename, tempname) == 0) {
            fileIndex = i;
            break;
        }
    }
    //get corresponding size and return it
    int size = root->files[fileIndex].size;
    return size;
}

int fs_lseek(int fd, size_t offset)
{
	/*CHECKING IF FD IS VALID*/
    //return -1 if fd is out of bounds
    if (fd < 0 || fd > MAX_OPEN_FILE_DESCRIPTORS - 1) {
        return -1;
    }
    //return -1 if fd is not opened
    if (openedFiles[fd].filename[0] == '\0') {
        return -1;
    }

    /*CHECKING IF OFFSET IS VALID*/
    //search through root directory and find file index
    char *filename = (char*)openedFiles[fd].filename;
    char *tempname;
    int fileIndex = -1;
    for (int i = 0; i < NUM_ROOTDIR_ENTRIES; i++) {
        tempname = (char*)root->files[i].filename;
        if(strcmp(filename, tempname) == 0) {
            fileIndex = i;
            break;
        }
    }
    //return -1 if offset is out of bounds
    if (offset < 0 || offset > root->files[fileIndex].size) {
        return -1;
    }

    /*SETTING OFFSET*/
    //set new offset
    openedFiles[fd].offset = offset;

    //return 0 when offset is updated successfully 
    return 0;
}

int fs_write(int fd, void *buf, size_t count)
{
    /*CHECKING IF FD IS VALID*/
    //return -1 if fd is out of bounds
    if (fd < 0 || fd > MAX_OPEN_FILE_DESCRIPTORS - 1) {
        return -1;
    }
    //return -1 if fd is not opened
    if (openedFiles[fd].filename[0] == '\0') {
        return -1;
    }
    //skip if nothing to write
    if (count == 0) {
        return 0;
    }

    if (FS_DEBUG) fprintf(stderr,"fs_write: fd=%d, count=%ld\n", fd, count);

    /*FINDING FILE IN ROOT DIRECTORY*/
    //search through root directory
    char *filename = (char*)openedFiles[fd].filename;
    char *tempname;
    int fileIndex = -1;
    for (int i = 0; i < NUM_ROOTDIR_ENTRIES; i++) {
        tempname = (char*)root->files[i].filename;
        if(strcmp(filename, tempname) == 0) {
            fileIndex = i;
            break;
        }
    }
    //set current block index
    uint16_t currentIndex = root->files[fileIndex].firstIndex;

    /*CHECKING HOW MUCH SPACE IS NEEDED*/
    //calculate how many total blocks are needed
    int totalBytes = openedFiles[fd].offset + (int)count; 
    int totalBlocks = totalBytes / BLOCK_BYTES;
    if (totalBytes % BLOCK_BYTES > 0) {
        totalBlocks++;
    }

    if (FS_DEBUG) fprintf(stderr,"fs_write: fd=%d, totalBlocks=%d\n", fd, totalBlocks);

    //calculating how many data blocks we have
    int blocksHave = 0;
    uint16_t lastIndex = currentIndex;
    while (currentIndex != 0xFFFF) {
        lastIndex = currentIndex;
        blocksHave++;
        currentIndex = fat[currentIndex];
    }
    //calculating how many more blocks we need
    int blocksNeeded = totalBlocks - blocksHave;

    if (FS_DEBUG) fprintf(stderr,"fs_write: fd=%d, blocksNeeded=%d\n", fd, blocksNeeded);

    /*ASSIGN/DEASSIGN BLOCKS TO MEET TOTAL NUMBER OF BLOCKS*/
    //if blocks need to be assigned, assign as many as possible
    if (blocksNeeded > 0) {
        //start assigning blocks as necessary
        int i = 0;
        while (blocksNeeded > 0 && i < sb->numDBlocks) {
            if (FS_DEBUG) fprintf(stderr, "fs_write: Finding Blocks blocksNeeded=%d, i=%d, lastIndex=%d\n",
                blocksNeeded, i, lastIndex);
            //if fat entry is empty then assign new block
            if (fat[i] == 0) {
                if (FS_DEBUG) fprintf(stderr, "fs_write: Assigning block %d\n", i);
                blocksNeeded--;
                if (blocksNeeded == 0) fat[i] = 0xFFFF;
                fat[lastIndex] = i;
                lastIndex = i;
            }
            if (FS_DEBUG) fs_printFileBlocks();

            i++;
        }
    }
    //if blocks need to be deassigned, deassign excess blocks
    else if (blocksNeeded < 0) {
        //go to last blocked needed and deassign rest
        int blockCount = 0;
        currentIndex = root->files[fileIndex].firstIndex;
        while (blocksNeeded != 0) {
            if (blockCount < totalBlocks) {
                
            }
            blockCount++;
            currentIndex = fat[currentIndex];
            
        }
    }

    if (FS_DEBUG) fs_printFileBlocks();

    if (FS_DEBUG) fprintf(stderr,"fs_write: fd=%d, totalBlocks=%d\n", 
        fileIndex, totalBlocks);

    /*COPY FROM BOUNCE BUFFER*/
    //copying over data blocks into BLOCK_BYTES bounce buffer
    char bounce[BLOCK_BYTES];
    int i = 0;

    /*COPY TO FINAL BUFFER WITH OFFSET AND COUNT*/
    //calculate how many bytes can be read
    int totalCount = count - openedFiles[fd].offset;
    int realCount = totalCount;
    int startOffset = openedFiles[fd].offset;
    int copyCount = 0;
    currentIndex = root->files[fileIndex].firstIndex;

    for (i=0; i < totalBlocks; ++i) {
        // after first block, reset startOffset to 0
        if (i > 0) startOffset = 0;
        // set size for strncopy
        if (realCount > BLOCK_BYTES) {
            copyCount = BLOCK_BYTES;
        } else {
            copyCount = realCount;
        }
        if (FS_DEBUG) fprintf(stderr, "fs_write: currentIndex=%d, start=%d, count=%d\n",
            currentIndex, startOffset, realCount);
        //copy to final buffer
        strncpy(bounce + startOffset, buf+(i*BLOCK_BYTES), copyCount);
        block_write(currentIndex + sb->dataIndex, bounce);
        currentIndex = fat[currentIndex];
        realCount = realCount - BLOCK_BYTES;
    }  
    
    //change size and update offset
    root->files[fileIndex].size = count + openedFiles[fd].offset;
    openedFiles[fd].offset = root->files[fileIndex].size;
    if (FS_DEBUG) fprintf(stderr, "fs_write: size=%d, offset=%d\n",
        root->files[fileIndex].size, openedFiles[fd].offset);
    
    //return final count of bytes read if successfully read
    return totalCount;

    return 0;
}

int fs_read(int fd, void *buf, size_t count)
{
	/*CHECKING IF FD IS VALID*/
    //return -1 if fd is out of bounds
    if (fd < 0 || fd > MAX_OPEN_FILE_DESCRIPTORS - 1) {
        return -1;
    }
    //return -1 if fd is not opened
    if (openedFiles[fd].filename[0] == '\0') {
        return -1;
    }
    //skip if nothing to read
    if (count == 0) {
        return 0;
    }

    if (FS_DEBUG) fprintf(stderr,"fs_read: fd=%d, count=%ld\n", fd, count);

    /*FIND OUT NECESSARY VARIABLES*/
    //search through root directory and find file index
    char *filename = (char*)openedFiles[fd].filename;
    char *tempname;
    int fileIndex = -1;
    for (int i = 0; i < NUM_ROOTDIR_ENTRIES; i++) {
        tempname = (char*)root->files[i].filename;
        if(strcmp(filename, tempname) == 0) {
            fileIndex = i;
            break;
        }
    }

    //finding first index
    uint16_t currentIndex = root->files[fileIndex].firstIndex;
    //finding number of total blocks
    int totalBlocks = root->files[fileIndex].size / BLOCK_BYTES;
    if (root->files[fileIndex].size % BLOCK_BYTES) {
        totalBlocks++;
    }

    if (FS_DEBUG) fprintf(stderr,"fs_read: fileIndex=%d, totalBlocks=%d\n", 
        fileIndex, totalBlocks);

    /*COPY ONTO BOUNCE BUFFER*/
    //copying over data blocks into BLOCK_BYTES bounce buffer
    char bounce[BLOCK_BYTES];
    int i = 0;

    /*COPY TO FINAL BUFFER WITH OFFSET AND COUNT*/
    //calculate how many bytes can be read
    int totalCount = count - openedFiles[fd].offset;
    int realCount = totalCount;
    int startOffset = openedFiles[fd].offset;
    int copyCount = 0;

    for (i=0; i < totalBlocks; ++i) {
        // after first block, reset startOffset to 0
        if (i > 0) startOffset = 0;
        // set size for strncopy
        if (realCount > BLOCK_BYTES) {
            copyCount = BLOCK_BYTES;
        } else {
            copyCount = realCount;
        }
        if (FS_DEBUG) fprintf(stderr, "fs_read: currentIndex=%d, start=%d, count=%d\n",
            currentIndex, startOffset, realCount);
        block_read(currentIndex + sb->dataIndex, bounce);
        //copy to final buffer
        strncpy(buf+(i*BLOCK_BYTES), bounce + startOffset, copyCount);
        currentIndex = fat[currentIndex];
        realCount = realCount - BLOCK_BYTES;
    }  
    
    //change offset
    openedFiles[fd].offset = root->files[fileIndex].size;
    
    //return final count of bytes read if successfully read
    return totalCount;
}
