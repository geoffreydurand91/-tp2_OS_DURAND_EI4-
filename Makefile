default : servudp cliudp

cliudp : cliudp.c
	cc -Wall -o cliudp cliudp.c

servudp : servudp.c
	cc -Wall -o servudp servudp.c

clean :
	rm -f cliudp servudp

