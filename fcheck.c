/**
 * XV6 Filesystem Notes
 * 
 * Block 0 is unused
 * Block 1 is superblock
 * Block 2 is the beginning of inodes
 * 
 * inode 0 is unallocated
 * inode 1 is root
 * 
 * */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>

#define stat xv6_stat // to prevent error with fcntl stat

#include "include/syscall.h"
#include "include/stat.h"
#include "include/types.h"
#include "include/fs.h"

#undef stat // to use regular stat structure with fcntl's fstat

struct mapping
{
    void *addr;
    size_t length;
    int fd;
};

struct mapping map;       // mapping from disk image to memory
struct superblock sb;     // superblock data
void *fsptr;              // pointer to current place in memory mapped disk image
struct dinode curr_inode; // pointer to currently referenced inode

// **** Function Signature Definitions ****
// Solution Functions
void check_inode_types();
void check_inuse_valid();
void check_root();
void check_dir_format();
void check_inuse_marked_free();
void check_free_marked_inuse();
void check_direct_addr_once();
void check_indirect_addr_once();
void check_marked_inuse_in_dir();
void check_inuse_in_dir_marked_free();
void check_ref_count();
void check_dir_appear_once();
// Utility Functions
int traverseDir(uint addr, char *name);
int checkDirOfInode(struct dinode curr_inode, char *name);

int main()
{

    // **** SECTION 1: Mapping fs.img into memory so we can work on it from memory instead of reading the file. ****
    map.fd = open("fs.img", 0);

    struct stat fs;
    fstat(map.fd, &fs);
    map.length = fs.st_size;

    map.addr = mmap(NULL, map.length, PROT_READ, MAP_PRIVATE, map.fd, 0);

    // **** SECTION 2: Reading the initial blocks of fs.img ****

    memmove(&sb, map.addr + BSIZE, sizeof(sb)); // Copy superblock

    // uint addresses[sb.size];  // addresses of all blocks

    // uint i;
    // for (i = 0; i < sb.size; i++)
    // {                     // loop through blocks
    //     addresses[i] = 0; // initialize address to 0
    // }

    // **** SECTION 3: Perform all required checks ****
    // printf("Done loading superblock info.\nSuperblock reports size %d\n", sb.size);

    // Part # 1
    check_inode_types(curr_inode);

    // Part # 2

    // Part # 3
    check_root(curr_inode);

    // Part # 4
    check_dir_format();

    munmap(map.addr, map.length);
    return 0;
}

// **** PARTS ****

// Part 1
void check_inode_types()
{
    int inum;
    for (inum = 0; inum < ((int)sb.ninodes); inum++)
    {                                                             // loop through inodes
        fsptr = map.addr + 2 * BSIZE + inum * sizeof(curr_inode); // base addr + starting ino + ino size
        memmove(&curr_inode, fsptr, sizeof(curr_inode));          // Copy the inode into curr_inode
        if (curr_inode.type != 0 && curr_inode.type != T_FILE && curr_inode.type != T_DIR && curr_inode.type != T_DEV)
        { // check if inode type is valid
            munmap(map.addr, map.length);
            fprintf(stderr, "ERROR: bad inode.\n");
            exit(1);
        }
        // printf("INODE %d TYPE: %d\n", i, curr_inode.type);
    }
}

// Part 3
void check_root()
{
    fsptr = map.addr + 2 * BSIZE + sizeof(curr_inode); // first inode is root on block 2 and inode id 1
    memmove(&curr_inode, fsptr, sizeof(curr_inode));
    if (curr_inode.type != T_DIR || checkDirOfInode(curr_inode, ".") != 1 || checkDirOfInode(curr_inode, "..") != 1)
    {
        munmap(map.addr, map.length);
        fprintf(stderr, "ERROR: root directory does not exist.\n");
        exit(1);
    }
}

// Part 4
void check_dir_format()
{
    int inum;
    for (inum = 0; inum < ((int)sb.ninodes); inum++)
    {                                                             // loop through inodes
        fsptr = map.addr + 2 * BSIZE + inum * sizeof(curr_inode); // base addr + starting ino + ino size
        memmove(&curr_inode, fsptr, sizeof(curr_inode));          // Copy the inode into curr_inode
        if (curr_inode.type == T_DIR && (checkDirOfInode(curr_inode, ".") != inum || checkDirOfInode(curr_inode, "..") < 0))
        { // check if inode type is valid
            munmap(map.addr, map.length);
            fprintf(stderr, "ERROR: directory not properly formatted.\n");
            exit(1);
        }
        // printf("INODE %d .: %d, ..:%d\n", i, inum_self, inum_parent);
    }
}

// **** UTILITY ****

int isValidDataBlock(struct dinode curr_inode)
{
    // Determine boundaries
    uint maxBlockNum = sb.size - 1;
    uint maxBmap = sb.size / BPB;
    if (sb.size % BPB != 0)
        maxBmap++;

    // Iterate and check
    int i;
    for (i = 0; i < NDIRECT + 1; i++)
        if (curr_inode.addrs[i] != 0 && (curr_inode.addrs[i] < maxBmap || curr_inode.addrs[i] > maxBlockNum))
            return 0;
    return 0;
}

int traverseDir(uint addr, char *name)
{
    fsptr = map.addr + addr * BSIZE;
    struct dirent buf;
    uint i;
    for (i = 0; i < BSIZE / sizeof(struct dirent); i++)
    {
        memmove(&buf, fsptr + i * sizeof(struct dirent), sizeof(struct dirent));
        if (buf.inum == 0)
            continue;
        if (strncmp(name, buf.name, DIRSIZ) == 0)
            return buf.inum;
    }
    return -1;
}

int checkDirOfInode(struct dinode curr_inode, char *name)
{
    int result = -1;
    uint i;
    for (i = 0; i < NDIRECT; i++)
    { // Check all direct pointers
        //if it is an free directory entry, dont do anything
        if (curr_inode.addrs[i] == 0)
            continue;
        result = traverseDir(curr_inode.addrs[i], name);
        if (result != -1)
            return result;
    }
    if (curr_inode.addrs[NDIRECT] != 0)
    { // Move to indirect pointers
        fsptr = (void *)(intptr_t)(curr_inode.addrs[NDIRECT] * BSIZE);
        uint buf2;
        for (i = 0; i < NINDIRECT; i++)
        { // loop through all indirect pointers
            fsptr = (void *)(curr_inode.addrs[NDIRECT] * BSIZE + i * sizeof(uint));
            memmove(&buf2, fsptr, sizeof(uint));
            result = traverseDir(buf2, name);
            if (result != -1)
                return result;
        }
    }
    return result;
}