#ifndef _TUBII_LINK_H
#define _TUBII_LINK_H

#include <queue>
#include <pthread.h>


#include "NetUtils.h"
#include "GenericLink.h"


class TUBIILink : public GenericLink {
  public:
    TUBIILink();
    ~TUBIILink();

    int CloseConnection();
    int Connect();
    void RecvCallback(struct bufferevent *bev);
    void SentCallback(struct bufferevent *bev){};
    void EventCallback(struct bufferevent *bev, short what){};
    int SendCommand(const char* command);
    

  private:
    //std::queue<redisReply> fRecvQueue;
    pthread_mutex_t fRecvQueueLock;
    pthread_cond_t fRecvQueueCond;
    int fTempBytes;
    int fBytesLeft;
};
#endif
