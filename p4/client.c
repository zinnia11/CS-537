#include <stdio.h>
#include "udp.h"
#include "mfs.h"
#include "ufs.h"

#include "message.h"

// client code
int main(int argc, char *argv[]) {
    char *hostname = argv[1];
    int port = atoi(argv[2]);
    char command = argv[3];

    int rc = MFS_Init(hostname, port);
    if (rc != 0) {
        printf("Could not connect to server\n");
        return -1;
    }

    if (strcmp(command, "stat") == 0) {
        int inum = atoi(argv[4]);
        MFS_Stat_t stat;
        rc = MFS_Stat(inum, &stat);
        if (rc != 0) {
            printf("Error getting stats\n");
        } else {
            printf("filetype %i\n", stat.type);
        }
        return 0;
    }

    struct sockaddr_in addrSnd, addrRcv;

    int sd = UDP_Open(20000);
    rc = UDP_FillSockAddr(&addrSnd, "localhost", 10000);

    message_t m;

    m.mtype = MFS_READ;
    printf("client:: send message %d\n", m.mtype);
    rc = UDP_Write(sd, &addrSnd, (char *) &m, sizeof(message_t));
    if (rc < 0) {
        printf("client:: failed to send\n");
        exit(1);
    }

    // add a timeout where the request is retried
    printf("client:: wait for reply...\n");
    rc = UDP_Read(sd, &addrRcv, (char *) &m, sizeof(message_t));
    printf("client:: got reply [size:%d rc:%d type:%d]\n", rc, m.rc, m.mtype);
    return 0;
}

