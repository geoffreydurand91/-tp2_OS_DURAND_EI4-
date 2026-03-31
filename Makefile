default : servudp cliudp servbeuip

cliudp : cliudp.c
	cc -Wall -o cliudp cliudp.c

servudp : servudp.c
	cc -Wall -o servudp servudp.c

servbeuip : servbeuip.c
	cc -Wall -o servbeuip servbeuip.c

clean :
	rm -f cliudp servudp servbeuip