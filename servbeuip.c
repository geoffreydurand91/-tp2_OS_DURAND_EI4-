//servbeuip.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define PORT 9998
#define MAX_CLIENTS 255

// structure pour la table des clients
struct client {
    struct in_addr ip;
    char pseudo[256];
};

struct client table[MAX_CLIENTS];
int nb_clients = 0;

int main(int argc, char *argv[]) {
    int sid, n;
    struct sockaddr_in SockConf, Sock, Bcast;
    socklen_t ls;
    char buf[512];
    char msg_out[512];

    // verification du parametre pseudo
    if (argc != 2) {
        fprintf(stderr, "utilisation : %s pseudo\n", argv[0]);
        return 1;
    }

    if ((sid = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) return 2;

    // configuration pour le bind sur le port 9998
    bzero(&SockConf, sizeof(SockConf));
    SockConf.sin_family = AF_INET;
    SockConf.sin_port = htons(PORT);
    SockConf.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sid, (struct sockaddr *)&SockConf, sizeof(SockConf)) == -1) return 3;

    // activation du droit d'envoyer en broadcast
    int opt = 1;
    if (setsockopt(sid, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt)) < 0) return 4;

    // preparation et envoi du message broadcast initial
    bzero(&Bcast, sizeof(Bcast));
    Bcast.sin_family = AF_INET;
    Bcast.sin_port = htons(PORT);
    Bcast.sin_addr.s_addr = inet_addr("192.168.88.255");
    
    // format du message : code 1 + beuip + pseudo
    sprintf(msg_out, "1BEUIP%s", argv[1]);
    sendto(sid, msg_out, strlen(msg_out), 0, (struct sockaddr *)&Bcast, sizeof(Bcast));
    printf("serveur %s demarre et annonce envoyee.\n", argv[1]);

    for (;;) {
        ls = sizeof(Sock);
        if ((n = recvfrom(sid, buf, sizeof(buf) - 1, 0, (struct sockaddr *)&Sock, &ls)) == -1) continue;
        buf[n] = '\0';

        // verification de la taille minimale et de la chaine beuip
        if (n >= 6 && strncmp(buf + 1, "BEUIP", 5) == 0) {
            char code = buf[0];
            char *pseudo_recu = buf + 6;
            
            // verification des doublons avant ajout a la table
            int existe = 0;
            for (int i = 0; i < nb_clients; i++) {
                if (table[i].ip.s_addr == Sock.sin_addr.s_addr) {
                    existe = 1;
                    break;
                }
            }
            
            // ajout si nouveau et table non pleine
            if (!existe && nb_clients < MAX_CLIENTS) {
                table[nb_clients].ip = Sock.sin_addr;
                strncpy(table[nb_clients].pseudo, pseudo_recu, 255);
                nb_clients++;
                printf("nouveau contact ajoute : %s (%s)\n", pseudo_recu, inet_ntoa(Sock.sin_addr));
            }

            // envoi de l'accuse de reception si code 1
            if (code == '1') {
                sprintf(msg_out, "2BEUIP%s", argv[1]);
                sendto(sid, msg_out, strlen(msg_out), 0, (struct sockaddr *)&Sock, ls);
            }
        }
    }
    return 0;
}