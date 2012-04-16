#include <cstring>
#include <time.h>
#include <errno.h>

#include "Globals.h"

#include "NetUtils.h"
#include "MTCLink.h"


MTCLink::MTCLink() : GenericLink(SBC_PORT)
{
  pthread_mutex_init(&fRecvQueueLock,NULL);
  pthread_cond_init(&fRecvQueueCond,NULL);
}

MTCLink::~MTCLink()
{
}

int MTCLink::Connect()
{
  fFD = socket(AF_INET, SOCK_STREAM, 0);
  if (fFD <= 0){
    printf("Error opening a new socket for sbc connection!\n");
    throw 1;
  }

  struct sockaddr_in sbc_addr;
  memset(&sbc_addr,'\0',sizeof(sbc_addr));
  sbc_addr.sin_family = AF_INET;
  inet_pton(AF_INET, SBC_SERVER, &sbc_addr.sin_addr.s_addr);
  sbc_addr.sin_port = htons(SBC_PORT);
  // make the connection
  if (connect(fFD,(struct sockaddr*) &sbc_addr,sizeof(sbc_addr))<0){
    close(fFD);
    printf("Problem connecting to sbc socket!\n");
    throw 2;
  }

  int32_t test_word = 0x000DCBA;
  int n = write(fFD,(char*)&test_word,4);
  fConnected = 1;
  fBev = bufferevent_socket_new(evBase,fFD,BEV_OPT_CLOSE_ON_FREE | BEV_OPT_THREADSAFE);
  bufferevent_setwatermark(fBev, EV_READ, 0, 0); 
  bufferevent_setcb(fBev,&GenericLink::RecvCallbackHandler,&GenericLink::SentCallbackHandler,&GenericLink::EventCallbackHandler,this);
  bufferevent_enable(fBev,EV_READ | EV_WRITE);
  printf("Connected to SBC!\n");
  return 0;
}

int MTCLink::CloseConnection()
{
  if (fConnected){
    bufferevent_free(fBev);
    close(fFD);
    fConnected = 0;
  }
  return 0;
}

void MTCLink::RecvCallback(struct bufferevent *bev)
{
  int totalLength = 0;
  int n;
  char input[10000];
  memset(input,'\0',10000);
  while (1){
    bufferevent_lock(bev);
    n = bufferevent_read(bev, input+strlen(input), sizeof(input));
    bufferevent_unlock(bev);
    totalLength += n;
    if (n <= 0)
      break;
  }


  SBCPacket *packet = (SBCPacket *) input;
  printf("numBytes = %d, recvd = %d\n",packet->numBytes,n);
  pthread_mutex_lock(&fRecvQueueLock);
  fRecvQueue.push(*packet);
  pthread_cond_signal(&fRecvQueueCond);
  pthread_mutex_unlock(&fRecvQueueLock);

}


int MTCLink::SendPacket(SBCPacket *packet)
{

  packet->numBytes = packet->header.numberBytesinPayload + sizeof(uint32_t) +
    sizeof(SBCHeader) + kSBC_MaxMessageSizeBytes;
  int numBytesToSend = packet->numBytes;
  bufferevent_lock(fBev);
  bufferevent_write(fBev,packet,numBytesToSend);
  bufferevent_unlock(fBev);
  return 0;
}

int MTCLink::GetNextPacket(SBCPacket *packet,int waitSeconds)
{
  pthread_mutex_lock(&fRecvQueueLock);
  if (waitSeconds){
    struct timeval tp;
    struct timespec ts;
    gettimeofday(&tp, NULL);
    ts.tv_sec  = tp.tv_sec;
    ts.tv_nsec = tp.tv_usec * 1000;
    ts.tv_sec += waitSeconds;
    while (fRecvQueue.empty()){
      int rc = pthread_cond_timedwait(&fRecvQueueCond,&fRecvQueueLock,&ts);
      if (rc == ETIMEDOUT) {
        printf("Wait timed out!\n");
        rc = pthread_mutex_unlock(&fRecvQueueLock);
        return 1;
      }
    }
  }else{
    while (fRecvQueue.empty())
      pthread_cond_wait(&fRecvQueueCond,&fRecvQueueLock);
  }

  *packet = fRecvQueue.front();
  fRecvQueue.pop();
  pthread_mutex_unlock(&fRecvQueueLock);
  return 0;
}

