// fichier : clibeuip.c 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

int main(int argc, char *argv[]) {
    int sid;
    struct sockaddr_in Sock;
    char buf[512];

    if (argc < 2) {
        fprintf(stderr, "utilisation : %s [liste | mp pseudo msg | all msg | leave pseudo]\n", argv[0]);
        return 1;
    }

    if ((sid = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) return 2;

    bzero(&Sock, sizeof(Sock));
    Sock.sin_family = AF_INET;
    Sock.sin_port = htons(9998);
    Sock.sin_addr.s_addr = inet_addr("127.0.0.1");

    bzero(buf, sizeof(buf));

    if (strcmp(argv[1], "liste") == 0) {
        sprintf(buf, "3BEUIP");
        sendto(sid, buf, 6, 0, (struct sockaddr *)&Sock, sizeof(Sock));
    } 
    // code 4: message prive avec insertion manuelle du separateur nul
    else if (strcmp(argv[1], "mp") == 0 && argc == 4) {
        sprintf(buf, "4BEUIP%s", argv[2]);
        int offset = 6 + strlen(argv[2]) + 1;
        strcpy(buf + offset, argv[3]); // ecriture apres le caractere nul
        sendto(sid, buf, offset + strlen(argv[3]), 0, (struct sockaddr *)&Sock, sizeof(Sock));
    }

    return 0;
}