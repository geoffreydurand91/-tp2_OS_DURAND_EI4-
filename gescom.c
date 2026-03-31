#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h> 
#include "gescom.h"

// on definit la limite de commandes internes que le tableau peut stocker
#define NBMAXC 10

// structure associant une chaine de caracteres a un pointeur de fonction
typedef struct {
    char *nom;
    int (*fonction)(int N, char *P[]);
} CommandeInterne;

// variables globales a ce fichier pour stocker l'etat de la commande en cours
static char **Mots = NULL;
static int NMots = 0;
CommandeInterne TabComInt[NBMAXC];
int NbComInt = 0;

// fonction dediee a la liberation propre de la memoire dynamique allouee
void libererMots(void) {
    if (Mots != NULL) {
        // on parcourt chaque mot pour liberer sa memoire specifique
        for (int i = 0; i < NMots; i++) free(Mots[i]);
        // on libere ensuite le tableau de pointeurs lui-meme
        free(Mots);
        // on reinitialise les compteurs pour eviter les problemes memoire
        Mots = NULL;
        NMots = 0;
    }
}

// analyse et decoupage d'une chaine de caracteres en tableau d'arguments par les espaces
int analyseCom(char *b) {
    char *delim = " \t\n"; // delimiteurs classiques du shell
    char *token;
    int capacite_max = 20; // allocation arbitraire pour preserver la memoire

    // on nettoie d'abord les residus d'une eventuelle commande precedente
    libererMots();

    // allocation dynamique d'un nouveau tableau de pointeurs
    Mots = malloc(capacite_max * sizeof(char *));
    NMots = 0;

    // boucle de decoupage: strsep remplace les delimiteurs par des caracteres nuls
    while ((token = strsep(&b, delim)) != NULL) {
        // on ignore les chaines vides (cas de plusieurs espaces consecutifs)
        if (*token != '\0') {
            // strdup alloue la memoire et copie la chaine en une seule operation
            Mots[NMots] = strdup(token);
            NMots++;
        }
    }
    
    // la norme posix pour execvp exige que le tableau de pointeurs se termine par null
    Mots[NMots] = NULL; 
    return NMots;
}

// fonction pour intercepter et appliquer les redirections de flux (appelee dans un processus enfant)
void gererRedirections(char **args) {
    int i = 0;
    // on parcourt tous les mots de la commande a la recherche de symboles de redirection
    while (args[i] != NULL) {
        if (strcmp(args[i], "<") == 0) {
            if (args[i+1] != NULL) {
                // ouverture du fichier en lecture seule
                int fd = open(args[i+1], O_RDONLY);
                if (fd >= 0) { 
                    dup2(fd, 0); // la fonction dup2 remplace l'entree standard (0) par notre fichier
                    close(fd); 
                } else { 
                    perror("erreur redirection <"); exit(1); 
                }
                // on decale tout le tableau vers la gauche pour faire disparaitre le symbole et le fichier
                int j = i;
                while (args[j+2] != NULL) { args[j] = args[j+2]; j++; }
                args[j] = NULL;
            }
        } else if (strcmp(args[i], ">") == 0) {
            if (args[i+1] != NULL) {
                // ouverture en ecriture, creation si inexistant, et ecrasement (O_TRUNC) si existant
                int fd = open(args[i+1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd >= 0) { dup2(fd, 1); close(fd); } // on remplace la sortie standard (1)
                else { perror("erreur redirection >"); exit(1); }
                int j = i;
                while (args[j+2] != NULL) { args[j] = args[j+2]; j++; }
                args[j] = NULL;
            }
        } else if (strcmp(args[i], ">>") == 0) {
            if (args[i+1] != NULL) {
                // ouverture en ecriture, mais ajout a la fin du fichier (O_APPEND)
                int fd = open(args[i+1], O_WRONLY | O_CREAT | O_APPEND, 0644);
                if (fd >= 0) { dup2(fd, 1); close(fd); }
                else { perror("erreur redirection >>"); exit(1); }
                int j = i;
                while (args[j+2] != NULL) { args[j] = args[j+2]; j++; }
                args[j] = NULL;
            }
        } else if (strcmp(args[i], "2>") == 0) {
            if (args[i+1] != NULL) {
                // ecrasement mais pour la sortie d'erreur standard (2)
                int fd = open(args[i+1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd >= 0) { dup2(fd, 2); close(fd); }
                else { perror("erreur redirection 2>"); exit(1); }
                int j = i;
                while (args[j+2] != NULL) { args[j] = args[j+2]; j++; }
                args[j] = NULL;
            }
        } else if (strcmp(args[i], "2>>") == 0) {
            if (args[i+1] != NULL) {
                // ajout a la fin pour la sortie d'erreur standard (2)
                int fd = open(args[i+1], O_WRONLY | O_CREAT | O_APPEND, 0644);
                if (fd >= 0) { dup2(fd, 2); close(fd); }
                else { perror("erreur redirection 2>>"); exit(1); }
                int j = i;
                while (args[j+2] != NULL) { args[j] = args[j+2]; j++; }
                args[j] = NULL;
            }
        } else if (strcmp(args[i], "<<") == 0) {
            if (args[i+1] != NULL) {
                // pour le here-document, on utilise un fichier temporaire unique cree par mkstemp
                char temp_file[] = "/tmp/biceps_heredoc_XXXXXX";
                int fd_temp = mkstemp(temp_file);
                if (fd_temp >= 0) {
                    char *delim = args[i+1];
                    char *line = NULL;
                    size_t len = 0;
                    ssize_t read_len;
                    // on bloque et on lit l'entree clavier de l'utilisateur
                    while (1) {
                        write(1, "> ", 2); // on affiche un petit prompt secondaire
                        read_len = getline(&line, &len, stdin);
                        if (read_len == -1) break; // en cas de ctrl+d
                        // on arrete de lire si la ligne tapee correspond exactement au mot cle (delimiteur)
                        if (strncmp(line, delim, strlen(delim)) == 0 && (line[strlen(delim)] == '\n' || line[strlen(delim)] == '\0')) {
                            break;
                        }
                        // on ecrit ce qu'il a tape dans notre fichier temporaire
                        write(fd_temp, line, read_len);
                    }
                    free(line);
                    close(fd_temp);
                    // on re-ouvre ce fichier rempli en lecture seule et on le branche sur l'entree standard (0)
                    fd_temp = open(temp_file, O_RDONLY);
                    dup2(fd_temp, 0);
                    close(fd_temp);
                    // on demande au systeme de detruire ce fichier temporaire des que le programme s'arretera
                    unlink(temp_file); 
                }
                int j = i;
                while (args[j+2] != NULL) { args[j] = args[j+2]; j++; }
                args[j] = NULL;
            }
        } else {
            // si le mot n'est pas un symbole de redirection, on passe simplement au mot suivant
            i++;
        }
    }
}

// enregistrement d'une nouvelle fonction dans le dictionnaire des commandes internes
void ajouteCom(char *nom, int (*fonc)(int, char **)) {
    // verification de la capacite du tableau avant insertion
    if (NbComInt >= NBMAXC) {
        fprintf(stderr, "le tableau des commandes est plein\n");
        exit(1);
    }
    // on duplique le nom pour s'assurer qu'il persiste en memoire
    TabComInt[NbComInt].nom = strdup(nom);
    // on lie le pointeur de la fonction c correspondante
    TabComInt[NbComInt].fonction = fonc;
    // on avance l'index pour la prochaine insertion
    NbComInt++;
}

// recherche sequentielle et execution d'une commande interne
int execComInt(int N, char **P) {
    // protection contre les appels vides
    if (N == 0 || P[0] == NULL) return 0;
    
    // parcours du tableau des commandes connues
    for (int i = 0; i < NbComInt; i++) {
        // si le premier argument correspond a un nom connu
        if (strcmp(P[0], TabComInt[i].nom) == 0) {
            // on execute la fonction pointee en lui passant les arguments
            TabComInt[i].fonction(N, P);
            // on signale le succes de l'execution interne
            return 1;
        }
    }
    // on signale que la commande n'a pas ete trouvee
    return 0;
}

// creation d'un processus enfant simple pour executer une commande seule
int execComExt(char **P) {
    // la fonction fork duplique le processus courant
    pid_t pid = fork();

    // gestion du cas ou le clonage echoue (manque de ressources)
    if (pid < 0) {
        perror("erreur fork");
        return -1;
    } else if (pid == 0) {
        // nous sommes dans le processus fils
        
        // on verifie s'il y a des redirections a mettre en place avant d'executer la commande
        gererRedirections(P);

#ifdef TRACE
        printf("[trace] processus enfant va executer : %s\n", P[0]);
#endif
        // execvp remplace l'image memoire du fils par le programme demande
        execvp(P[0], P);
        // ce code n'est atteint que si execvp a echoue (commande introuvable)
        printf("erreur d'execution : commande introuvable\n");
        // on termine le fils en erreur pour eviter d'avoir deux shells en parallele
        exit(1);
    } else {
        // nous sommes dans le processus pere (notre shell interactif)
        // on met le pere en attente bloquante jusqu'a la fin du fils
        waitpid(pid, NULL, 0);
#ifdef TRACE
        printf("[trace] processus enfant termine\n");
#endif
    }
    return 0;
}

// gestion avancee des tubes (pipes) pour transferer les flux entre commandes
void traiterPipes(char *sequence) {
    char *cmd_brute[20]; // tableau pour stocker chaque bloc de commande separe par un |
    int nb_cmds = 0;
    char *token;
    char *delim = "|";

    // decoupage de la chaine par le caractere pipe
    while ((token = strsep(&sequence, delim)) != NULL) {
        if (strlen(token) > 0) {
            cmd_brute[nb_cmds] = strdup(token);
            nb_cmds++;
        }
    }

    // s'il n'y a qu'une seule commande, pas de pipe, on garde le comportement classique
    if (nb_cmds == 1) {
        analyseCom(cmd_brute[0]);
        if (NMots > 0) {
            if (execComInt(NMots, Mots) == 0) {
                execComExt(Mots);
            }
        }
        free(cmd_brute[0]); // nettoyage immediat
        return;
    }

    // s'il y a plusieurs commandes, on doit construire les embranchements
    int i;
    int in_fd = 0; // descripteur de memoire de lecture (0 = terminal par defaut)
    int fd[2]; // tableau de descripteurs pour un nouveau tube (0: lecture, 1: ecriture)
    pid_t pid;

    // on boucle sur chaque commande isolee de notre tuyauterie
    for (i = 0; i < nb_cmds; i++) {
        // on cree un nouveau tube systeme si l'on n'est pas a la toute derniere etape
        if (i < nb_cmds - 1) {
            if (pipe(fd) < 0) {
                perror("erreur creation tube");
                return; 
            }
        }

        // on clone le processus pour la sous-commande en cours
        pid = fork(); 
        
        if (pid < 0) {
            perror("erreur fork");
        } else if (pid == 0) {
            // --- nous sommes dans le clone (l'enfant) ---

            // si ce n'est pas la premiere etape, on force la lecture depuis le tube precedent
            if (in_fd != 0) {
                dup2(in_fd, 0); // on ecrase l'entree standard (0) par le canal de lecture precedent
                close(in_fd);   // on ferme le canal original pour ne pas saturer le systeme
            }
            // si ce n'est pas la derniere etape, on force l'ecriture vers le nouveau tube
            if (i < nb_cmds - 1) {
                dup2(fd[1], 1); // on ecrase la sortie standard (1) par le canal d'ecriture du tube
                close(fd[1]);   // on ferme le canal original
                close(fd[0]);   // l'enfant ne lit pas ce qu'il est en train d'ecrire
            }

            // maintenant que les flux generaux (pipes) sont devies, on analyse la sous-commande
            analyseCom(cmd_brute[i]);
            if (NMots > 0) {
                
                // on applique les redirections eventuelles specifiques a cette sous-commande (ex: >> fichier)
                gererRedirections(Mots);
                
                // on essaie de lancer la commande comme une commande interne
                if (execComInt(NMots, Mots) == 0) {
                    // sinon on demande au systeme de la lancer
                    execvp(Mots[0], Mots);
                    // si le systeme rend la main, c'est une erreur
                    printf("commande introuvable dans le pipe : %s\n", Mots[0]);
                }
            }
            // on detruit cet enfant independamment du succes ou non pour eviter un fork bomb
            exit(1); 
        } else {
            // --- nous sommes dans le parent (le shell global) ---

            // on ferme le bout de lecture du tube precedent dont on n'a plus besoin
            if (in_fd != 0) {
                close(in_fd);
            }
            // on sauvegarde le bout de lecture du nouveau tube pour l'enfant de la boucle suivante
            if (i < nb_cmds - 1) {
                close(fd[1]); // le parent ne fera jamais d'ecriture la-dedans
                in_fd = fd[0]; // transfert de la responsabilite de lecture au prochain tour
            }
        }
    }

    // le parent doit desormais attendre la fin de vie de tous les processus enfants du pipe
    for (i = 0; i < nb_cmds; i++) {
        wait(NULL);
    }

    // nettoyage massif de la memoire des chaines temporaires
    for (i = 0; i < nb_cmds; i++) {
        free(cmd_brute[i]);
    }
}

// gestion de l'enchainement des commandes separees par des points-virgules
void traiterSequence(char *ligne) {
    char *commande_seule;
    char *delimiteur_sequence = ";";

    // decoupage preliminaire de la ligne brute par blocs de sous-commandes
    while ((commande_seule = strsep(&ligne, delimiteur_sequence)) != NULL) {
        // verification que la sous-commande n'est pas vide
        if (strlen(commande_seule) > 0) {
            // on delegue la tache au gestionnaire de pipes au lieu d'executer directement
            traiterPipes(commande_seule);
        }
    }
}