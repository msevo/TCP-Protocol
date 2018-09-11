#include <stdlib.h>
#include <stdint.h>
#include <vector>
#include <sys/time.h>
#include <strings.h>
#include "Constants.h"

using namespace std;

class Packet {
  public:
      Packet(uint16_t seqNum, uint16_t ackNum, uint16_t wnd, ssize_t dataLength, char type, char* data);
      Packet(uint16_t wnd);
      Packet();

      uint16_t getSeqNum() { return _seqNum; }
      uint16_t getAckNum() { return _ackNum; }
      uint16_t getWnd() { return _wnd; }
      uint16_t getDataLength() { return _dataLength; }
      char getType() { return _type; }
      void getData(char* data);

      unsigned char* encodeRawPacket();
      bool decodeRawPacket(unsigned char buffer[], size_t size);

      size_t getPacketSize() { return (HEADER_LENGTH + _dataLength); }

      bool isAcked() { return _isAcked; }
      void setAcked() { _isAcked = true; }
      bool isRetransmission() { return _isRetransmission; }
      void retransmitted() { _isRetransmission = true; }
      bool hasTimedOut();
      void setTimer() { gettimeofday(&_timeSent, NULL); }
      bool shouldIgnore() { return _shouldIgnore; }
      void setIgnore() { _shouldIgnore = true; }
      bool isSent() { return _isSent; }
      void setSent() { _isSent = true; }


  private:
    //header
    uint16_t _seqNum;
    uint16_t _ackNum;
    uint16_t _wnd;
    char _type;

    //data
    vector<char> _rawData;
    uint16_t _dataLength;

    bool _isAcked;
    bool _isRetransmission;
    bool _isSent;

    struct timeval _timeSent;

    bool _shouldIgnore;

    //functions
    void initPacket(char* data);

};
