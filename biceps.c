// fichier : biceps.c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>
#include <sys/select.h>
#include <ifaddrs.h>
#include "gescom.h" 

#define PORT 9998
#define MAX_CLIENTS 255

struct client {
    struct in_addr ip;
    char pseudo[256];
};

// variables globales partagees (memoire commune)
struct client table[MAX_CLIENTS];
int nb_clients = 0;
pthread_mutex_t mutex_table = PTHREAD_MUTEX_INITIALIZER;
int serveur_actif = 0;
pthread_t thread_udp;
char pseudo_local[256];
char chemin_historique[1024];

// fonction centralisee pour les commandes internes beuip
void commande(char octet1, char *message, char *pseudo) {
    if (!serveur_actif) {
        printf("erreur : le serveur beuip n'est pas demarre.\n");
        return;
    }

    if (octet1 == '3') {
        pthread_mutex_lock(&mutex_table);
        printf("\n--- liste des contacts internes ---\n");
        for (int i = 0; i < nb_clients; i++) {
            printf("- %s (%s)\n", table[i].pseudo, inet_ntoa(table[i].ip));
        }
        printf("-----------------------------------\n\n");
        pthread_mutex_unlock(&mutex_table);
    } 
    else if (octet1 == '4' || octet1 == '5' || octet1 == '0') {
        int sid_out = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sid_out < 0) return;
        
        int opt = 1;
        setsockopt(sid_out, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt));
        
        char msg_out[512];
        struct sockaddr_in Dest;
        bzero(&Dest, sizeof(Dest));
        Dest.sin_family = AF_INET;
        Dest.sin_port = htons(PORT);

        pthread_mutex_lock(&mutex_table);
        
        if (octet1 == '4' && pseudo != NULL && message != NULL) {
            int trouve = 0;
            for (int i = 0; i < nb_clients; i++) {
                if (strcmp(table[i].pseudo, pseudo) == 0) {
                    Dest.sin_addr = table[i].ip;
                    sprintf(msg_out, "9BEUIP%s", message);
                    sendto(sid_out, msg_out, 6 + strlen(message), 0, (struct sockaddr *)&Dest, sizeof(Dest));
                    trouve = 1;
                    break;
                }
            }
            if (!trouve) printf("erreur : pseudo introuvable.\n");
        } 
        else if (octet1 == '5' && message != NULL) {
            sprintf(msg_out, "9BEUIP%s", message);
            for (int i = 0; i < nb_clients; i++) {
                Dest.sin_addr = table[i].ip;
                sendto(sid_out, msg_out, 6 + strlen(message), 0, (struct sockaddr *)&Dest, sizeof(Dest));
            }
        }
        else if (octet1 == '0') {
            sprintf(msg_out, "0BEUIP%s", pseudo_local);
            for (int i = 0; i < nb_clients; i++) {
                Dest.sin_addr = table[i].ip;
                sendto(sid_out, msg_out, strlen(msg_out), 0, (struct sockaddr *)&Dest, sizeof(Dest));
            }
        }
        
        pthread_mutex_unlock(&mutex_table);
        close(sid_out);
    }
}

// thread du serveur udp avec detection reseau automatique (etape 2.1)
void *serveur_udp(void *p) {
    int sid, n;
    struct sockaddr_in SockConf, Sock;
    socklen_t ls;
    char buf[512];
    char msg_out[512];
    fd_set readfds;
    struct timeval tv;
    struct ifaddrs *ifaddr, *ifa;
    char host[NI_MAXHOST];

    if ((sid = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) pthread_exit(NULL);

    int opt = 1;
    // ajout de so_reuseport pour permettre les tests locaux avec plusieurs terminaux
    setsockopt(sid, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
    setsockopt(sid, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    bzero(&SockConf, sizeof(SockConf));
    SockConf.sin_family = AF_INET;
    SockConf.sin_port = htons(PORT);
    SockConf.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sid, (struct sockaddr *)&SockConf, sizeof(SockConf)) == -1) {
        perror("erreur bind serveur udp");
        close(sid);
        pthread_exit(NULL);
    }

    setsockopt(sid, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt));

    printf("\n[thread réseau] serveur udp lance sur le port %d.\n", PORT);

    sprintf(msg_out, "1BEUIP%s", pseudo_local);

    // recuperation et parcours des interfaces reseau
    if (getifaddrs(&ifaddr) != -1) {
        for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr == NULL || ifa->ifa_broadaddr == NULL) continue;

            if (ifa->ifa_addr->sa_family == AF_INET) {
                int s = getnameinfo(ifa->ifa_broadaddr, sizeof(struct sockaddr_in), host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
                
                if (s == 0 && strcmp(host, "127.0.0.1") != 0 && strcmp(host, "255.0.0.0") != 0) {
                    struct sockaddr_in *bcast_addr = (struct sockaddr_in *)ifa->ifa_broadaddr;
                    bcast_addr->sin_port = htons(PORT);
                    sendto(sid, msg_out, strlen(msg_out), 0, (struct sockaddr *)bcast_addr, sizeof(struct sockaddr_in));
                    printf("[thread réseau] annonce broadcast envoyee sur %s (%s)\n", ifa->ifa_name, host);
                }
            }
        }
        freeifaddrs(ifaddr);
    }

    // boucle principale du serveur
    while (serveur_actif) {
        FD_ZERO(&readfds);
        FD_SET(sid, &readfds);
        tv.tv_sec = 0;
        tv.tv_usec = 200000;

        if (select(sid + 1, &readfds, NULL, NULL, &tv) > 0 && FD_ISSET(sid, &readfds)) {
            ls = sizeof(Sock);
            n = recvfrom(sid, buf, sizeof(buf) - 1, 0, (struct sockaddr *)&Sock, &ls);
            if (n > 0) {
                buf[n] = '\0';
                if (n >= 6 && strncmp(buf + 1, "BEUIP", 5) == 0) {
                    char code = buf[0];
                    char *donnees = buf + 6;
                    
                    if (code == '1' || code == '2') {
                        pthread_mutex_lock(&mutex_table);
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
                            printf("\n[thread réseau] nouveau contact : %s (%s)\n", donnees, inet_ntoa(Sock.sin_addr));
                        }
                        pthread_mutex_unlock(&mutex_table);
                        
                        if (code == '1') {
                            char msg_rep[512];
                            sprintf(msg_rep, "2BEUIP%s", pseudo_local);
                            sendto(sid, msg_rep, strlen(msg_rep), 0, (struct sockaddr *)&Sock, ls);
                        }
                    } else if (code == '9') {
                        pthread_mutex_lock(&mutex_table);
                        int trouve = 0;
                        for (int i = 0; i < nb_clients; i++) {
                            if (table[i].ip.s_addr == Sock.sin_addr.s_addr) {
                                printf("\n[message de %s] : %s\n", table[i].pseudo, donnees);
                                trouve = 1;
                                break;
                            }
                        }
                        pthread_mutex_unlock(&mutex_table);
                        if (!trouve) printf("\n[message d'inconnu] : %s\n", donnees);
                    } else if (code == '0') {
                        pthread_mutex_lock(&mutex_table);
                        for (int i = 0; i < nb_clients; i++) {
                            if (table[i].ip.s_addr == Sock.sin_addr.s_addr) {
                                printf("\n[thread réseau] %s a quitte le reseau.\n", table[i].pseudo);
                                for (int j = i; j < nb_clients - 1; j++) {
                                    table[j] = table[j+1];
                                }
                                nb_clients--;
                                break;
                            }
                        }
                        pthread_mutex_unlock(&mutex_table);
                    } else {
                        printf("\n[thread réseau] alerte : tentative de piratage bloquee (code %c).\n", code);
                    }
                }
            }
        }
    }
    close(sid);
    pthread_exit(NULL);
}

// parsage de la commande interne beuip
int cmd_beuip(int n, char *p[]) {
    if (n >= 3 && strcmp(p[1], "start") == 0) {
        if (serveur_actif) {
            printf("erreur : serveur deja actif.\n");
            return 1;
        }
        strncpy(pseudo_local, p[2], sizeof(pseudo_local)-1);
        serveur_actif = 1;
        pthread_create(&thread_udp, NULL, serveur_udp, NULL);
    } 
    else if (n == 2 && strcmp(p[1], "stop") == 0) {
        if (serveur_actif) {
            commande('0', NULL, NULL); 
            serveur_actif = 0;         
            pthread_join(thread_udp, NULL); 
            printf("serveur beuip arrete.\n");
        } else {
            printf("aucun serveur actif.\n");
        }
    }
    else if (n == 2 && strcmp(p[1], "liste") == 0) {
        commande('3', NULL, NULL);
    }
    else if (n == 4 && strcmp(p[1], "mp") == 0) {
        commande('4', p[3], p[2]);
    }
    else if (n == 3 && strcmp(p[1], "all") == 0) {
        commande('5', p[2], NULL);
    }
    else {
        printf("utilisation : beuip [start pseudo | stop | liste | mp pseudo msg | all msg]\n");
    }
    return 0;
}

int est_ligne_utile(const char *ligne) {
    while (*ligne != '\0') {
        if (*ligne != ' ' && *ligne != '\t') return 1;
        ligne++;
    }
    return 0;
}

int Sortie(int N, char *P[]) {
    if (serveur_actif) {
        commande('0', NULL, NULL);
        serveur_actif = 0;
        pthread_join(thread_udp, NULL);
    }
    libererMots();
    write_history(chemin_historique);
    printf("sortie correcte du programme biceps.\n");
    exit(0);
    return 0; 
}

int change_dir(int n, char *p[]) {
    if (n > 1) chdir(p[1]);
    else chdir(getenv("HOME"));
    return 0;
}

int print_wd(int n, char *p[]) {
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) != NULL) printf("%s\n", cwd);
    return 0;
}

void majComInt(void) {
    ajouteCom("exit", Sortie);
    ajouteCom("cd", change_dir);
    ajouteCom("pwd", print_wd);
    ajouteCom("beuip", cmd_beuip);
}

int main(void) {
    char nom_machine[256], *nom_utilisateur, prompt[512], *ligne_saisie, caractere_fin;

    signal(SIGINT, SIG_IGN);
    majComInt();

    if (gethostname(nom_machine, sizeof(nom_machine)) != 0) strcpy(nom_machine, "inconnu");
    nom_utilisateur = getenv("USER");
    if (nom_utilisateur == NULL) nom_utilisateur = "inconnu";
    caractere_fin = (geteuid() == 0) ? '#' : '$';

    snprintf(chemin_historique, sizeof(chemin_historique), "%s/.biceps_history", getenv("HOME"));
    read_history(chemin_historique);

    while (1) {
        char cwd[1024], chemin_affiche[1024] = "";
        if (getcwd(cwd, sizeof(cwd)) != NULL) snprintf(chemin_affiche, sizeof(chemin_affiche), " [%s]", cwd);
        snprintf(prompt, sizeof(prompt), "%s@%s%s%c ", nom_utilisateur, nom_machine, chemin_affiche, caractere_fin);

        ligne_saisie = readline(prompt);
        if (ligne_saisie == NULL) {
            if (serveur_actif) {
                commande('0', NULL, NULL);
                serveur_actif = 0;
                pthread_join(thread_udp, NULL);
            }
            printf("\nsortie correcte.\n");
            write_history(chemin_historique);
            break;
        }

        if (est_ligne_utile(ligne_saisie)) {
            add_history(ligne_saisie);
            traiterSequence(ligne_saisie);
        }
        free(ligne_saisie);
    }
    libererMots();
    return 0;
}