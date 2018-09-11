#include "Packet.h"

Packet::Packet(uint16_t seqNum, uint16_t ackNum, uint16_t wnd, ssize_t dataLength, char type, char* data) :
  _seqNum(seqNum), _ackNum(ackNum), _wnd(wnd), _dataLength(dataLength), _type(type), _isAcked(false),
  _isRetransmission(false), _shouldIgnore(false), _isSent(false) {
    initPacket(data);
  }

Packet::Packet(uint16_t wnd) :
  _seqNum(0), _ackNum(0), _wnd(wnd), _dataLength(0), _type('0'), _isAcked(false), _isRetransmission(false),
  _shouldIgnore(false), _isSent(false) {
    char* data = nullptr;
    initPacket(data);
  }

Packet::Packet() :
  _seqNum(0), _ackNum(0), _wnd(WIN_SIZE), _dataLength(0), _type('0'), _isAcked(false), _isRetransmission(false),
  _shouldIgnore(false), _isSent(false) {
    char* data = nullptr;
    initPacket(data);
  }

void Packet::getData(char* data) {
  for(ssize_t i = 0; i < _dataLength; i++) {
    data[i] = _rawData[i];
  }
  if (_dataLength < MAX_DATA_SIZE) {
    data[_dataLength] = '\0';
  }
}

unsigned char* Packet::encodeRawPacket() {
  unsigned char* rawPacket = new unsigned char[HEADER_LENGTH + _dataLength];
  bzero(rawPacket, (HEADER_LENGTH + _dataLength));

  rawPacket[0] = (_seqNum >> 8);
  rawPacket[1] = (_seqNum & 0xff);
  rawPacket[2] = (_ackNum >> 8);
  rawPacket[3] = (_ackNum & 0xff);
  rawPacket[4] = (_wnd >> 8);
  rawPacket[5] = (_wnd & 0xff);
  rawPacket[6] = _type;

  for(ssize_t i = 0; i < _dataLength; i++) {
    rawPacket[i+HEADER_LENGTH] = _rawData[i];
  }

  return rawPacket;
}

bool Packet::decodeRawPacket(unsigned char buffer[], size_t size) {
  if (size < HEADER_LENGTH) {
    return false;
  }

  _seqNum = (buffer[0] << 8) | buffer[1];
  _ackNum = (buffer[2] << 8) | buffer[3];
  _wnd = (buffer[4] << 8) | buffer[5];
  _type = buffer[6];
  _dataLength = (size-HEADER_LENGTH);

  _rawData.clear();
  for(ssize_t i = HEADER_LENGTH; i < size; i++) {
    _rawData.push_back(buffer[i]);
  }

  return true;
}

bool Packet::hasTimedOut() {
  struct timeval currTime;
  gettimeofday(&currTime, NULL);

  ssize_t msPassed = ((currTime.tv_sec - _timeSent.tv_sec)*1000) + ((currTime.tv_usec - _timeSent.tv_usec)/1000);
  if (msPassed > RTO) {
    return true;
  }

  return false;
}

void Packet::initPacket(char* data) {
  if (data != nullptr) {
    for(ssize_t i = 0; i < _dataLength; i++) {
  		_rawData.push_back(data[i]);
  	}
  }
}
