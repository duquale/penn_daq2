#include <stdlib.h>

#include "Globals.h"

#include "TUBIILink.h"
#include "TUBIIModel.h"

TUBIIModel::TUBIIModel()
{
  fLink = new TUBIILink();
}

TUBIIModel::~TUBIIModel()
{
  delete fLink;
}

int TUBIIModel::CheckLock()
{
//  if (!fLink->IsConnected())
//    return NO_CONNECTION_FLAG;
//  if (fLink->IsLocked())
//    return BUSY_CONNECTION_FLAG;
  return 0;
}

void TUBIIModel::SetECALBit(int bit) {
    lprintf("SetECALBit %i\n", bit);
    char buffer[32];
    snprintf(buffer,32, "SetECalBit %i\n",bit);
    fLink->SendCommand(buffer);
}
