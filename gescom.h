// protection contre les inclusions multiples lors de la compilation
#ifndef GESCOM_H
#define GESCOM_H

// declaration de la fonction pour nettoyer la memoire du tableau de mots
void libererMots(void);

// declaration de la fonction qui decoupe une chaine en mots
int analyseCom(char *b);

// declaration de la fonction pour enregistrer une commande interne
void ajouteCom(char *nom, int (*fonc)(int, char **));

// declaration de la fonction qui execute une commande interne si elle existe
int execComInt(int N, char **P);

// declaration de la fonction qui delegue l'execution au systeme d'exploitation
int execComExt(char **P);

// declaration de la fonction qui gere les separateurs de sequence (point-virgule)
void traiterSequence(char *ligne);

#endif