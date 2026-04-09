biceps : biceps.c gescom.c
	gcc -Wall -o biceps biceps.c gescom.c -lreadline

servbeuip : servbeuip.c
	gcc -Wall -o servbeuip servbeuip.c

clibeuip : clibeuip.c
	gcc -Wall -o clibeuip clibeuip.c

clean :
	rm -f biceps servbeuip clibeuip *.o