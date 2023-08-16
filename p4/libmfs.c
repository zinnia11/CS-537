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

struct sockaddr_in server_addr;
int fd;

/////////////////////////////////////////////////////////////////////////////////////////////

/*
    Takes a host name and port number and uses those to find the server exporting the file system.
*/
int MFS_Init(char *hostname, int port) {
    printf("MFS Init2 %s %d\n", hostname, port);
    int rc = UDP_FillSockAddr(&server_addr, hostname, port);
    if (rc != 0) {
        printf("Failed to set up server address\n");
        return rc;
    }
    // opening a client port
    fd = UDP_Open(11369);
    return 0;
}

/*
    Takes the parent inode number (which should be the inode number of a directory) 
    and looks up the entry name in it. The inode number of name is returned. 
    Success: return inode number of name
    Failure: return -1
    Failure modes: invalid pinum, name does not exist in pinum
*/
int MFS_Lookup(int pinum, char *name) {
    // name should be 27 chars max and /0 terminated
    if (strlen(name) > 27) {
        return -1;
    }
    message_t message;
    message.mtype = MFS_LOOKUP;
    message.file_s.inum = pinum;
    strcpy(message.info.name, name);

    UDP_Write(fd, &server_addr, (char *) &message, sizeof(message_t));

    // TODO: implement timeout ?
    short int sock = socket(AF_INET, SOCK_STREAM, 0);
    fd_set fdset;
    struct timeval timeout;
    timeout.tv_sec  = 5;
    timeout.tv_usec = 0;
    FD_SET(sock, &fdset);
    while(select(sock+1, NULL, &fdset, NULL, &timeout) <= 0) {
        UDP_Write(fd, &server_addr, (char *) &message, sizeof(message_t));
    }

    message_t response; 
    struct sockaddr_in ret_addr; 
    UDP_Read(fd, &ret_addr, (char *) &response, sizeof(message_t));

    return response.file_s.inum;
}

/*
    Returns some information about the file specified by inum. 
    The exact info returned is defined by MFS_Stat_t.
    Upon success, return 0, otherwise -1. 
    Failure modes: inum does not exist.
*/
int MFS_Stat(int inum, MFS_Stat_t *m) {
    message_t message;
    message.mtype = MFS_STAT;
    message.file_s.inum = inum;

    UDP_Write(fd, &server_addr, (char *) &message, sizeof(message_t));

    // TODO: implement timeout ?
    short int sock = socket(AF_INET, SOCK_STREAM, 0);
    fd_set fdset;
    struct timeval timeout;
    timeout.tv_sec  = 5;
    timeout.tv_usec = 0;
    FD_SET(sock, &fdset);
    while(select(sock+1, NULL, &fdset, NULL, &timeout) <= 0) {
        UDP_Write(fd, &server_addr, (char *) &message, sizeof(message_t));
    }

    message_t response; 
    struct sockaddr_in ret_addr; 
    UDP_Read(fd, &ret_addr, (char *) &response, sizeof(message_t));
    memcpy(m, response.info.buffer, sizeof(MFS_Stat_t));

    return response.rc;
}

/*
    Writes a buffer of size nbytes (max size: 4096 bytes) at the byte offset specified by offset. 
    Returns 0 on success, -1 on failure. 
    Failure modes: invalid inum, invalid nbytes, invalid offset, 
                not a regular file (because you can't write to directories)
*/
int MFS_Write(int inum, char *buffer, int offset, int nbytes) {
    message_t message;
    message.mtype = MFS_WRITE;
    message.file_s.inum = inum;
    if (offset<0){
        return -1;
    }
    if(offset/MFS_BLOCK_SIZE >= 30) {
        return -1;
    }
    message.file_s.offset = offset;
    if ((nbytes>4096) | (nbytes<0)) {
        return -1;
    }
    message.file_s.nbtyes = nbytes;
    memcpy(&message.info.buffer, buffer, nbytes);

    UDP_Write(fd, &server_addr, (char *) &message, sizeof(message_t));

    // TODO: implement timeout ?
    short int sock = socket(AF_INET, SOCK_STREAM, 0);
    fd_set fdset;
    struct timeval timeout;
    timeout.tv_sec  = 5;
    timeout.tv_usec = 0;
    FD_SET(sock, &fdset);
    while(select(sock+1, NULL, &fdset, NULL, &timeout) <= 0) {
        UDP_Write(fd, &server_addr, (char *) &message, sizeof(message_t));
    }

    message_t response; 
    struct sockaddr_in ret_addr; 
    UDP_Read(fd, &ret_addr, (char *) &response, sizeof(message_t));

    return response.rc;
}

/*
    Reads nbytes of data (max size 4096 bytes) specified by the byte offset offset into the buffer 
    from file specified by inum. The routine should work for either a file or directory; 
    directories should return data in the format specified by MFS_DirEnt_t.
    Success: 0, failure: -1.
    Failure modes: invalid inum, invalid offset, invalid nbytes.
*/
int MFS_Read(int inum, char *buffer, int offset, int nbytes) {
    message_t message;
    message.mtype = MFS_READ;
    message.file_s.inum = inum;
    if (offset<0){
        return -1;
    }
    if(offset/MFS_BLOCK_SIZE >= 30) {
        return -1;
    }
    message.file_s.offset = offset;
    if ((nbytes>4096) | (nbytes<0)) {
        return -1;
    }
    message.file_s.nbtyes = nbytes;
    // fill buffer in the response

    UDP_Write(fd, &server_addr, (char *) &message, sizeof(message_t));

    // TODO: implement timeout ?
    short int sock = socket(AF_INET, SOCK_STREAM, 0);
    fd_set fdset;
    struct timeval timeout;
    timeout.tv_sec  = 5;
    timeout.tv_usec = 0;
    FD_SET(sock, &fdset);
    while(select(sock+1, NULL, &fdset, NULL, &timeout) <= 0) {
        UDP_Write(fd, &server_addr, (char *) &message, sizeof(message_t));
    }

    message_t response; 
    struct sockaddr_in ret_addr; 
    UDP_Read(fd, &ret_addr, (char *) &response, sizeof(message_t));
    // fill the buffer argument
    memcpy(buffer, response.info.buffer, nbytes);

    return response.rc;
}

/*
    Makes a file (type == MFS_REGULAR_FILE) or directory (type == MFS_DIRECTORY) 
    in the parent directory specified by pinum of name name. 
    Returns 0 on success, -1 on failure. 
    Failure modes: pinum does not exist, or name is too long. If name already exists, return success.
*/
int MFS_Creat(int pinum, int type, char *name) {
    // name too long
    // name should be 27 chars max and /0 terminated
    if (strlen(name) > 27) {
        return -1;
    }

    message_t message;
    message.mtype = MFS_CRET;
    message.file_s.inum = pinum;
    message.file_s.ftype = type; // file type
    strcpy(message.info.name, name);

    UDP_Write(fd, &server_addr, (char *) &message, sizeof(message_t));

    // TODO: implement timeout ?
    short int sock = socket(AF_INET, SOCK_STREAM, 0);
    fd_set fdset;
    struct timeval timeout;
    timeout.tv_sec  = 5;
    timeout.tv_usec = 0;
    FD_SET(sock, &fdset);
    while(select(sock+1, NULL, &fdset, NULL, &timeout) <= 0) {
        UDP_Write(fd, &server_addr, (char *) &message, sizeof(message_t));
    }

    message_t response; 
    struct sockaddr_in ret_addr; 
    UDP_Read(fd, &ret_addr, (char *) &response, sizeof(message_t));

    return response.rc;
}

/*
    Removes the file or directory name from the directory specified by pinum. 
    0 on success, -1 on failure. 
    Failure modes: pinum does not exist, directory is NOT empty. 
    Note that the name not existing is NOT a failure by our definition (think about why this might be).
*/
int MFS_Unlink(int pinum, char *name) {
    // name should be 27 chars max and /0 terminated
    if (strlen(name) > 27) {
        return -1;
    }
    message_t message;
    message.mtype = MFS_UNLINK;
    message.file_s.inum = pinum;
    strcpy(message.info.name, name);

    UDP_Write(fd, &server_addr, (char *) &message, sizeof(message_t));

    // TODO: implement timeout ?
    short int sock = socket(AF_INET, SOCK_STREAM, 0);
    fd_set fdset;
    struct timeval timeout;
    timeout.tv_sec  = 5;
    timeout.tv_usec = 0;
    FD_SET(sock, &fdset);
    while(select(sock+1, NULL, &fdset, NULL, &timeout) <= 0) {
        UDP_Write(fd, &server_addr, (char *) &message, sizeof(message_t));
    }

    message_t response; 
    struct sockaddr_in ret_addr; 
    UDP_Read(fd, &ret_addr, (char *) &response, sizeof(message_t));

    return response.rc;
}

/*
    Just tells the server to force all of its data structures to disk and shutdown by calling exit(0). 
    This interface will mostly be used for testing purposes.
*/
int MFS_Shutdown() {
    message_t message;
    message.mtype = MFS_SHUTDOWN;

    UDP_Write(fd, &server_addr, (char *) &message, sizeof(message_t));
    UDP_Close(fd);
    return 0;
}
