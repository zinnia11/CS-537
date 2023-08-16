#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>


#include "ufs.h"

int main(int argc, char *argv[]) {
    int fd = open("test.img", O_RDWR);
    assert(fd > -1);

    struct stat sbuf;
    int rc = fstat(fd, &sbuf);
    assert(rc > -1);

    int image_size = (int) sbuf.st_size;

    void *image = mmap(NULL, image_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    assert(image != MAP_FAILED);

    super_t *s = (super_t *) image;
    printf("inode bitmap address %d [len %d]\n", s->inode_bitmap_addr, s->inode_bitmap_len);
    printf("data bitmap address %d [len %d]\n", s->data_bitmap_addr, s->data_bitmap_len);

    inode_t *inode_table = image + (s->inode_region_addr * UFS_BLOCK_SIZE);
    inode_t *root_inode = inode_table;
    printf("\nroot type:%d root size:%d\n", root_inode->type, root_inode->size);
    printf("direct pointers[0]:%d [1]:%d\n", root_inode->direct[0], root_inode->direct[1]);

    dir_ent_t *root_dir = image + (root_inode->direct[0] * UFS_BLOCK_SIZE);
    printf("\nroot dir entries\n%d %s\n", root_dir[0].inum, root_dir[0].name);
    printf("%d %s\n", root_dir[1].inum, root_dir[1].name);

    close(fd);
    return 0;
}

