
#include <unistd.h>
#include "Globals.h"

#include "NetUtils.h"
#include "TUBIILink.h"


TUBIILink::TUBIILink() : GenericLink() {
  fBytesLeft = 0;
  fTempBytes = 0;
  pthread_mutex_init(&fRecvQueueLock,NULL);
  pthread_cond_init(&fRecvQueueCond,NULL);
}

TUBIILink::~TUBIILink()
{ }

int TUBIILink::Connect()
{
  fFD = socket(AF_INET, SOCK_STREAM, 0);
  if (fFD <= 0) {
    lprintf("Error opening a new socket for tubii connection!\n");
    throw "Error opening a new socket\n";
  }

  struct sockaddr_in tubii_addr;
  memset(&tubii_addr,'\0',sizeof(tubii_addr));
  tubii_addr.sin_family = AF_INET;
  inet_pton(tubii_addr.sin_family, TUBII_SERVER, &tubii_addr.sin_addr.s_addr);
  tubii_addr.sin_port = htons(TUBII_PORT);

  // make the connection
  if (connect(fFD,(struct sockaddr*) &tubii_addr,sizeof(tubii_addr))<0){
    close(fFD);
    lprintf("Problem connecting to tubii socket: %s\n",strerror(errno));
    throw "Problem connecting to socket\n";
  }
  fConnected = 1;

  fBev = bufferevent_socket_new(evBase,fFD,BEV_OPT_CLOSE_ON_FREE | BEV_OPT_THREADSAFE);
  bufferevent_setwatermark(fBev, EV_READ, 0, 0); 
  bufferevent_setcb(fBev,&GenericLink::RecvCallbackHandler,&GenericLink::SentCallbackHandler,&GenericLink::EventCallbackHandler,this);
  bufferevent_enable(fBev,EV_READ | EV_WRITE);

  lprintf("Connected to TUBii!\n");
  return 0;
}

int TUBIILink::CloseConnection()
{
  if (fConnected) {
    close(fFD);
    fConnected = 0;
  }
  return 0;
}


// Assumes null terminated string
int read_until(const char * input, char limit) {
    const char* f = strchr(input,limit);
    if(f == NULL) {
        return -1;
    }
    return f - input;
}

int TUBIILink::SendCommand(const char* command)
{
    if(!fConnected) {
        Connect();
    }
    int numBytesToSend = strlen(command);
    bufferevent_lock(fBev);
    bufferevent_write(fBev,command,numBytesToSend);
    bufferevent_unlock(fBev);
    return 0;
}

void TUBIILink::RecvCallback(struct bufferevent *bev)
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
      if (n <= 0) {
          break;
      }
  }
  lprintf("%s\n", input);
  /*char *inputP = input;
  while (totalLength > 0) {
    if (fTempBytes == 0) {
        char type = input[0];
        if(type == '+') {
            //Simple string
            lprintf("Simple\n");
            int f = read_until(inputP,'\n');
            printf(inputP);
            if(f < 0) {

                fTempBytes = n;
            }
            else {
            // ???
            // printf(input[0:f])?
            }
        }
        else if (type == ':') {
            // Integer
        }
        else if (type == '-') {
            // Error

        }
        else if (type == '$') {
            // Bulk String

        }
        else if (type == '*') {
            // Array

        }
    }else{

      }
    break;
    }
*/
}
