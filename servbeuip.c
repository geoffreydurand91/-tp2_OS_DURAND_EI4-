#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 9998
#define MAX_CLIENTS 255

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
    char buf[1024];
    char msg_out[1024];

    if (argc != 2) {
        fprintf(stderr, "utilisation : %s pseudo\n", argv[1]);
        return 1;
    }

    if ((sid = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) return 2;

    int opt = 1;
    setsockopt(sid, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt));

    bzero(&SockConf, sizeof(SockConf));
    SockConf.sin_family = AF_INET;
    SockConf.sin_port = htons(PORT);
    SockConf.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sid, (struct sockaddr *)&SockConf, sizeof(SockConf)) < 0) return 3;

    bzero(&Bcast, sizeof(Bcast));
    Bcast.sin_family = AF_INET;
    Bcast.sin_port = htons(PORT);
    Bcast.sin_addr.s_addr = htonl(INADDR_BROADCAST);

    sprintf(msg_out, "1BEUIP%s", argv[1]);
    sendto(sid, msg_out, strlen(msg_out), 0, (struct sockaddr *)&Bcast, sizeof(Bcast));

    for (;;) {
        ls = sizeof(Sock);
        if ((n = recvfrom(sid, buf, sizeof(buf) - 1, 0, (struct sockaddr *)&Sock, &ls)) == -1) continue;
        buf[n] = '\0';

        if (n < 6 || strncmp(buf + 1, "BEUIP", 5) != 0) continue;

        char code = buf[0];
        // identification si le message vient de l'hote local
        int is_local = (Sock.sin_addr.s_addr == inet_addr("127.0.0.1"));

        // codes 1 & 2 : mise a jour de la table (uniquement via reseau)
        if (!is_local && (code == '1' || code == '2')) {
            int existe = 0;
            for (int i = 0; i < nb_clients; i++) {
                if (table[i].ip.s_addr == Sock.sin_addr.s_addr) {
                    existe = 1;
                    break;
                }
            }
            if (!existe && nb_clients < MAX_CLIENTS) {
                table[nb_clients].ip = Sock.sin_addr;
                strncpy(table[nb_clients].pseudo, buf + 6, 255);
                nb_clients++;
                printf("contact ajoute : %s\n", buf + 6);
            }
            if (code == '1') {
                sprintf(msg_out, "2BEUIP%s", argv[1]);
                sendto(sid, msg_out, strlen(msg_out), 0, (struct sockaddr *)&Sock, sizeof(Sock));
            }
        }

        // code 3 : demande de liste par le client local
        else if (is_local && code == '3') {
            printf("--- liste des pairs connectes ---\n");
            for (int i = 0; i < nb_clients; i++) {
                printf("[%d] %s -> %s\n", i, table[i].pseudo, inet_ntoa(table[i].ip));
            }
        }

        // code 4 : client local demande d'envoyer un message a un pair
        else if (is_local && code == '4') {
            char *pseudo_dest = buf + 6;
            // le message commence juste apres le '\0' du pseudo
            char *message = pseudo_dest + strlen(pseudo_dest) + 1;
            
            int cible = -1;
            for (int i = 0; i < nb_clients; i++) {
                if (strcmp(table[i].pseudo, pseudo_dest) == 0) {
                    cible = i;
                    break;
                }
            }

            if (cible != -1) {
                sprintf(msg_out, "9BEUIP%s", message);
                struct sockaddr_in Dest;
                bzero(&Dest, sizeof(Dest));
                Dest.sin_family = AF_INET;
                Dest.sin_port = htons(PORT);
                Dest.sin_addr = table[cible].ip;
                sendto(sid, msg_out, 6 + strlen(message), 0, (struct sockaddr *)&Dest, sizeof(Dest));
            } else {
                printf("erreur : destinataire %s inconnu\n", pseudo_dest);
            }
        }

        // code 9 : reception d'un message venant d'un pair distant
        else if (!is_local && code == '9') {
            printf("message de %s : %s\n", inet_ntoa(Sock.sin_addr), buf + 6);
        }
    }
    return 0;
}