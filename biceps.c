#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <readline/readline.h>
#include <readline/history.h>
// inclusion de notre librairie maison pour la gestion des commandes
#include "gescom.h" 

// variable globale pour conserver le chemin du fichier cache
char chemin_historique[1024];

// fonction filtre pour eviter d'enregistrer des chaines vides dans l'historique
int est_ligne_utile(const char *ligne) {
    // on parcourt chaque caractere de la chaine
    while (*ligne != '\0') {
        // des qu'un caractere n'est pas un separateur blanc, la ligne est utile
        if (*ligne != ' ' && *ligne != '\t') return 1;
        ligne++;
    }
    // si on arrive a la fin sans trouver de vrai caractere, elle est inutile
    return 0;
}

// fonction implementant la commande interne exit
int Sortie(int N, char *P[]) {
    // nettoyage memoire via la fonction exposee par notre librairie
    libererMots();
    // sauvegarde de l'historique de la session sur le disque dur
    write_history(chemin_historique);
    printf("sortie correcte du programme biceps.\n");
    // terminaison propre du processus avec un code de succes
    exit(0);
    return 0; 
}

// fonction implementant le changement de repertoire (cd)
int change_dir(int n, char *p[]) {
    // si un dossier de destination est specifie dans les arguments
    if (n > 1) {
        // appel systeme chdir, et affichage d'erreur si le dossier n'existe pas
        if (chdir(p[1]) != 0) perror("erreur cd");
    } else {
        // comportement par defaut: retour au dossier personnel (home)
        chdir(getenv("HOME"));
    }
    return 0;
}

// fonction implementant l'affichage du repertoire courant (pwd)
int print_wd(int n, char *p[]) {
    char cwd[1024];
    // appel systeme getcwd pour recuperer le chemin absolu
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        printf("%s\n", cwd);
    } else {
        perror("erreur pwd");
    }
    return 0;
}

// fonction implementant l'affichage de la version
int version(int n, char *p[]) {
    // affichage statique de la version actuelle du programme
    printf("biceps version 1.0\n");
    return 0;
}

// fonction chargee d'initialiser les commandes specifiques a ce shell
void majComInt(void) {
    // enregistrement de chaque couple (nom de commande, fonction c associee)
    ajouteCom("exit", Sortie);
    ajouteCom("cd", change_dir);
    ajouteCom("pwd", print_wd);
    ajouteCom("vers", version);
}

// point d'entree principal du programme
int main(void) {
    char nom_machine[256];
    char *nom_utilisateur;
    char prompt[512];
    char *ligne_saisie;
    char caractere_fin;

    // desactivation du signal d'interruption materielle (ctrl-c)
    signal(SIGINT, SIG_IGN);
    
    // chargement des commandes dans la memoire de la librairie gescom
    majComInt();

    // resolution du nom d'hote de la machine locale
    if (gethostname(nom_machine, sizeof(nom_machine)) != 0) strcpy(nom_machine, "inconnu");
    
    // resolution de l'identifiant de l'utilisateur connecte
    nom_utilisateur = getenv("USER");
    if (nom_utilisateur == NULL) nom_utilisateur = "inconnu";
    
    // choix du delimiteur final selon les droits (root ou standard)
    caractere_fin = (geteuid() == 0) ? '#' : '$';

    // formatage du chemin absolu vers le fichier d'historique cache
    snprintf(chemin_historique, sizeof(chemin_historique), "%s/.biceps_history", getenv("HOME"));
    // chargement en ram des commandes tapees lors des sessions precedentes
    read_history(chemin_historique);

    // boucle interactive principale (le coeur du shell)
    while (1) {
        char cwd[1024];
        char chemin_affiche[1024] = "";
        
        // recuperation du dossier courant a chaque tour pour la dynamicite du prompt
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            snprintf(chemin_affiche, sizeof(chemin_affiche), " [%s]", cwd);
        }
        
        // assemblage de la chaine complete du prompt d'accueil
        snprintf(prompt, sizeof(prompt), "%s@%s%s%c ", nom_utilisateur, nom_machine, chemin_affiche, caractere_fin);

        // attente bloquante d'une saisie utilisateur via la librairie gnu readline
        ligne_saisie = readline(prompt);
        
        // interception du caractere eof (ctrl-d) pour quitter proprement
        if (ligne_saisie == NULL) {
            printf("\nsortie correcte du programme biceps.\n");
            // derniere sauvegarde de l'historique avant de casser la boucle
            write_history(chemin_historique);
            break;
        }

        // filtrage des entrees vides pour ne pas polluer l'historique
        if (est_ligne_utile(ligne_saisie)) {
            // ajout de la ligne a la structure memoire de readline
            add_history(ligne_saisie);
            // transmission de la chaine brute au moteur de traitement de la librairie
            traiterSequence(ligne_saisie);
        }
        // liberation de la ligne lue par readline
        free(ligne_saisie);
    }
    
    // securite finale: appel de la purge memoire en cas de sortie anormale de la boucle
    libererMots();
    return 0;
}