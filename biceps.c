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
#include "gescom.h" 

#define PORT 9998
#define MAX_CLIENTS 255

// structures et variables globales partagees pour le reseau beuip
struct client {
    struct in_addr ip;
    char pseudo[256];
};

struct client table[MAX_CLIENTS];
int nb_clients = 0;

// protection des variables globales pour le multi-threading
pthread_mutex_t mutex_table = PTHREAD_MUTEX_INITIALIZER;
int serveur_actif = 0;
pthread_t thread_udp;
char pseudo_local[256];
char chemin_historique[1024];

// fonction executee par le thread serveur udp
void *serveur_udp(void *p) {
    char *pseudo = (char *)p;
    int sid, n;
    struct sockaddr_in SockConf, Sock, Bcast;
    socklen_t ls;
    char buf[512];
    char msg_out[512];
    fd_set readfds;
    struct timeval tv;

    if ((sid = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
        perror("socket udp");
        pthread_exit(NULL);
    }

    bzero(&SockConf, sizeof(SockConf));
    SockConf.sin_family = AF_INET;
    SockConf.sin_port = htons(PORT);
    SockConf.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sid, (struct sockaddr *)&SockConf, sizeof(SockConf)) == -1) {
        perror("bind udp");
        close(sid);
        pthread_exit(NULL);
    }

    int opt = 1;
    setsockopt(sid, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt));

    bzero(&Bcast, sizeof(Bcast));
    Bcast.sin_family = AF_INET;
    Bcast.sin_port = htons(PORT);
    // adresse en dur temporaire, sera modifiee a l'etape 2
    Bcast.sin_addr.s_addr = inet_addr("192.168.88.255");
    
    sprintf(msg_out, "1BEUIP%s", pseudo);
    // on ignore l'erreur si le mac n'est pas connecte au bon reseau pour le test
    sendto(sid, msg_out, strlen(msg_out), 0, (struct sockaddr *)&Bcast, sizeof(Bcast));
    printf("\n[thread réseau] serveur udp lance et annonce envoyee.\n");

    while (serveur_actif) {
        FD_ZERO(&readfds);
        FD_SET(sid, &readfds);
        tv.tv_sec = 0;
        tv.tv_usec = 200000; // timeout de 200ms pour verifier serveur_actif regulierement

        int retval = select(sid + 1, &readfds, NULL, NULL, &tv);
        if (retval > 0 && FD_ISSET(sid, &readfds)) {
            ls = sizeof(Sock);
            n = recvfrom(sid, buf, sizeof(buf) - 1, 0, (struct sockaddr *)&Sock, &ls);
            if (n > 0) {
                buf[n] = '\0';
                // verification stricte : presence du beuip
                if (n >= 6 && strncmp(buf + 1, "BEUIP", 5) == 0) {
                    char code = buf[0];
                    char *donnees = buf + 6;
                    
                    // le serveur udp ne gere plus que les codes 0, 1, 2 et 9 (sujet 1.2)
                    if (code == '1' || code == '2') {
                        // verrouillage avant modification de la table
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
                            sprintf(msg_out, "2BEUIP%s", pseudo);
                            sendto(sid, msg_out, strlen(msg_out), 0, (struct sockaddr *)&Sock, ls);
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
                        if (!trouve) printf("\n[message d'inconnu (%s)] : %s\n", inet_ntoa(Sock.sin_addr), donnees);
                    } else if (code == '0') {
                        pthread_mutex_lock(&mutex_table);
                        for (int i = 0; i < nb_clients; i++) {
                            if (table[i].ip.s_addr == Sock.sin_addr.s_addr) {
                                printf("\n[thread réseau] le contact %s a quitte.\n", table[i].pseudo);
                                for (int j = i; j < nb_clients - 1; j++) {
                                    table[j] = table[j+1];
                                }
                                nb_clients--;
                                break;
                            }
                        }
                        pthread_mutex_unlock(&mutex_table);
                    } else {
                        printf("\n[thread réseau] erreur : tentative de piratage avec code %c rejete.\n", code);
                    }
                }
            }
        }
    }
    close(sid);
    pthread_exit(NULL);
}

// fonction implementant la commande beuip start
int cmd_beuip(int n, char *p[]) {
    if (n >= 3 && strcmp(p[1], "start") == 0) {
        if (serveur_actif) {
            printf("erreur : le serveur udp tourne deja.\n");
            return 1;
        }
        strncpy(pseudo_local, p[2], sizeof(pseudo_local)-1);
        serveur_actif = 1; // drapeau pour la boucle du thread
        
        // creation et lancement du thread posix
        if (pthread_create(&thread_udp, NULL, serveur_udp, pseudo_local) != 0) {
            perror("erreur lancement thread");
            serveur_actif = 0;
            return 2;
        }
    } else {
        printf("utilisation : beuip start pseudo\n");
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
    libererMots();
    write_history(chemin_historique);
    printf("sortie correcte du programme biceps.\n");
    exit(0);
    return 0; 
}

int change_dir(int n, char *p[]) {
    if (n > 1) {
        if (chdir(p[1]) != 0) perror("erreur cd");
    } else {
        chdir(getenv("HOME"));
    }
    return 0;
}

int print_wd(int n, char *p[]) {
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        printf("%s\n", cwd);
    } else {
        perror("erreur pwd");
    }
    return 0;
}

int version(int n, char *p[]) {
    printf("biceps version 3.0\n");
    return 0;
}

void majComInt(void) {
    ajouteCom("exit", Sortie);
    ajouteCom("cd", change_dir);
    ajouteCom("pwd", print_wd);
    ajouteCom("vers", version);
    // ajout de la nouvelle commande beuip
    ajouteCom("beuip", cmd_beuip);
}

int main(void) {
    char nom_machine[256];
    char *nom_utilisateur;
    char prompt[512];
    char *ligne_saisie;
    char caractere_fin;

    signal(SIGINT, SIG_IGN);
    majComInt();

    if (gethostname(nom_machine, sizeof(nom_machine)) != 0) strcpy(nom_machine, "inconnu");
    nom_utilisateur = getenv("USER");
    if (nom_utilisateur == NULL) nom_utilisateur = "inconnu";
    caractere_fin = (geteuid() == 0) ? '#' : '$';

    snprintf(chemin_historique, sizeof(chemin_historique), "%s/.biceps_history", getenv("HOME"));
    read_history(chemin_historique);

    while (1) {
        char cwd[1024];
        char chemin_affiche[1024] = "";
        
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            snprintf(chemin_affiche, sizeof(chemin_affiche), " [%s]", cwd);
        }
        snprintf(prompt, sizeof(prompt), "%s@%s%s%c ", nom_utilisateur, nom_machine, chemin_affiche, caractere_fin);

        ligne_saisie = readline(prompt);
        if (ligne_saisie == NULL) {
            printf("\nsortie correcte du programme biceps.\n");
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