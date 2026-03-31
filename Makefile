CC = cc
CFLAGS = -Wall -g
LDFLAGS = -lreadline

all: biceps servbeuip clibeuip

# compilation de l'executable principal avec les deux librairies
biceps: biceps.o gescom.o creme.o
	$(CC) $(CFLAGS) -o biceps biceps.o gescom.o creme.o $(LDFLAGS)

# serveurs et clients de test (utilisent aussi creme.o)
servbeuip: servbeuip.o creme.o
	$(CC) $(CFLAGS) -o servbeuip servbeuip.o creme.o

clibeuip: clibeuip.o creme.o
	$(CC) $(CFLAGS) -o clibeuip clibeuip.o creme.o

# regles generiques pour les fichiers objets
%.o: %.c %.h
	$(CC) $(CFLAGS) -c $<

%.o: %.c
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f *.o biceps servbeuip clibeuip