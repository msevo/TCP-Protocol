CC=g++
files=server.cpp Makefile report.pdf README
.SILENT:

default:
	$(CC) -g -o server server.cpp Packet.cpp
	$(CC) -g -o client client.cpp Packet.cpp

clean:
	rm -f 704833042.tar.gz

dist:
	tar -czf 704833042.tar.gz $(files)
