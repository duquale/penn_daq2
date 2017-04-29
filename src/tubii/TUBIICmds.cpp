#include <stdlib.h>
#include <unistd.h>

#include "Globals.h"
#include "TUBIILink.h"
#include "TUBIIModel.h"


int SetECALBit(int bit) {
    tubii->SetECALBit(bit);
    return 0;
}
