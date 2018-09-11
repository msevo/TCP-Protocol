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
#include <netdb.h>
#include <sys/time.h>
#include <algorithm>    // std::min

using namespace std;

#include "Packet.h"

//needed atexit() to clean up (close sockfd)
int sockfd;
socklen_t clilen;
struct sockaddr_in serv_addr, cli_addr;

uint16_t nextSeqNum = 0;
uint16_t sendBase = 0;
uint16_t expectedSeqNum = 0;
uint16_t lastAck = 0;
int numDuplicateAcks = 0;

uint16_t wndSize = WIN_SIZE;
uint16_t cwnd = PACKET_SIZE;
uint16_t ssthresh = SSTHRESH_SIZE;
int ccMode = 0; //0 = slow start, 1 = congestion avoidance

char filename[MAX_DATA_SIZE];

vector<Packet> filePackets;

void error(const char *m)
{
    perror(m);
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
  if (packet.getType() == SYNACK) {
    packetType = " SYN";
  } else if (packet.getType() == FIN) {
    packetType = " FIN";
  }
  string retransmissionString = "";
  if (packet.isRetransmission()) {
    retransmissionString = " Retransmission";
  }
  printf("Sending packet %u %u %u%s%s\n", packet.getSeqNum(), cwnd, ssthresh, retransmissionString.c_str(), packetType.c_str());
  fflush(stdout);
}

void printPacketReceived(Packet packet) {
  printf("Receiving packet %u\n", packet.getSeqNum());
  fflush(stdout);
}

int minWindow() {
  return min(min((int)cwnd, (int)filePackets.size()), (int)(wndSize/PACKET_SIZE));
}

bool sendPacket(Packet packet) {
  unsigned char* rawPacket = packet.encodeRawPacket();

  int n = sendto(sockfd, rawPacket, packet.getPacketSize(), 0, (struct sockaddr *) &cli_addr, clilen);
  if (n < 0)
      error("ERROR in sendto");

  if (!packet.isRetransmission()) {
    nextSeqNum = (nextSeqNum + packet.getDataLength()) % MAX_SEQ_NUM;
  }

  packet.setSent();

  printPacketSent(packet);

  return true;
}

bool sendPacketCongestion(int i, bool retransmited) {
  if (filePackets[i].getSeqNum() <= (sendBase + minWindow())) {
    if (retransmited) {
      filePackets[i].retransmitted();
    }
    sendPacket(filePackets[i]);
    filePackets[i].setTimer();
    filePackets[i].setSent();
  }
}

bool recvPacketMain(Packet &packet, bool blocking) {
  unsigned char buffer[PACKET_SIZE];
  int n;
  if (blocking) {
    n = recvfrom(sockfd, buffer, PACKET_SIZE, 0, (struct sockaddr *) &cli_addr, &clilen);
  } else {
    n = recvfrom(sockfd, buffer, PACKET_SIZE, MSG_DONTWAIT, (struct sockaddr *) &cli_addr, &clilen);
  }
  if (n == -1) {
    return false; //timed out
  }

  packet = Packet();
  packet.decodeRawPacket(buffer, n);

  if (expectedSeqNum == packet.getSeqNum()) {
    //packet received in correct order
    expectedSeqNum = (expectedSeqNum + packet.getDataLength()) % MAX_SEQ_NUM;
  }

  if (packet.getAckNum() > sendBase || (abs(packet.getAckNum() - sendBase) > (MAX_SEQ_NUM/2))) {
    sendBase = packet.getAckNum();
  }

  wndSize = packet.getWnd();
  cwnd += PACKET_SIZE;

  printPacketReceived(packet);

  return true;
}

bool recvPacket(Packet &packet) {
  return recvPacketMain(packet, true); //BLOCKING
}

bool recvPacketNonBlocking(Packet &packet) {
  return recvPacketMain(packet, false); //NON-BLOCKING
}

void sendAck() {
  Packet packet = Packet(nextSeqNum, expectedSeqNum, WIN_SIZE, 0, ACK, nullptr);
  sendPacket(packet);
}

void runCongestionControl(Packet ackPacket) {
  if (ccMode == 0) {
    //slow start
    if (ackPacket.getAckNum() > lastAck || (abs(ackPacket.getAckNum() - lastAck) > (MAX_SEQ_NUM/2))) {
      lastAck = ackPacket.getAckNum();
      numDuplicateAcks = 0;
      cwnd += PACKET_SIZE;

      if (cwnd >= ssthresh) {
        ccMode = 1;
        return;
      }
    } else if (ackPacket.getAckNum() == lastAck) {
      numDuplicateAcks++;

      if (numDuplicateAcks >= 3) {
        //retransmit packet with sequence number == this ackNumer
        ssthresh = (cwnd/2);
        ssize_t currWnd = minWindow();
        for (int i = 0; i < currWnd; i++) {
          if (filePackets[i].getSeqNum() == ackPacket.getAckNum()) {
            sendPacketCongestion(i, true);
          }
        }
      }
    }

  } else {
    //congestion avoidance
    if (ackPacket.getAckNum() > lastAck || (abs(ackPacket.getAckNum() - lastAck) > (MAX_SEQ_NUM/2))) {
      lastAck = ackPacket.getAckNum();
      numDuplicateAcks = 0;
      cwnd += (PACKET_SIZE/cwnd);
    } else if (ackPacket.getAckNum() == lastAck) {
      numDuplicateAcks++;

      if (numDuplicateAcks >= 3) {
        //retransmit packet with sequence number == this ackNumer
        ssthresh = (cwnd/2);
        cwnd = ssthresh + (3 * PACKET_SIZE);
        ssize_t currWnd = minWindow();
        for (int i = 0; i < currWnd; i++) {
          if (filePackets[i].getSeqNum() == ackPacket.getAckNum()) {
            sendPacketCongestion(i, true);
          }
        }
      }
    }

  }
}

void establishHandshake() {
  //Wait for SYN
  Packet synPacket = Packet();
  while(!recvPacket(synPacket)) {
    continue;
  }
  expectedSeqNum = (synPacket.getSeqNum() + 1) % MAX_SEQ_NUM; //manually increasing here because this specific packet is a SYN

  //Send SYNACK
  Packet synAckPacket = Packet(nextSeqNum, expectedSeqNum, WIN_SIZE, 0, SYNACK, nullptr);
  sendPacket(synAckPacket);
  synAckPacket.setTimer();
  nextSeqNum = (nextSeqNum+1) % MAX_SEQ_NUM; //manually increasing here because this specific packet is a SYNACK
  sendBase = nextSeqNum;

  //Get final ACK
  setRecvTimeout(RTO);
  Packet finalAckPacket = Packet();
  while(!recvPacketNonBlocking(finalAckPacket) || finalAckPacket.getType() != ACK) {
    if (synAckPacket.hasTimedOut()) {
      //Timeout
      synAckPacket.retransmitted();
      synAckPacket.setTimer();
      sendPacket(synAckPacket);
    }
  }
  finalAckPacket.getData(filename);

}

bool sendFile(char filename[]) {
  //First separate file into packet chunks
  ssize_t chunkSize = MAX_DATA_SIZE;
  char buffer[chunkSize];

  ifstream is(filename, ios::in | ios::binary);

  is.seekg (0, is.end);
  ssize_t fileSize = is.tellg();
  is.seekg (0, is.beg);

  int numChunks = fileSize/chunkSize;
  ssize_t lastChunkSize = fileSize % chunkSize;
  if (lastChunkSize > 0) { numChunks++; }

  for (int i = 0; i < numChunks; i++) {
    ssize_t dataSize = chunkSize;
    int16_t tempSeqNum = (nextSeqNum+(chunkSize * i)) % MAX_SEQ_NUM;
    if (i == (numChunks - 1)) {
      dataSize = lastChunkSize;
    }

    is.read (buffer, dataSize);
    if (!is)
      error("Error reading file.");

    Packet packet = Packet(tempSeqNum, expectedSeqNum, WIN_SIZE, dataSize, ACK, buffer);
    filePackets.push_back(packet);
  }

  //Now send as many chunks as possible
  ssize_t currWnd = minWindow();
  for (int i = 0; i < currWnd; i++) {
    sendPacketCongestion(i, false);
  }

  while (!filePackets.empty()) {
    //check for acks
    Packet ackPacket = Packet();
    while (recvPacketNonBlocking(ackPacket)) {
      runCongestionControl(ackPacket);

      //check for new acks
      for (int i = 0; i < filePackets.size(); i++) {
        uint16_t tempExpectedSeqNum = (filePackets[i].getSeqNum() + filePackets[i].getDataLength()) % MAX_SEQ_NUM;
        if (tempExpectedSeqNum == ackPacket.getAckNum()) {
          filePackets[i].setAcked();

          filePackets.erase(filePackets.begin(), filePackets.begin() + i + 1);
          break;
        }
      }
    }

    //check for timeouts and send those in window not yet sent
    currWnd = minWindow();
    for (int i = 0; i < currWnd; i++) {
      if (!filePackets[i].isSent()) {
        sendPacketCongestion(i, false);
      }
      if (!filePackets[i].isAcked() && filePackets[i].hasTimedOut()) {
        //Timeout; retransmit
        sendPacketCongestion(i, true);

        //update congestion values
        ssthresh = (cwnd/2);
        cwnd = PACKET_SIZE;
        ccMode = 0; //slow start
      }
    }
  }

  //Send FIN
  Packet finPacket = Packet(nextSeqNum, expectedSeqNum, WIN_SIZE, 0, FIN, nullptr);
  sendPacket(finPacket);
  finPacket.setTimer();
  nextSeqNum = (nextSeqNum + 1) % MAX_SEQ_NUM; //manually increasing here because this specific packet is a FIN

  //Wait for client FIN
  setRecvTimeout(RTO);
  Packet clientFinPacket = Packet();
  while(!recvPacketNonBlocking(clientFinPacket) || clientFinPacket.getType() != FIN) {
    if (finPacket.hasTimedOut()) {
      finPacket.retransmitted();
      finPacket.setTimer();
      sendPacket(finPacket);
    }
  }
  expectedSeqNum = (expectedSeqNum + 1) % MAX_SEQ_NUM; //manually increasing here because this specific packet is a FIN

  //Send final ACK
  sendAck();
  setRecvTimeout(RTO*2);
  while(recvPacket(clientFinPacket)) {
    //Resend final ACK
    sendAck();
  }

  return true;
}

int main(int argc, char *argv[])
{
    int portno;

    if (argc < 3) {
        fprintf(stderr, "Usage: %s <hostname> <port>\n", argv[0]);
        exit(1);
    }

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
        error("Socket opening error");
    memset((char *) &serv_addr, 0, sizeof(serv_addr));

    portno = atoi(argv[2]);
    serv_addr.sin_family = AF_INET;
    inet_pton(AF_INET, argv[1], &serv_addr.sin_addr.s_addr);
    serv_addr.sin_port = htons(portno);

    clilen = sizeof(cli_addr);

    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0)
        error("Socket binding error");

    atexit(cleanup);

    establishHandshake();

    sendFile(filename);

    return 0;
}
