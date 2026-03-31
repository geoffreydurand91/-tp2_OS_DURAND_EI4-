default : servudp cliudp servbeuip clibeuip

cliudp : cliudp.c
	cc -Wall -o cliudp cliudp.c

servudp : servudp.c
	cc -Wall -o servudp servudp.c

servbeuip : servbeuip.c
	cc -Wall -o servbeuip servbeuip.c

clibeuip : clibeuip.c
	cc -Wall -o clibeuip clibeuip.c

clean :
	rm -f cliudp servudp servbeuip clibeuip