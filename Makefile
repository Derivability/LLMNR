all:
	/usr/bin/x86_64-w64-mingw32-gcc -I /usr/include/wine/msvcrt/ -I /usr/include/wine/windows/ main.c -lws2_32 -o llmnr.exe