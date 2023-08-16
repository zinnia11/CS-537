#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "mfs.h"
#include "ufs.h"
#include "udp.h"
#include "message.h"

typedef struct {
    inode_t inodes[UFS_BLOCK_SIZE / sizeof(inode_t)];
} inode_block;

typedef struct {
	dir_ent_t entries[128];
} dir_block_t;

int sd;

void *image;
super_t *superblock;
int max_inodes;
unsigned *inode_bitmap;
inode_t *inodes;
unsigned *data_bitmap;
void *data;

////////////////////////////////////////////////////////////////////////////////

// handle ctrl-c interrupt so server is shut down correctly
void intHandler(int dummy) {
    UDP_Close(sd);
    exit(130);
}

// get bitmap value
unsigned int get_bit(unsigned int *bitmap, int position) {
   int index = position / 32;
   int offset = 31 - (position % 32);
   return (bitmap[index] >> offset) & 0x1;
}

// set bitmap value to 1 (OR with 1)
void set_bit(unsigned int *bitmap, int position) {
   int index = position / 32;
   int offset = 31 - (position % 32);
   bitmap[index] |= 0x1 << offset;
}

// clear bitmap value to 0 (AND with 0)
void clear_bit(unsigned int *bitmap, int position) {
   int index = position / 32;
   int offset = 31 - (position % 32);
   bitmap[index] &= ~(0x1 << offset);
}

// returns found inum of name from pinum directory
int lookup(int pinum, char *name){
	if (pinum >= max_inodes || get_bit(inode_bitmap, pinum) != 1) {
		printf("Invalid inode\n");
		return -1;
	}
    inode_t* inode = &inodes[pinum];
	if (inode->type != MFS_DIRECTORY) {
		printf("Inode is not a directory\n");
		return -1;
	}
	
    int num_block = inode->size / MFS_BLOCK_SIZE + 1;
    // fill in the entries
    int remain_entry = inode->size / sizeof(dir_ent_t);
    for(int i = 0; i < num_block; i++){
        int loopend;
        if(remain_entry < MFS_BLOCK_SIZE / sizeof(dir_ent_t)){
            loopend = remain_entry;
        }else{
            loopend = MFS_BLOCK_SIZE / sizeof(dir_ent_t);
        }
        dir_block_t* directory = (dir_block_t*)(image + (inode->direct[i]*MFS_BLOCK_SIZE)); // real address
        for(int j = 0; j < loopend; j++){
       		if(directory->entries[j].inum != -1 && !strcmp(name, directory->entries[j].name)){
           		// found
           		return directory->entries[j].inum;
   			}
			remain_entry--;
        }
    }
    return -1;
}

int write_file(inode_t *n, message_t *message) {
	int blockn = message->file_s.offset / MFS_BLOCK_SIZE;
	int block_off = message->file_s.offset % MFS_BLOCK_SIZE;
	int remaining =  message->file_s.nbtyes;
	// use direct array in inodes to get to offset and write file

	// writing bigger than the file, assign a new data block
	if (message->file_s.offset + message->file_s.nbtyes > n->size) {
		int num_block = n->size / MFS_BLOCK_SIZE;
		if (num_block>=DIRECT_PTRS) {
			printf("File is at max size\n");
			return -1;
		}
		// find an unassigned data block (bit is 0)
		for (int k=0; k<superblock->num_data; k++) {
			if (get_bit(data_bitmap, k) == 0) {
				// if not right at the edge of a block
				if (n->size % MFS_BLOCK_SIZE != 0) {
					num_block++;
				}
				// block number starting from the beginning
				n->direct[num_block] = k + superblock->data_bitmap_len 
									+ superblock->inode_bitmap_len + superblock->inode_region_len + 1;
				set_bit(data_bitmap, k);
				break;
			}
		}
		// update size
		n->size = message->file_s.offset + message->file_s.nbtyes;
	} 

	// where in the file we are
	char *file = (image + (n->direct[blockn]*MFS_BLOCK_SIZE)) + block_off;
	int leftover = 0;
	// goes off a single block
	if (block_off+remaining > MFS_BLOCK_SIZE) {
		leftover = block_off+remaining - MFS_BLOCK_SIZE;
		remaining -= leftover;
	}
	memcpy(file, message->info.buffer, remaining);
	// actual leftover exists
	if (leftover != 0) {
		file = (image + (n->direct[blockn+1]*MFS_BLOCK_SIZE));
		memcpy(file, message->info.buffer + remaining, leftover);
	}
	
	return 0;
}

int read_file(inode_t *n, message_t *message) {
	if (message->file_s.offset + message->file_s.nbtyes > n->size) {
		printf("Read greater than size of file\n");
		return -1;
	}

	int blockn = message->file_s.offset / MFS_BLOCK_SIZE;
	int block_off = message->file_s.offset % MFS_BLOCK_SIZE;
	int remaining =  message->file_s.nbtyes;
	// directory read
	if (n->type == MFS_DIRECTORY) {
		if (message->file_s.offset%sizeof(dir_ent_t) != 0) {
			printf("Offset not aligned for a directory read\n");
			return -1;
		}
		if (message->file_s.nbtyes%sizeof(dir_ent_t) != 0) {
			printf("Byte number not aligned for a directory read\n");
			return -1;
		}
							
		// number of blocks the inode references
		MFS_DirEnt_t *fill = (MFS_DirEnt_t *) message->info.buffer;
		int num_block = n->size / MFS_BLOCK_SIZE + 1;
    	int remain_entry = n->size / sizeof(dir_ent_t);
		for (int i=blockn; i<num_block; i++) {
			int loopend;
        	if(remain_entry < MFS_BLOCK_SIZE / sizeof(dir_ent_t)){
            	loopend = remain_entry;
	        }else{
    	        loopend = MFS_BLOCK_SIZE / sizeof(dir_ent_t);
        	}
    	    dir_block_t* pdirectory = (dir_block_t*)(image + (n->direct[i]*MFS_BLOCK_SIZE)); // real address
			// get directory block, which is just an array of directory entries
			for (int j=0; j<loopend; j++) {
				fill->inum = pdirectory->entries[j].inum;
				strcpy(fill->name, pdirectory->entries[j].name);
				// increment to the next directory entries
				fill++;
				remain_entry--;
			}
		}
	// file read
	} else {
		// where in the file we are
		char *file = (image + (n->direct[blockn]*MFS_BLOCK_SIZE)) + block_off;
		int leftover = 0;
		// goes off a single block
		if (block_off+remaining > MFS_BLOCK_SIZE) {
			leftover = block_off+remaining - MFS_BLOCK_SIZE;
			remaining -= leftover;
		}
		memcpy(message->info.buffer, file, remaining);
		// actual leftover exists
		if (leftover != 0) {
			file = (image + (n->direct[blockn+1]*MFS_BLOCK_SIZE));
			memcpy(message->info.buffer + remaining, file, leftover);
		}
	}
	return 0;
}

// add a directory entry by search for empty directory entry, unassigned inode, and unassigned data block
int add_directory_entry(dir_block_t *directory, message_t *minfo) {
	inode_t *n;
	// loop through directory
	for (int j=0; j<128; j++) {
		// found an empty directory entry
		if (directory->entries[j].inum == -1) {
			// find an unassigned inode (bit is 0)
			for (int k=0; k<superblock->num_inodes; k++) {
				if (get_bit(inode_bitmap, k) == 0) {
					directory->entries[j].inum = k;
					memcpy(directory->entries[j].name, minfo->info.name, 28);
					set_bit(inode_bitmap, k);
					n = &inodes[k]; // actual inode
					break;
				}
			}
			// found a inode
			if (directory->entries[j].inum != -1) {
				n->size = 0;
				n->type = minfo->file_s.ftype;
				if (n->type == MFS_DIRECTORY) {
					// search for free block
					for (int k=0; k<superblock->num_data; k++) {
						if (get_bit(data_bitmap, k) == 0) {
							n->direct[0] = k + superblock->data_bitmap_len 
										+ superblock->inode_bitmap_len + superblock->inode_region_len + 1;
							set_bit(data_bitmap, k);
							// set up the parents in a directory
							dir_block_t* current = (dir_block_t*) (data + k * MFS_BLOCK_SIZE);
							// . points to current directory
							current->entries[0].inum = directory->entries[j].inum;
							strcpy(current->entries[0].name, ".");
							// .. points to parent directory
							current->entries[1].inum = minfo->file_s.inum;
							strcpy(current->entries[1].name, "..");
							// fill the rest of the directory with -1
							for (int x = 2; x < 128; x++) {
								current->entries[x].inum = -1;
							}
							// size is 2 directory entries
							n->size = 2 * sizeof(dir_ent_t);
							return 0;
						}
					}
					// didn't find free block
					return -1;
				}
				return 0;
			} else {
				return -1;
			}
		}
	}	

	// reach here means no empty directory entry, add a new page to the pdirectory
	int found = 0;
	n = &inodes[minfo->file_s.inum];
	int num_block = n->size / MFS_BLOCK_SIZE + 1;
	if (num_block>=DIRECT_PTRS) {
		printf("Directory is at max size\n");
		return -1;
	}
	// find an unassigned data block for new directory block
	for (int k=0; k<superblock->num_data; k++) {
		if (get_bit(data_bitmap, k) == 0) {
			n->direct[num_block] = k;
			set_bit(data_bitmap, k);
			found = 1;
			break;
		}
	}
	if (found == 0) {
		return -1;
	}
	// new directory
	dir_block_t *new_dir = (dir_block_t *) (image + (n->direct[num_block]*MFS_BLOCK_SIZE));
	for (int j=0; j<128; j++) {
		// all directory entries must be -1
		new_dir->entries[j].inum = -1;
	}
	// assign data 
	// find an unassigned inode (bit is 0)
	for (int k=0; k<superblock->num_inodes; k++) {
		if (get_bit(inode_bitmap, k) == 0) {
			new_dir->entries[0].inum = k; // first entry of new directory
			memcpy(new_dir->entries[0].name, minfo->info.name, 28);
			set_bit(inode_bitmap, k);
			n = &inodes[k]; // actual inode
			break;
		}
	}
	// found a inode
	if (new_dir->entries[0].inum != -1) {
		n->size = 0;
		n->type = minfo->file_s.ftype;
		if (n->type == MFS_DIRECTORY) {
			// search for free block
			for (int k=0; k<superblock->num_data; k++) {
				if (get_bit(data_bitmap, k) == 0) {
					n->direct[0] = k + superblock->data_bitmap_len 
								+ superblock->inode_bitmap_len + superblock->inode_region_len + 1;
					set_bit(data_bitmap, k);
					// set up the parents in a directory
					dir_block_t* current = (dir_block_t*) (data + k * MFS_BLOCK_SIZE);
					// . points to current directory
					current->entries[0].inum = directory->entries[0].inum;
					strcpy(current->entries[0].name, ".");
					// .. points to parent directory
					current->entries[1].inum = minfo->file_s.inum;
					strcpy(current->entries[1].name, "..");
					// fill the rest of the directory with -1
					for (int x = 2; x < 128; x++) {
						current->entries[x].inum = -1;
					}
					// size is 2 directory entries
					n->size = 2 * sizeof(dir_ent_t);
					return 0;
				}
			}
			// didn't find free block
			return -1;
		}
		return 0;
	}
	return -1;
}

int unlink_entry(int pinum, char* name) {
	// beginning is same as lookup to find a name
	if (pinum >= max_inodes || get_bit(inode_bitmap, pinum) != 1) {
		printf("Invalid inode\n");
		return -1;
	}
    inode_t* inode = &inodes[pinum];
	if (inode->type != MFS_DIRECTORY) {
		printf("Inode is not a directory\n");
		return -1;
	}
	
	dir_ent_t* entry;
	int found = -1;
    int num_block = inode->size / MFS_BLOCK_SIZE + 1;
    // fill in the entries
    int remain_entry = inode->size / sizeof(dir_ent_t);
    for(int i = 0; i < num_block; i++){
        int loopend;
        if(remain_entry < MFS_BLOCK_SIZE / sizeof(dir_ent_t)){
            loopend = remain_entry;
        }else{
            loopend = MFS_BLOCK_SIZE / sizeof(dir_ent_t);
        }
        dir_block_t* directory = (dir_block_t*)(image + (inode->direct[i]*MFS_BLOCK_SIZE)); // real address
        for(int j = 0; j < loopend; j++){
       		if(directory->entries[j].inum != -1 && !strcmp(name, directory->entries[j].name)){
           		// found
           		entry = &(directory->entries[j]);
				found = 0;
				break;
   			}
			remain_entry--;
        }
		if (found != -1) {
			break;
		}
    }
	// name not found
	if (found == -1) {
		return 0;
	}

	int inum = entry->inum;
	inode_t *n = &inodes[inum];
	// unlink only empty directories
	if (n->type == MFS_DIRECTORY) { 
		int num_block = n->size / MFS_BLOCK_SIZE + 1;
    	int remain_entry = n->size / sizeof(dir_ent_t);
		for (int i=0; i<num_block; i++) {
			int loopend;
        	if(remain_entry < MFS_BLOCK_SIZE / sizeof(dir_ent_t)){
            	loopend = remain_entry;
	        }else{
    	        loopend = MFS_BLOCK_SIZE / sizeof(dir_ent_t);
        	}

    	    dir_block_t* directory = (dir_block_t*)(image + (n->direct[i]*MFS_BLOCK_SIZE)); // real address
			// get directory block, which is just an array of directory entries
			for (int j=0; j<loopend; j++) {
				// the first two entries of the first directory block should be . and ..
				if (i == 0) j+=2;
				if (directory->entries[j].inum != -1) {
					printf("Not an empty directory\n");
					return -1; 
				}
				remain_entry--;
			}
		}
	}
	clear_bit(inode_bitmap, inum);
	// clear data bit map
	num_block = n->size / MFS_BLOCK_SIZE + 1;
	for (int i=0; i<num_block; i++) {
		int data_blk = n->direct[i];
		clear_bit(data_bitmap, data_blk - superblock->data_region_addr);
	}
	entry->inum = -1;
	return 0;
}


// server code
int main(int argc, char *argv[]) {
	// for interrupts
	signal(SIGINT, intHandler);
	int rc;
	// parse command line arguments
	if (argc != 3) {
		printf("please provide a port and a file system image\n");
		return -1;
	}
	int port = atoi(argv[1]);
	const char* path = argv[2];

	// bind to port
    sd = UDP_Open(port);
	if (sd <= 0) {
		printf("Failed to bind UDP socket\n");
		return -1;
	} else {
		printf("Listening for connections at port %i\n", port);
	}

	// open up the file system
	int fd = open(path, O_RDWR | O_APPEND);
    if (fd < 0) {
		printf("image does not exist\n");
	}

	// get stat of the opened file in order to get the size
	struct stat finfo;
	rc = fstat(fd, &finfo);
    assert(rc > -1);
	int im_size = (int) finfo.st_size;

	// use mmap to get file into memory
    image = mmap(NULL, im_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    assert(image != MAP_FAILED);
	close(fd);

	// super block is right where the file system starts
	// file stats
	superblock = (super_t*)image;
	max_inodes = superblock->num_inodes;
	inode_bitmap = image + superblock->inode_bitmap_addr * UFS_BLOCK_SIZE;
	inodes = image + superblock->inode_region_addr * UFS_BLOCK_SIZE;
	data_bitmap = image + superblock->data_bitmap_addr * UFS_BLOCK_SIZE;
	data = image + superblock->data_region_addr * UFS_BLOCK_SIZE;

	printf("Max inum: %i\n", max_inodes);
	printf("Number of inode blocks: %i\n", superblock->inode_region_len);
	printf("Number of data blocks: %i\n", superblock->data_region_len);
	printf("Start of inode section: %i\n", superblock->inode_region_addr);
	printf("Start of data section: %i\n", superblock->data_region_addr);

	// wait for messages from the client
	printf("server:: waiting...\n");
    while (1) {
		struct sockaddr_in client_addr;
		message_t message;
		message_t response;
		
		rc = UDP_Read(sd, &client_addr, (char *) &message, sizeof(message_t));
		printf("server:: read message [size:%d contents:(%d)]\n", rc, message.mtype);
		
		// handle client requests through helper methods
		if (rc > 0) {
			int inum = message.file_s.inum;
			switch (message.mtype){
				// take inode of a directory and searches for a name in it
				// return value will be the inum so store return codes in there
				case MFS_LOOKUP:
					inum = message.file_s.inum;
					rc = lookup(inum, message.info.name);
					response.file_s.inum = rc;
					UDP_Write(sd, &client_addr, (char *) &response, sizeof(message_t));
					break;
				// takes a MFS_Stat_t struct and fills it with info about a file (type, size)
				case MFS_STAT:
					inum = message.file_s.inum;
					if (inum >= max_inodes || get_bit(inode_bitmap, inum) != 1) {
						printf("Invalid inode\n");
						response.rc = -1;
					} else {
						// find inode and fill in information about this file
						inode_t *n = &inodes[inum];
						MFS_Stat_t stat_fill;
						stat_fill.type = n->type;
						stat_fill.size = n->size;

						response.mtype = n->type;
						memcpy(response.info.buffer, &stat_fill, sizeof(MFS_Stat_t));
						response.rc = 0;
					}
					UDP_Write(sd, &client_addr, (char *) &response, sizeof(message_t));
					break;
				// write to a specfied file
				case MFS_WRITE:
					inum = message.file_s.inum;
					if (inum >= max_inodes || get_bit(inode_bitmap, inum) != 1) {
						printf("Invalid inode\n");
						response.rc = -1;
					} else {
						inode_t *n = &inodes[inum];
						if (n->type != MFS_REGULAR_FILE) {
							printf("Cannot write to directory\n");
							response.rc = -1;
							UDP_Write(sd, &client_addr, (char *) &response, sizeof(message_t));
							break;
						}

						rc = write_file(n, &message);
						response.rc = rc;
					}
					if(msync(image, im_size, MS_SYNC) == -1){
        				response.rc = -1;
    				}
					UDP_Write(sd, &client_addr, (char *) &response, sizeof(message_t));
					break;
				// read from a file
				case MFS_READ:
					inum = message.file_s.inum;
					if (inum >= max_inodes || get_bit(inode_bitmap, inum) != 1) {
						printf("Invalid inode\n");
						response.rc = -1;
					} else {
						inode_t *n = &inodes[inum];
						rc = read_file(n, &message);

						if (rc != -1) {
							memcpy(response.info.buffer, message.info.buffer, message.file_s.nbtyes);
						}
						response.rc = rc;
					}
					UDP_Write(sd, &client_addr, (char *) &response, sizeof(message_t));
					break;
				// creates a file in a certain parent directory
				// if the name already exists then return success (0)
				case MFS_CRET:
					inum = message.file_s.inum;
					if (inum >= max_inodes || get_bit(inode_bitmap, inum) != 1) {
						printf("Invalid inode\n");
						response.rc = -1;
						break;
					} else {
						// find inode
						inode_t *n = &inodes[inum];
						if (n->type != MFS_DIRECTORY) {
							printf("Inode is not a directory\n");
							response.rc = -1;
							UDP_Write(sd, &client_addr, (char *) &response, sizeof(message_t));
							break;
						}
						
						inum = lookup(inum, message.info.name);
						// no file with such name
						if (inum == -1) {
							// add a directory entry
							int num_block = n->size / MFS_BLOCK_SIZE + 1;
							for (int i=0; i<num_block; i++) {
								dir_block_t *pdirectory = (dir_block_t *) (image + (n->direct[i]*MFS_BLOCK_SIZE));
								rc = add_directory_entry(pdirectory, &message);
								if (rc == 0) {
									n->size = n->size + sizeof(dir_ent_t);
									response.rc = rc;
									break;
								} else {
									response.rc = -1;
								}
							}
						} else {
							response.rc = 0;
						}
					}
					// sync to disk
					rc = msync(superblock, sizeof(super_t), MS_SYNC);
					assert(rc > -1);
					UDP_Write(sd, &client_addr, (char *) &response, sizeof(message_t));
					break;
				// removes a file or empty directory from a parent directory by setting the inode to invalid
				// sync file system to disk
				case MFS_UNLINK:
					inum = message.file_s.inum;
					rc = unlink_entry(inum, message.info.name);
					response.rc = rc;
					if(msync(image, im_size, MS_SYNC) == -1){
        				response.rc = -1;
    				}
					UDP_Write(sd, &client_addr, (char *) &response, sizeof(message_t));
					break;
				case MFS_SHUTDOWN:
					// sync to disk
					rc = msync(superblock, im_size, MS_SYNC);
    				assert(rc > -1);
                    rc = munmap(image, im_size);
					assert(rc > -1);
					UDP_Close(sd);
					exit(0);
					break;
			}
		} 
    }

    return 0; 
}