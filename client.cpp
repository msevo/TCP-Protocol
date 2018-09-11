#include <stdio.h>
#include <iostream>
#include <sys/types.h>   // definitions of a number of data types used in socket.h and netinet/in.h
#include <sys/socket.h>  // definitions of structures needed for sockets, e.g. sockaddr
#include <netinet/in.h>  // constants and structures needed for internet domain addresses, e.g. sockaddr_in
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sstream>
#include <fstream>
#include <signal.h>  /* signal name macros, and the kill() prototype */
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <poll.h>
#include <arpa/inet.h>
#include <cmath>        // std::abs

#include "Packet.h"

using namespace std;

//needed atexit() to clean up (close sockfd)
int sockfd;
socklen_t server_length;
struct sockaddr_in serv_addr;

uint16_t nextSeqNum = 0;
uint16_t expectedSeqNum = 0;
uint16_t rwnd = WIN_SIZE;

ofstream outputFile;
vector<Packet> packetBuffer; //buffers packets

void error(const char *m)
{
    perror(m);
    outputFile.close();
    exit(1);
}

//close sockfd on exit
void cleanup() {
    close(sockfd);
}

void setRecvTimeout(int ms) {
  struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = ms*1000;
  while (tv.tv_usec >= 1000000) {
    tv.tv_usec -= 1000000;
    tv.tv_sec++;
  }

	if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0)
		error("Timeout failed.");
}

void printPacketSent(Packet packet) {
  string packetType = "";
  if (packet.getType() == SYN) {
    packetType = " SYN";
  } else if (packet.getType() == FIN) {
    packetType = " FIN";
  }
  string retransmissionString = "";
  if (packet.isRetransmission()) {
    retransmissionString = " Retransmission";
  }
  printf("Sending packet %u%s%s\n", packet.getSeqNum(), retransmissionString.c_str(), packetType.c_str());
  fflush(stdout);
}

void printPacketReceived(Packet packet) {
  printf("Receiving packet %u\n", packet.getSeqNum());
  fflush(stdout);
}

bool sendPacket(Packet packet) {
  unsigned char* rawPacket = packet.encodeRawPacket();

  int n = sendto(sockfd, rawPacket, packet.getPacketSize(), 0, (struct sockaddr *) &serv_addr, server_length);
  if (n < 0)
      return false;

  if (!packet.isRetransmission()) {
    nextSeqNum = (nextSeqNum + packet.getDataLength()) % MAX_SEQ_NUM;
  }

  printPacketSent(packet);

  return true;
}

void sendAck() {
  Packet packet = Packet(nextSeqNum, expectedSeqNum, rwnd, 0, ACK, nullptr);
  sendPacket(packet);
}

bool recvPacket(Packet &packet) {
  unsigned char buffer[PACKET_SIZE];
  int n = recvfrom(sockfd, buffer, PACKET_SIZE, 0, (struct sockaddr *) &serv_addr, &server_length);
  if (n == -1)
    return false; //timed out

  packet = Packet(rwnd);
  packet.decodeRawPacket(buffer, n);

  printPacketReceived(packet);

  return true;
}

void establishHandshake() {

  //Send SYN
  Packet synPacket = Packet(nextSeqNum, expectedSeqNum, rwnd, 0, SYN, nullptr);
  sendPacket(synPacket);
  nextSeqNum = (nextSeqNum + 1) % MAX_SEQ_NUM; //manually increasing here because this specific packet is a SYN

  //Get SYNACK
  setRecvTimeout(RTO);
  Packet synAckPacket = Packet(rwnd);
  while(!recvPacket(synAckPacket)) {
    //Timeout
    synPacket.retransmitted();
    sendPacket(synPacket);
  }
  expectedSeqNum = (synAckPacket.getSeqNum()+1) % MAX_SEQ_NUM;

  //Final ack is piggybacked with request for file

}

void closeConnection() {
  //printf("close connection called\n");
  if (outputFile.is_open()) {
    outputFile.close();
  }

  //Send FIN/ACK
  expectedSeqNum = (expectedSeqNum + 1) % MAX_SEQ_NUM; //manually increasing here because this specific packet is a FIN
  Packet finPacket = Packet(nextSeqNum, expectedSeqNum, rwnd, 0, FIN, nullptr);
  sendPacket(finPacket);
  finPacket.setTimer();
  nextSeqNum = (nextSeqNum + 1) % MAX_SEQ_NUM; //manually increasing here because this specific packet is a SYNACK

  //Get FIN ACK
  setRecvTimeout(RTO);
  Packet finAckPacket = Packet(rwnd);
  while(!recvPacket(finAckPacket)) {
    if (finPacket.hasTimedOut()) {
      finPacket.retransmitted();
      finPacket.setTimer();
      sendPacket(finPacket);
    }
  }
}

void writeToFile(Packet packet) {
  //write to file
  char fileData[MAX_DATA_SIZE];
  packet.getData(fileData);
  for(ssize_t i = 0; i < packet.getDataLength(); i++){
      outputFile << fileData[i];
  }
}

void processPacketBuffer(Packet packet) {
  packet.setAcked();
  if (packet.getSeqNum() == expectedSeqNum) {
    writeToFile(packet);
    expectedSeqNum = (expectedSeqNum + packet.getDataLength()) % MAX_SEQ_NUM;
  } else if (packet.getSeqNum() > expectedSeqNum || (abs(packet.getSeqNum() - expectedSeqNum) > (MAX_SEQ_NUM/2))) {
    bool isInBuffer = false;
    for (int i = 0; i < packetBuffer.size(); i++) {
      if (packet.getSeqNum() == packetBuffer[i].getSeqNum()) {
        isInBuffer = true;
      }
    }
    if (!isInBuffer) {
      packetBuffer.push_back(packet);
      rwnd -= packet.getPacketSize();
    }
  }

  for (int i = 0; i < packetBuffer.size(); i++) {
    if (packetBuffer[i].getSeqNum() == expectedSeqNum) {
      writeToFile(packetBuffer[i]);
      expectedSeqNum = (expectedSeqNum + packetBuffer[i].getDataLength()) % MAX_SEQ_NUM;
      rwnd += packetBuffer[i].getPacketSize();
      packetBuffer.erase(packetBuffer.begin() + i);
      i = -1; //reset i to look through again because now there might be more in-order chunks
    }
  }
  sendAck();
}

bool requestFile(string file) {
  //Open/create output file
  outputFile.open("received.data", ofstream::out | std::ofstream::trunc);

  //Send request for file
  char filename[file.length()+1];
  strcpy(filename, file.c_str());
  Packet requestPacket = Packet(nextSeqNum, expectedSeqNum, rwnd, (file.length()+1), ACK, filename);
  sendPacket(requestPacket);
  requestPacket.setTimer();

  //Await first chunk of file, acts as ack for request
  Packet firstChunk = Packet(rwnd);
  while(!recvPacket(firstChunk)) {
    if (requestPacket.hasTimedOut()) {
      //Timeout
      requestPacket.retransmitted();
      requestPacket.setTimer();
      sendPacket(requestPacket);
    }
  }
  processPacketBuffer(firstChunk);

  //Await rest of file and FIN packet
  while (1) {
    Packet packet = Packet(rwnd);
    if (recvPacket(packet)) {
      if (packet.getType() != FIN) {
        processPacketBuffer(packet);
      } else {
        closeConnection();
        return true;
      }
    } else {
      //timeout or received duplicate packet, just send ack

      //problem is fin never obtained but ack sent here so it thinks it got acked
      sendAck();
    }
  }

  return true;
}

int main(int argc, char *argv[])
{

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <filename>\n", argv[0]);
        exit(1);
    }

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
        error("Socket opening error");
    memset((char *) &serv_addr, 0, sizeof(serv_addr));

    serv_addr.sin_family = AF_INET;
    inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr.s_addr);
    serv_addr.sin_port = htons(9000);
    server_length = sizeof(serv_addr);

    atexit(cleanup);

    establishHandshake();

    //Request file
    requestFile(argv[1]);

    return 0;
}
