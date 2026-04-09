// fichier : servbeuip.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#include <unistd.h>

#define PORT 9998
#define MAX_CLIENTS 255

// variables globales pour le gestionnaire de signal
int sid_global;
struct sockaddr_in bcast_global;
char pseudo_global[256];

struct client {
    struct in_addr ip;
    char pseudo[256];
};

struct client table[MAX_CLIENTS];
int nb_clients = 0;

// fonction qui se declenche a la reception du signal
void handle_stop(int sig) {
    char msg_out[512];
    sprintf(msg_out, "0BEUIP%s", pseudo_global);
    sendto(sid_global, msg_out, strlen(msg_out), 0, (struct sockaddr *)&bcast_global, sizeof(bcast_global));
    printf("\nsignal d'arret recu. deconnexion de %s et fermeture du processus.\n", pseudo_global);
    close(sid_global);
    exit(0);
}

int main(int argc, char *argv[]) {
    int sid, n;
    struct sockaddr_in SockConf, Sock, Bcast;
    socklen_t ls;
    char buf[512];
    char msg_out[512];

    if (argc != 2) {
        fprintf(stderr, "utilisation : %s pseudo\n", argv[0]);
        return 1;
    }

    if ((sid = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) return 2;

    bzero(&SockConf, sizeof(SockConf));
    SockConf.sin_family = AF_INET;
    SockConf.sin_port = htons(PORT);
    SockConf.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sid, (struct sockaddr *)&SockConf, sizeof(SockConf)) == -1) return 3;

    int opt = 1;
    if (setsockopt(sid, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt)) < 0) return 4;

    bzero(&Bcast, sizeof(Bcast));
    Bcast.sin_family = AF_INET;
    Bcast.sin_port = htons(PORT);
    Bcast.sin_addr.s_addr = inet_addr("192.168.88.255");

    // branchement du signal et assignation des globales
    sid_global = sid;
    bcast_global = Bcast;
    strncpy(pseudo_global, argv[1], 255);
    signal(SIGUSR1, handle_stop);

    sprintf(msg_out, "1BEUIP%s", argv[1]);
    sendto(sid, msg_out, strlen(msg_out), 0, (struct sockaddr *)&Bcast, sizeof(Bcast));
    printf("serveur %s demarre et annonce envoyee.\n", argv[1]);

    for (;;) {
        ls = sizeof(Sock);
        if ((n = recvfrom(sid, buf, sizeof(buf) - 1, 0, (struct sockaddr *)&Sock, &ls)) == -1) continue;
        buf[n] = '\0';

        if (n >= 6 && strncmp(buf + 1, "BEUIP", 5) == 0) {
            char code = buf[0];
            char *donnees = buf + 6;

            if ((code == '3' || code == '4' || code == '5') && Sock.sin_addr.s_addr != inet_addr("127.0.0.1")) {
                continue;
            }

            if (code == '1' || code == '2') {
                int existe = 0;
                for (int i = 0; i < nb_clients; i++) {
                    if (table[i].ip.s_addr == Sock.sin_addr.s_addr) {
                        existe = 1;
                        break;
                    }
                }
                if (!existe && nb_clients < MAX_CLIENTS) {
                    table[nb_clients].ip = Sock.sin_addr;
                    strncpy(table[nb_clients].pseudo, donnees, 255);
                    nb_clients++;
                    printf("nouveau contact ajoute : %s (%s)\n", donnees, inet_ntoa(Sock.sin_addr));
                }
                if (code == '1') {
                    sprintf(msg_out, "2BEUIP%s", argv[1]);
                    sendto(sid, msg_out, strlen(msg_out), 0, (struct sockaddr *)&Sock, ls);
                }
            } 
            else if (code == '3') {
                printf("\n--- liste des contacts ---\n");
                for (int i = 0; i < nb_clients; i++) {
                    printf("- %s (%s)\n", table[i].pseudo, inet_ntoa(table[i].ip));
                }
                printf("--------------------------\n\n");
            }
            else if (code == '4') {
                char *pseudo_dest = donnees;
                char *msg_txt = donnees + strlen(pseudo_dest) + 1;
                int trouve = 0;
                for (int i = 0; i < nb_clients; i++) {
                    if (strcmp(table[i].pseudo, pseudo_dest) == 0) {
                        struct sockaddr_in Dest;
                        bzero(&Dest, sizeof(Dest));
                        Dest.sin_family = AF_INET;
                        Dest.sin_port = htons(PORT);
                        Dest.sin_addr = table[i].ip;
                        
                        sprintf(msg_out, "9BEUIP%s", msg_txt);
                        sendto(sid, msg_out, 6 + strlen(msg_txt), 0, (struct sockaddr *)&Dest, sizeof(Dest));
                        trouve = 1;
                        break;
                    }
                }
                if (!trouve) printf("erreur : le pseudo '%s' est introuvable.\n", pseudo_dest);
            }
            else if (code == '5') {
                sprintf(msg_out, "9BEUIP%s", donnees);
                for (int i = 0; i < nb_clients; i++) {
                    struct sockaddr_in Dest;
                    bzero(&Dest, sizeof(Dest));
                    Dest.sin_family = AF_INET;
                    Dest.sin_port = htons(PORT);
                    Dest.sin_addr = table[i].ip;
                    sendto(sid, msg_out, 6 + strlen(donnees), 0, (struct sockaddr *)&Dest, sizeof(Dest));
                }
            }
            else if (code == '9') {
                int trouve = 0;
                for (int i = 0; i < nb_clients; i++) {
                    if (table[i].ip.s_addr == Sock.sin_addr.s_addr) {
                        printf("\nmessage de %s : %s\n", table[i].pseudo, donnees);
                        trouve = 1;
                        break;
                    }
                }
                if (!trouve) printf("\nerreur : message d'une ip inconnue (%s)\n", inet_ntoa(Sock.sin_addr));
            }
            else if (code == '0') {
                for (int i = 0; i < nb_clients; i++) {
                    if (table[i].ip.s_addr == Sock.sin_addr.s_addr) {
                        printf("le contact %s a quitte le reseau.\n", table[i].pseudo);
                        for (int j = i; j < nb_clients - 1; j++) {
                            table[j] = table[j+1];
                        }
                        nb_clients--;
                        break;
                    }
                }
            }
        }
    }
    return 0;
}