#include "fs.h"
#include "disk.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>

#define FS_MAGIC 0xf0f03410
#define INODES_PER_BLOCK 128
#define POINTERS_PER_INODE 5
#define POINTERS_PER_BLOCK 1024

// Returns the number of dedicated inode blocks given the disk size in blocks
#define NUM_INODE_BLOCKS(disk_size_in_blocks) (1 + (disk_size_in_blocks / 10))

struct fs_superblock
{
    int magic;        // Magic bytes
    int nblocks;      // Size of the disk in number of blocks
    int ninodeblocks; // Number of blocks dedicated to inodes
    int ninodes;      // Number of dedicated inodes
};

struct fs_inode
{
    int isvalid;                    // 1 if valid (in use), 0 otherwise
    int size;                       // Size of file in bytes
    int direct[POINTERS_PER_INODE]; // Direct data block numbers (0 if invalid)
    int indirect;                   // Indirect data block number (0 if invalid)
};

union fs_block
{
    struct fs_superblock super;              // Superblock
    struct fs_inode inode[INODES_PER_BLOCK]; // Block of inodes
    int pointers[POINTERS_PER_BLOCK];        // Indirect block of direct data block numbers
    char data[DISK_BLOCK_SIZE];              // Data block
};


// TRUE  -> inode is free
// FALSE -> inode is used
bool* freeInodesBitMap;

bool* freeBlockBitMap;

int findOpenINode() {
  union fs_block block;
  disk_read(0, block.data);
  for (int i = 0; block.super.ninodes; i++) {
    if (freeInodesBitMap[i] == true) {
        return i;
      }
  }
  return -1;
}

int findOpenBlock() {
  union fs_block block;
  disk_read(0, block.data);
  for (int i = 0; i < block.super.nblocks - block.super.ninodeblocks; i++) {
    if (freeBlockBitMap[i] == true) {
      return i;
    }
  }

  return -1;
}

int findOpenIndirectBlock() {
  union fs_block block;
  disk_read(0, block.data);
  for (int i = block.super.nblocks - block.super.ninodeblocks; i < block.super.nblocks; i++) {
    if (freeBlockBitMap[i] == true) {
      return i;
    }
  }

  return -1;
}

void fs_debug() {
  union fs_block block;

  disk_read(0, block.data);

  int totalInodeBlocks = block.super.ninodeblocks;

  printf("superblock:\n");
  printf("    %d blocks\n", block.super.nblocks);
  printf("    %d inode blocks\n", block.super.ninodeblocks);
  printf("    %d inodes\n", block.super.ninodes);

  for (int i = 1; i < 1 + totalInodeBlocks; i++) {
    printf("__inode block %d__\n", i);
    disk_read(i, block.data);
    for (int j = 0; j < INODES_PER_BLOCK; j++) {
      //printf("inode %d (isvalid = %d):\n", (i-1)*128+j, block.inode[j].isvalid);
      if (block.inode[j].isvalid == 1) {
        printf("inode %d:\n", (i-1)*128+j);
        printf("    size: %d bytes\n", block.inode[j].size);
        bool atLeastOne = false;
        for (int k = 0; k < POINTERS_PER_INODE; k++) {
          if (block.inode[j].direct[k] != 0) {
            if (!atLeastOne) {
              printf("    direct blocks: ");
              atLeastOne = true;
            }
            printf("%d ", block.inode[j].direct[k]);
          }
        }
        if (atLeastOne) {
          printf("\n");
        }
        if (block.inode[j].indirect != 0) {
          printf("    indirect block: %d\n", block.inode[j].indirect);
          printf("    indirect data blocks: ");
          union fs_block indirect;
          disk_read(block.inode[j].indirect, indirect.data);
          for (int z = 0; z < POINTERS_PER_BLOCK; z++) {
            if (indirect.pointers[z] != 0) {
              printf(" %d", indirect.pointers[z]);
            }
          }
          // printf("    indirect data blocks: %d %d %d ...\n", block.inode[j].indirect+1, block.inode[j].indirect+2, block.inode[j].indirect+3);
          printf("\n");
        }
      }
    }
  }
   disk_read(0, block.data);
   printf("__FreeInodeBitMap__\n");
   for (int i = 0; i < block.super.ninodeblocks * INODES_PER_BLOCK; i++) {
     if (freeInodesBitMap[i]) {
      // printf("inode %d: Free\n", i);
     }
     else {
       printf("inode %d: In Use\n", i);
     }
   }

  printf("__FreeBlockBitMap__\n");
  for (int i = 0; i < block.super.nblocks - block.super.ninodeblocks - 1; i++) {
    if (freeBlockBitMap[i]) {
      //printf("%d: Free\n", i+1+block.super.ninodeblocks);
    }
    else {
      printf("%d: In Use\n", i+1+block.super.ninodeblocks);
    }
  }
}

// DONE (?)
int fs_format() {
  // erase all data currently on disk
  union fs_block empty;
  // not sure if this is the way to create a empty block
  for (int i = 0; i < DISK_BLOCK_SIZE; i++) {
    empty.data[i] = 0;
  }
  for (int i = 0; i < disk_size(); i++) {
    disk_write(i, empty.data);
  }
  union fs_block block;

  // put superblock info into disk
  block.super.magic = FS_MAGIC;
  block.super.nblocks = disk_size();
  block.super.ninodeblocks = NUM_INODE_BLOCKS(disk_size());
  block.super.ninodes = block.super.ninodeblocks * INODES_PER_BLOCK;
  disk_write(0, block.data);
  return 1;
}

// indirect still needs to be finished
int fs_mount() {
  // check that superblock is formatted
  union fs_block super;
  disk_read(0, super.data);
  if (super.super.magic != FS_MAGIC) {
    printf("superblock not initialized\n");
    return 0;
  }

  // create inodebitmap
  freeInodesBitMap = (bool*) malloc(super.super.ninodes * sizeof(bool));
  if (freeInodesBitMap == NULL) {
    printf("malloc error\n");
    return 0;
  }

  // create blockbitmap
  freeBlockBitMap = (bool*) malloc((super.super.nblocks - super.super.ninodeblocks - 1) * sizeof(bool));
  if (freeBlockBitMap == NULL) {
    printf("malloc error\n");
    return 0;
  }

  //initialize maps
  // start all data blocks as free
  for (int i = 0; i < super.super.nblocks - super.super.ninodeblocks - 1; i++) {
    freeBlockBitMap[i] = true;
  }
  for (int i = 1; i <= super.super.ninodeblocks; i++) {
    union fs_block block;
    disk_read(i, block.data);
    for (int j = 0; j < INODES_PER_BLOCK; j++) {
      freeInodesBitMap[(i-1)*128+j] = (block.inode[j].isvalid == 0);
      // inode is in use, check direct/indirect
      if (!freeInodesBitMap[(i-1)*128+j]) {
        for (int k = 0; k < POINTERS_PER_INODE; k++) {
          if (block.inode[j].direct[k] != 0) {
            freeBlockBitMap[block.inode[j].direct[k] - super.super.ninodeblocks - 1] = false;
          }
        }
        if (block.inode[j].indirect != 0) {
          freeBlockBitMap[block.inode[j].indirect - super.super.ninodeblocks - 1] = false;
          // what other blocks does this include???
          union fs_block indirect;
          disk_read(block.inode[j].indirect, indirect.data);
          for (int i = 0; i < POINTERS_PER_BLOCK; i++) {
            if (indirect.pointers[i] != 0) {
              freeBlockBitMap[indirect.pointers[i] - super.super.ninodeblocks - 1] = false;
            }
          }
        }
      }
    }
  }
  return 1;
}

int fs_unmount() {
    if (freeBlockBitMap == NULL) {
      printf("unmount error, blockbitmap already freed\n");
      return 0;
    }
    if (freeInodesBitMap == NULL) {
      printf("unmount error, inodebitmap already freed\n");
      return 0;
    }
    free(freeBlockBitMap);
    free(freeInodesBitMap);
    freeBlockBitMap = NULL;
    freeInodesBitMap = NULL;
    return 1;
}

int fs_create() {
  int inodeNumber = findOpenINode();
  if (inodeNumber == -1) {
    printf("fail, no free inodes");
    return -1;
  }
  union fs_block block;
  int inodeBlock = 1 + inodeNumber / INODES_PER_BLOCK;
  int inodePosition = inodeNumber % INODES_PER_BLOCK;
  disk_read(inodeBlock, block.data);
  block.inode[inodePosition].isvalid = 1;
  block.inode[inodePosition].size = 0;
  for (int i = 0; i < POINTERS_PER_INODE; i++) {
      block.inode[inodePosition].direct[i] = 0;
  }
  block.inode[inodePosition].indirect = 0;
  disk_write(inodeBlock, block.data);
  freeInodesBitMap[inodeNumber] = false;
  return inodePosition;
}

int fs_delete(int inumber) {
  int inodeBlock = 1 + inumber / INODES_PER_BLOCK;
  int inodePosition = inumber % INODES_PER_BLOCK;
  union fs_block block;
  disk_read(inodeBlock, block.data);
  union fs_block super;
  disk_read(0, super.data);
  union fs_block empty;
  for (int i = 0; i < DISK_BLOCK_SIZE; i++) {
    empty.data[i] = 0;
  }
  if (block.inode[inodePosition].isvalid == 0) {
    printf("error, nothing to delete\n");
    return 0;
  }
  block.inode[inodePosition].isvalid = 0;
  block.inode[inodePosition].size = 0;

  // clear out direct array
  for (int i = 0; i < POINTERS_PER_INODE; i++) {
    if (block.inode[inodePosition].direct[i] != 0) {
      freeBlockBitMap[block.inode[inodePosition].direct[i] - super.super.ninodeblocks - 1] = true;
      disk_write(block.inode[inodePosition].direct[i], empty.data);
      block.inode[inodePosition].direct[i] = 0;
    }
  }

  // clear out indirect
  if (block.inode[inodePosition].indirect != 0) {
    union fs_block indirect;
    disk_read(block.inode[inodePosition].indirect, indirect.data);
    for (int i = 0; i < POINTERS_PER_BLOCK; i++) {
      if (indirect.pointers[i] != 0) {
        freeBlockBitMap[indirect.pointers[i]] = true;
        disk_write(indirect.pointers[i], empty.data);
      }
    }
    freeBlockBitMap[block.inode[inodePosition].indirect - super.super.ninodeblocks - 1] = true;
    disk_write(block.inode[inodePosition].indirect, empty.data);
    block.inode[inodePosition].indirect = 0;
  }
  disk_write(inodeBlock, block.data);
  freeInodesBitMap[inumber] = true;
  return 1;
}

int fs_getsize(int inumber) {
  int inodeBlock = 1 + inumber / INODES_PER_BLOCK;
  int inodePosition = inumber % INODES_PER_BLOCK;
  union fs_block block;
  disk_read(inodeBlock, block.data);
  if (block.inode[inodePosition].isvalid == 0) {
    printf("error, inode doesn't exist\n");
    return -1;
  }
  return block.inode[inodePosition].size;
}

int fs_read(int inumber, char *data, int length, int offset) {
  int inodeBlock = 1 + inumber / INODES_PER_BLOCK;
  int inodePosition = inumber % INODES_PER_BLOCK;
  union fs_block block;
  disk_read(inodeBlock, block.data);
  // check that inode is valid
  if (block.inode[inodePosition].isvalid == 0) {
    printf("error, inode doesn't exist\n");
    return 0;
  }
  // check if inode has data at offset
  if (offset > fs_getsize(inumber)) {
    printf("error, offset is larger then inode size\n");
    return 0;
  }
  // not efficient to do it this one byte at a time, don't know how important that is
  for (int i = 0; i < length; i++) {
    // check if out of data to read
    if (fs_getsize(inumber) < offset+i) {
      //printf("ran out of data to read, returning early\n");
      return i-1;
    }
    union fs_block blockData;
    int dataBlock = (offset+i) / DISK_BLOCK_SIZE;   // the ith data block of this inode, not the actual data block position
    int dataPosition = (offset+i) % DISK_BLOCK_SIZE;
    //   dataBlock is in direct block
    union fs_block super;
    disk_read(0, super.data);
    if (dataBlock < 5) {
      // double check that this block is not free
      if (freeBlockBitMap[block.inode[inodePosition].direct[dataBlock]-1-super.super.ninodeblocks]) {
        printf("error, data block %d not initialized\n", block.inode[inodePosition].direct[dataBlock]);
        return i;
      }
    disk_read(block.inode[inodePosition].direct[dataBlock], blockData.data);
    data[i] = blockData.data[dataPosition];
    }
    // dataBlock is in indirect
    else {
      // double check that indirect block exists
      if (block.inode[inodePosition].indirect == 0) {
        printf("error, indirect data block doesn't exist\n");
        return i;
      }
      union fs_block indirect;
      disk_read(block.inode[inodePosition].indirect, indirect.data);
      // double check that the pointer exists
      if (indirect.pointers[dataBlock-5] == 0) {
        printf("error, indirect pointer doesnt exist\n");
        return i;
      }
      // checks that the data block that is pointed to is not free
      if (freeBlockBitMap[indirect.pointers[dataBlock-5]-1-super.super.ninodeblocks]) {
        printf("error, indirectly linked data block isn't initialized\n");
        return i;
      }
      disk_read(indirect.pointers[dataBlock-5], blockData.data);
      data[i] = blockData.data[dataPosition];
    }
  }
  return length;
}
int fs_write(int inumber, const char *data, int length, int offset)
{
  int inodeBlock = 1 + inumber / INODES_PER_BLOCK;
  int inodePosition = inumber % INODES_PER_BLOCK;

  union fs_block block;
  disk_read(inodeBlock, block.data);

  //check that offset isn't greater than total size
  if (offset > fs_getsize(inumber)) {
    printf("error, offset is larger then inode size\n");
    return 0;
  }

  for (int i = 0 ; i < length; i++) {
    if (fs_getsize(inumber) < offset + i) {
      return i - 1;
    }
    int dataBlock = (offset+i) / DISK_BLOCK_SIZE;   // the ith data block of this inode, not the actual data block position
    int dataPosition = (offset+i) % DISK_BLOCK_SIZE;
    union fs_block super;
    union fs_block indirect;
    
    disk_read(0, super.data);
    //direct block
    if (dataBlock < 5) {
      // double check that this block is not free
      if (freeBlockBitMap[block.inode[inodePosition].direct[dataBlock]-1-super.super.ninodeblocks]) {
        int newBlock = findOpenBlock();
        block.inode[inodePosition].direct[dataBlock] = newBlock;
        freeBlockBitMap[newBlock] = false;
      }
      disk_write(block.inode[inodePosition].direct[dataBlock], data);
    } else { //indirect block
        4; // not done yet
      
    }

  }

  return length;
}
