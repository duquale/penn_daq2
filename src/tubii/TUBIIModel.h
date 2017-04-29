#ifndef _TUBII_MODEL_H
#define _TUBII_MODEL_H

#include <stdint.h>
#include "TUBIILink.h"
class TUBIIModel{

  public:
    TUBIIModel();
    ~TUBIIModel();

    int CloseConnection(){fLink->CloseConnection();};
    int Connect(){fLink->Connect();};
    int IsConnected(){return fLink->IsConnected();};
    int SendCommand(SBCPacket *packet, int withResponse = 1, int timeout = 5);

    int CheckLock();
    void Lock(){fLink->SetLock(1);};
    void UnLock(){fLink->SetLock(0);};

    int CheckQueue(int empty){return 0;};//fLink->CheckQueue(empty);};

    void SetECALBit(int bit);

  private:
    TUBIILink *fLink;
};

#endif
