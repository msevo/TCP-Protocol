//values in bytes
#define PACKET_SIZE 1024
#define HEADER_LENGTH 7
#define MAX_DATA_SIZE PACKET_SIZE-HEADER_LENGTH
#define MAX_SEQ_NUM 30720
#define WIN_SIZE 5120
#define SSTHRESH_SIZE 15360
#define RTO 500 //miliseconds

#define SYN 0
#define SYNACK 1
#define ACK 2
#define FIN 3
