#include <stdlib.h>
#include <math.h>

#include "XL3PacketTypes.h"
#include "XL3Registers.h"
#include "DacNumber.h"
#include "Globals.h"
#include "Json.h"

#include <libpq-fe.h>

#include "DB.h"
#include "DetectorDB.h"
#include "XL3Model.h"
#include "MTCModel.h"
#include "GTValidTest.h"

int GTValidTest(uint32_t crateMask, uint32_t *slotMasks, uint32_t channelMask, float gtCutoff, int twiddleOn, int setOnly, int updateDB, int updateDetectorDB, int finalTest, int ecal)
{
  lprintf("*** Starting GT Valid Test *************\n");

  uint32_t result;
  int error;
  int chan_errors[20][16][32];

  uint16_t tacbits[20][16][32];
  uint16_t max_isetm[20][16][32],isetm[2][20][16];
  float max_gtvalid[20][16][32];
  float gtvalid_final[2][32] = {{0}};
  float gmax[2],gmin[2];
  int cmax[2],cmin[2];


  uint32_t dac_nums[50],dac_values[50],slot_nums[50];
  int num_dacs;

  try{

    // setup crate
    for (int crateNum=0;crateNum<20;crateNum++){
      if ((0x1<<crateNum) & crateMask){
        for (int i=0;i<16;i++){
          if ((0x1<<i) & slotMasks[crateNum]){
            uint32_t select_reg = FEC_SEL*i;
            // disable pedestals
            xl3s[crateNum]->RW(PED_ENABLE_R + select_reg + WRITE_REG,0x0,&result);
            // reset fifo
            xl3s[crateNum]->RW(CMOS_CHIP_DISABLE_R + select_reg + WRITE_REG,0xFFFFFFFF,&result);
            xl3s[crateNum]->RW(GENERAL_CSR_R + select_reg + READ_REG,0x0,&result);
            xl3s[crateNum]->RW(GENERAL_CSR_R + select_reg + WRITE_REG,
                result | (crateNum << FEC_CSR_CRATE_OFFSET) | 0x6,&result);
            xl3s[crateNum]->RW(GENERAL_CSR_R + select_reg + WRITE_REG,
                (crateNum << FEC_CSR_CRATE_OFFSET),&result);
            xl3s[crateNum]->RW(CMOS_CHIP_DISABLE_R + select_reg + WRITE_REG,0x0,&result);
          }
        }
        xl3s[crateNum]->DeselectFECs();
      }
    }

    if (mtc->SetupPedestals(0,DEFAULT_PED_WIDTH,10,0,crateMask,crateMask)){
      lprintf("Error setting up mtc. Exiting\n");
      return -1;
    }

    for (int crateNum=0;crateNum<20;crateNum++){
      for (int i=0;i<16;i++){ 
        for (int j=0;j<32;j++){
          chan_errors[crateNum][i][j] = 0;
        }
      }
    }

    for (int crateNum=0;crateNum<20;crateNum++){
      if ((0x1<<crateNum) & crateMask){
        num_dacs = 0;
        for (int i=0;i<16;i++){
          if ((0x1<<i) & slotMasks[crateNum]){
            dac_nums[num_dacs] = d_vmax;
            dac_values[num_dacs] = VMAX;
            slot_nums[num_dacs] = i;
            num_dacs++;
            dac_nums[num_dacs] = d_tacref;
            dac_values[num_dacs] = TACREF;
            slot_nums[num_dacs] = i;
            num_dacs++;
          }
        }
        xl3s[crateNum]->MultiLoadsDac(num_dacs,dac_nums,dac_values,slot_nums);
      }
    }

    // We first need to find the max gtvalid per channel, and find the
    // ISETM settings per channel for max gtvalid 
    lprintf("Finding max possible gtvalids\n");

    // turn off twiddle bits
    for (int crateNum=0;crateNum<20;crateNum++){
      if ((0x1<<crateNum) & crateMask){
        num_dacs = 0;
        for (int i=0;i<16;i++){
          if ((0x1<<i) & slotMasks[crateNum]){
            dac_nums[num_dacs] = d_iseta[0];
            dac_values[num_dacs] = ISETA_NO_TWIDDLE;
            slot_nums[num_dacs] = i;
            num_dacs++;
            dac_nums[num_dacs] = d_iseta[1];
            dac_values[num_dacs] = ISETA_NO_TWIDDLE;
            slot_nums[num_dacs] = i;
            num_dacs++;
          }
        }
        xl3s[crateNum]->MultiLoadsDac(num_dacs,dac_nums,dac_values,slot_nums);
      }
    }

    for (int crateNum=0;crateNum<20;crateNum++){
      if ((0x1<<crateNum) & crateMask){
        for (int i=0;i<16;i++){
          for (int j=0;j<32;j++)
            tacbits[crateNum][i][j] = 0x00;
        }
        for (int i=0;i<16;i++){
          if ((0x1<<i) & slotMasks[crateNum]){
            error = xl3s[crateNum]->LoadTacbits(i,tacbits[crateNum][i]);
            // FIXME
            if (error)
              lprintf("Crate %d slot %d: Error setting up TAC voltages. Exiting\n",crateNum,i);
          }
        }
        /*
           if (error){
           lprintf("Error setting up TAC voltages. Exiting\n");
           return -1;
           }
           */
      }
    }

    // loop over channels
    for (int j=0;j<32;j++){
      if ((0x1<<j) & channelMask){
        for (int crateNum=0;crateNum<20;crateNum++){
          if ((0x1<<crateNum) & crateMask){
            for (int i=0;i<16;i++){
              if ((0x1<<i) & slotMasks[crateNum]){
                xl3s[crateNum]->RW(PED_ENABLE_R + FEC_SEL*i + WRITE_REG,1<<j,&result);
              }
            }
            for (int i=0;i<16;i++){
              max_gtvalid[crateNum][i][j] = 0;
            }
            // first try with the default ISETM
            num_dacs = 0;
            for (int i=0;i<16;i++){
              if ((0x1<<i) & slotMasks[crateNum]){
                dac_nums[num_dacs] = d_isetm[0];
                dac_values[num_dacs] = ISETM_MAX_GTVALID;
                slot_nums[num_dacs] = i;
                num_dacs++;
                dac_nums[num_dacs] = d_isetm[1];
                dac_values[num_dacs] = ISETM_MAX_GTVALID;
                slot_nums[num_dacs] = i;
                num_dacs++;
              }
            }
            error+= xl3s[crateNum]->MultiLoadsDac(num_dacs,dac_nums,dac_values,slot_nums);
          }
        }

        uint16_t islonger[20], islonger2[20];
        for (int crateNum=0;crateNum<20;crateNum++){
          islonger[crateNum] = 0x0;
          islonger2[crateNum] = 0x0;
        }

        IsGTValidLonger(crateMask,slotMasks,GTMAX,islonger);
        uint32_t notDoneMask[20];
        uint32_t crateNotDoneMask = crateMask;
        for (int crateNum=0;crateNum<20;crateNum++){
          if ((0x1<<crateNum) & crateMask){
            notDoneMask[crateNum] = slotMasks[crateNum] & (~islonger[crateNum]);
            for (int i=0;i<16;i++){
              if ((0x1<<i) & slotMasks[crateNum]){
                if ((0x1<<i) & islonger[crateNum]){
                  max_gtvalid[crateNum][i][j] = GTMAX;
                  max_isetm[crateNum][i][j] = ISETM_MAX_GTVALID;
                }
              }
            }
            if (notDoneMask[crateNum] == 0x0)
              crateNotDoneMask &= ~(0x1<<crateNum);
          }
        }
        if (crateNotDoneMask){
          // scan to see if any ISETM value puts this channel over GTMAX
          int done = 0;
          for (int k=0;k<8;k++){
            float max_time = 1000.0-100.0*k;
            for (int l=0;l<50;l++){
              uint32_t isetm_temp = l*5;
              for (int crateNum=0;crateNum<20;crateNum++){
                if ((0x1<<crateNum) & crateNotDoneMask){
                  num_dacs = 0;
                  for (int i=0;i<16;i++){
                    if ((0x1<<i) & notDoneMask[crateNum]){
                      dac_nums[num_dacs] = d_isetm[0];
                      dac_values[num_dacs] = isetm_temp;
                      slot_nums[num_dacs] = i;
                      num_dacs++;
                      dac_nums[num_dacs] = d_isetm[1];
                      dac_values[num_dacs] = isetm_temp;
                      slot_nums[num_dacs] = i;
                      num_dacs++;
                    }
                  }
                  error+= xl3s[crateNum]->MultiLoadsDac(num_dacs,dac_nums,dac_values,slot_nums);
                }
              }
              IsGTValidLonger(crateMask,notDoneMask,max_time,islonger2);
              for (int crateNum=0;crateNum<20;crateNum++){
                if ((0x1<<crateNum) & crateNotDoneMask){
                  for (int i=0;i<16;i++){
                    if ((0x1<<i) & notDoneMask[crateNum]){
                      if ((0x1<<i) & islonger2[crateNum]){
                        max_gtvalid[crateNum][i][j] = max_time;
                        max_isetm[crateNum][i][j] = isetm_temp;
                        notDoneMask[crateNum] &= ~(islonger2[crateNum]);
                      }
                    }
                  }
                  if (notDoneMask[crateNum] == 0x0){
                    crateNotDoneMask &= ~(0x1<<crateNum);
                  }
                }
              }
              if (crateNotDoneMask == 0x0)
                break;
            }
            if (crateNotDoneMask == 0x0)
              break;
          }

          // if the max gtvalid time is too small, fail this channel
          for (int crateNum=0;crateNum<20;crateNum++){
            if ((0x1<<crateNum) & crateNotDoneMask){
              for (int i=0;i<16;i++){
                if ((0x1<<i) & slotMasks[crateNum]){
                  if (max_gtvalid[crateNum][i][j] == 0){
                    chan_errors[crateNum][i][j] = 1;
                  }
                }
              }
            }
          }
        }
      } // end channel mask
    } // end loop over channels


    // ok we now know what the max gtvalid is for each channel and what
    // isetm value will get us it
    // now we increment isetm until every channels gtvalid is shorter than
    // gtcutoff
    for (int wt=0;wt<2;wt++){
      lprintf("Finding ISETM values for crates %05x, TAC %d\n",crateMask,wt);
      int ot = (wt+1)%2;
      for (int crateNum=0;crateNum<20;crateNum++){
        if ((0x1<<crateNum) & crateMask){
          for (int i=0;i<16;i++){
            isetm[wt][crateNum][i] = ISETM_MIN;
          }
        }
      }
      for (int j=0;j<32;j++){
        lprintf("%d ",j);
        fflush(stdout);
        for (int crateNum=0;crateNum<20;crateNum++){
          if ((0x1<<crateNum) & crateMask){
            for (int i=0;i<16;i++){
              if ((0x1<<i) & slotMasks[crateNum]){
                xl3s[crateNum]->RW(PED_ENABLE_R + FEC_SEL*i + WRITE_REG,1<<j,&result);
              }
            }
            num_dacs = 0;
            for (int i=0;i<16;i++){
              if ((0x1<<i) & slotMasks[crateNum]){
                dac_nums[num_dacs] = d_isetm[ot];
                dac_values[num_dacs] = max_isetm[crateNum][i][j];
                slot_nums[num_dacs] = i;
                num_dacs++;
              }
            }
            error+= xl3s[crateNum]->MultiLoadsDac(num_dacs,dac_nums,dac_values,slot_nums);
          }
        }
        uint32_t crateNotDoneMask = crateMask;
        uint32_t notDoneMask[20];
        for (int crateNum=0;crateNum<20;crateNum++){
          if ((0x1<<crateNum) & crateMask){
            notDoneMask[crateNum] = slotMasks[crateNum];
          }
        }
        while (crateNotDoneMask){
          for (int crateNum=0;crateNum<20;crateNum++){
            if ((0x1<<crateNum) & crateMask){
              num_dacs = 0;
              for (int i=0;i<16;i++){
                if ((0x1<<i) & slotMasks[crateNum]){
                  dac_nums[num_dacs] = d_isetm[wt];
                  dac_values[num_dacs] = isetm[wt][crateNum][i];
                  slot_nums[num_dacs] = i;
                  num_dacs++;
                }
              }
              error+= xl3s[crateNum]->MultiLoadsDac(num_dacs,dac_nums,dac_values,slot_nums);
            }
          }
          uint16_t islonger[20];
          IsGTValidLonger(crateMask,notDoneMask,gtCutoff,islonger);
          for (int crateNum=0;crateNum<20;crateNum++){
            if ((0x1<<crateNum) & crateNotDoneMask){
              for (int i=0;i<16;i++){
                if ((0x1<<i) & notDoneMask[crateNum]){
                  if ((0x1<<i) & islonger[crateNum]){
                    isetm[wt][crateNum][i]++;
                    if (isetm[wt][crateNum][i] == 255)
                      notDoneMask[crateNum] &= ~(0x1<<i);
                  }
                  else{
                    notDoneMask[crateNum] &= ~(0x1<<i);
                  }
                }
              }
              if (notDoneMask[crateNum] == 0x0){
                crateNotDoneMask &= ~(0x1<<crateNum);
              }
            }
          }
        }
      }

      for (int j=0;j<32;j++){
        lprintf(".");
        fflush(stdout);
        for (int crateNum=0;crateNum<20;crateNum++){
          if ((0x1<<crateNum) & crateMask){
            for (int i=0;i<16;i++){
              if ((0x1<<i) & slotMasks[crateNum]){
                xl3s[crateNum]->RW(PED_ENABLE_R + FEC_SEL*i + WRITE_REG,1<<j,&result);
              }
            }
            num_dacs = 0;
            for (int i=0;i<16;i++){
              if ((0x1<<i) & slotMasks[crateNum]){
                dac_nums[num_dacs] = d_isetm[ot];
                dac_values[num_dacs] = max_isetm[crateNum][i][j];
                slot_nums[num_dacs] = i;
                num_dacs++;
              }
            }
            error+= xl3s[crateNum]->MultiLoadsDac(num_dacs,dac_nums,dac_values,slot_nums);
          }
        }
        uint32_t crateNotDoneMask = crateMask;
        uint32_t notDoneMask[20];
        for (int crateNum=0;crateNum<20;crateNum++){
          if ((0x1<<crateNum) & crateMask){
            notDoneMask[crateNum] = slotMasks[crateNum];
          }
        }
        while (crateNotDoneMask){
          for (int crateNum=0;crateNum<20;crateNum++){
            if ((0x1<<crateNum) & crateMask){
              num_dacs = 0;
              for (int i=0;i<16;i++){
                if ((0x1<<i) & slotMasks[crateNum]){
                  dac_nums[num_dacs] = d_isetm[wt];
                  dac_values[num_dacs] = isetm[wt][crateNum][i];
                  slot_nums[num_dacs] = i;
                  num_dacs++;
                }
              }
              error+= xl3s[crateNum]->MultiLoadsDac(num_dacs,dac_nums,dac_values,slot_nums);
            }
          }
          uint16_t islonger[20];
          IsGTValidLonger(crateMask,notDoneMask,gtCutoff,islonger);
          for (int crateNum=0;crateNum<20;crateNum++){
            if ((0x1<<crateNum) & crateNotDoneMask){
              for (int i=0;i<16;i++){
                if ((0x1<<i) & notDoneMask[crateNum]){
                  if ((0x1<<i) & islonger[crateNum]){
                    isetm[wt][crateNum][i]++;
                    printf("incremented again!\n");
                    if (isetm[wt][crateNum][i] == 255)
                      notDoneMask[crateNum] &= ~(0x1<<i);
                  }
                  else{
                    notDoneMask[crateNum] &= ~(0x1<<i);
                  }
                }
              }
              if (notDoneMask[crateNum] == 0x0){
                crateNotDoneMask &= ~(0x1<<crateNum);
              }
            }
          }
        }
      }
      printf("\n");
    } // end loop over tacs


    // we are done getting our dac values. lets measure and display the final gtvalids
    for (int crateNum=0;crateNum<20;crateNum++){
      if ((0x1<<crateNum) & crateMask){
        for (int i=0;i<16;i++){
          if ((0x1<<i) & slotMasks[crateNum]){
            if (!setOnly){
              for (int wt=0;wt<2;wt++){
                lprintf("\nMeasuring GTVALID for crate %d, slot %d, TAC %d\n",crateNum,i,wt);
                // loop over channel to measure inital GTVALID and find channel with max
                for (int j=0;j<32;j++){
                  if ((0x1<<j) & channelMask){
                    error+= xl3s[crateNum]->LoadsDac(d_isetm[0],isetm[0][crateNum][i],i);
                    error+= xl3s[crateNum]->LoadsDac(d_isetm[1],isetm[1][crateNum][i],i);
                    xl3s[crateNum]->RW(PED_ENABLE_R + FEC_SEL*i + WRITE_REG,1<<j,&result);
                    gtvalid_final[wt][j] = MeasureGTValid(crateNum,i,wt,max_gtvalid[crateNum][i][j],
                                                          max_isetm[crateNum][i][j]);
                  } // end if chan mask
                } // end loop over channels
                // find maximum gtvalid time
                gmax[wt] = 0.0;
                cmax[wt] = 0;
                for (int j=0;j<32;j++){
                  if ((0x1<<j) & channelMask){
                    if (gtvalid_final[wt][j] > gmax[wt]){
                      gmax[wt] = gtvalid_final[wt][j];
                      cmax[wt] = j;
                    }
                  }
                }
                // find minimum gtvalid time
                gmin[wt] = 9999.0;
                cmin[wt] = 0;
                for (int j=0;j<32;j++){
                  if ((0x1<<j) & channelMask){
                    if (gtvalid_final[wt][j] < gmin[wt]){
                      gmin[wt] = gtvalid_final[wt][j];
                      cmin[wt] = j;
                    }
                  }
                }
              } // end loop over tacs
            }
            // print out
            lprintf("\n--------------------------------------------------------\n");
            lprintf("Crate %d Slot %d - GTVALID FINAL results, time in ns:\n",crateNum,i);
            lprintf("--------------------------------------------------------\n");
            if (!twiddleOn)
              lprintf(" >>> ISETA0/1 = 0, no TAC twiddle bits set\n");
            lprintf("set up: VMAX: %hu, TACREF: %hu, ",VMAX,TACREF);
            if (twiddleOn)
              lprintf("ISETA: %hu\n",ISETA);
            else
              lprintf("ISETA: %hu\n",ISETA_NO_TWIDDLE);
            lprintf("Found ISETM0: %d, ISETM1: %d\n",isetm[0][crateNum][i],isetm[1][crateNum][i]);
            if (!setOnly){
              lprintf("Chan Tacbits GTValid 0/1:\n");
              for (int j=0;j<32;j++){
                if ((0x1<<j) & channelMask){
                  lprintf("%02d 0x%02x %4.1f %4.1f",
                      j,tacbits[crateNum][i][j],
                      gtvalid_final[0][j],gtvalid_final[1][j]);
                  if (isetm[0][crateNum][i] == ISETM_MIN || isetm[1][crateNum][i] == ISETM_MIN)
                    lprintf(">>> Warning: isetm not adjusted\n");
                  else
                    lprintf("\n");
                }
              }
              lprintf(">>> Maximum TAC0 GTValid - Chan %02d: %4.1f\n",
                  cmax[0],gmax[0]);
              lprintf(">>> Minimum TAC0 GTValid - Chan %02d: %4.1f\n",
                  cmin[0],gmin[0]);
              lprintf(">>> Maximum TAC1 GTValid - Chan %02d: %4.1f\n",
                  cmax[1],gmax[1]);
              lprintf(">>> Minimum TAC1 GTValid - Chan %02d: %4.1f\n",
                  cmin[1],gmin[1]);
            }

            int slot_errors;
            slot_errors = 0;
            if (abs(isetm[1][crateNum][i] - isetm[0][crateNum][i]) > 10)
              slot_errors |= 0x1;
            for (int j=0;j<32;j++){
              if ((gtvalid_final[0][j] < 0) || gtvalid_final[1][j] < 0)
                chan_errors[crateNum][i][j] = 1;
            }

            //store in DB
            if (updateDB || updateDetectorDB){
              lprintf("updating the database\n");
              JsonNode *newdoc = json_mkobject();
              json_append_member(newdoc,"type",json_mkstring("cmos_m_gtvalid"));

              json_append_member(newdoc,"vmax",json_mknumber((double)VMAX));
              json_append_member(newdoc,"tacref",json_mknumber((double)TACREF));

              JsonNode* isetm_new = json_mkarray();
              JsonNode* iseta_new = json_mkarray();
              json_append_element(isetm_new,json_mknumber((double)isetm[0][crateNum][i]));
              json_append_element(isetm_new,json_mknumber((double)isetm[1][crateNum][i]));
              if (twiddleOn){
                json_append_element(iseta_new,json_mknumber((double)ISETA));
                json_append_element(iseta_new,json_mknumber((double)ISETA));
              }else{
                json_append_element(iseta_new,json_mknumber((double)ISETA_NO_TWIDDLE));
                json_append_element(iseta_new,json_mknumber((double)ISETA_NO_TWIDDLE));
              }
              json_append_member(newdoc,"isetm",isetm_new);
              json_append_member(newdoc,"iseta",iseta_new);

              JsonNode* channels = json_mkarray();
              for (int j=0;j<32;j++){
                JsonNode *one_chan = json_mkobject();
                json_append_member(one_chan,"id",json_mknumber((double) j));
                json_append_member(one_chan,"tac_shift",json_mknumber((double) (tacbits[crateNum][i][j])));
                json_append_member(one_chan,"gtvalid0",json_mknumber((double) (gtvalid_final[0][j])));
                json_append_member(one_chan,"gtvalid1",json_mknumber((double) (gtvalid_final[1][j])));
                // GTValid lengths too long or too short, FIXME GTValid short is hard-coded
                if(gtvalid_final[0][j] > gtCutoff || gtvalid_final[1][j] > gtCutoff ||
                   gtvalid_final[0][j] < 300.0 || gtvalid_final[1][j] < 300.0){
                  chan_errors[crateNum][i][j] = 1;
                }
                json_append_member(one_chan,"errors",json_mkbool(chan_errors[crateNum][i][j]));
                if (chan_errors[crateNum][i][j])
                  slot_errors |= 0x2;
                json_append_element(channels,one_chan);
              }
              json_append_member(newdoc,"channels",channels);

              json_append_member(newdoc,"pass",json_mkbool(!(slot_errors)));
              json_append_member(newdoc,"slot_errors",json_mknumber(slot_errors));
              if (updateDetectorDB){
                lprintf("Pushing gtvalid information to detectorDB\n");
                PGconn* detectorDB = ConnectToDetectorDB();
                int error = LoadGTValidsToDetectorDB(newdoc, crateNum, i, "", detectorDB);
                if(error){
                  lprintf("Warning: Failure pushing gtvalid lengths to detectorDB for crate %d slot %d \n", crateNum, i);
                }
              }
              if (finalTest)
                json_append_member(newdoc,"final_test_id",json_mkstring(finalTestIDs[crateNum][i]));
              if (ecal)
                json_append_member(newdoc,"ecal_id",json_mkstring(ecalID));
              PostDebugDoc(crateNum,i,newdoc);
              json_delete(newdoc); // only delete the head
            }
            lprintf("******************************************\n");

          } // end in slotmask
        } // end loop over slots
      }
    } // end loop over crates
  } // end try
  catch(const char* s){
    lprintf("GTValidTest: %s\n",s);
  }
  return 0;
}

// returns 1 if gtvalid is longer than time, 0 otherwise.
// if gtvalid is longer should get hits generated from all the pedestals
void IsGTValidLonger(uint32_t crateMask, uint32_t *slotMasks, float time, uint16_t* islonger)
{
  usleep(5000);
  uint32_t result;
  for (int i=0;i<20;i++)
    islonger[i] = 0x0;

  // reset fifo
  for (int crateNum=0;crateNum<20;crateNum++){
    if ((0x1<<crateNum) & crateMask){
      for (int slotNum=0;slotNum<16;slotNum++){
        if ((0x1<<slotNum) & slotMasks[crateNum]){
          xl3s[crateNum]->RW(GENERAL_CSR_R + FEC_SEL*slotNum + WRITE_REG,0x2,&result);
          xl3s[crateNum]->RW(GENERAL_CSR_R + FEC_SEL*slotNum + WRITE_REG,0x0,&result);
        }
      }
    }
  }
  mtc->SetGTDelay(time+GTPED_DELAY+TDELAY_EXTRA); 

  mtc->MultiSoftGT(NGTVALID);
  usleep(5000);

  for (int crateNum=0;crateNum<20;crateNum++){
    if ((0x1<<crateNum) & crateMask){
      for (int slotNum=0;slotNum<16;slotNum++){
        if ((0x1<<slotNum) & slotMasks[crateNum]){
          xl3s[crateNum]->RW(FIFO_WRITE_PTR_R + FEC_SEL*slotNum + READ_REG,0x0,&result);

          int num_read = (result & 0x000FFFFF)/3;
          // Hard-coded FECD slot deals with the fact that we pipe the GT into channel 23.
          // If we stop doing that, this will break
          if(crateNum == 17 && slotNum == 15){
            if (num_read >= (2*NGTVALID)*0.75){
              islonger[crateNum] |= (0x1<<slotNum);
            }
          }
          else{
            if (num_read >= (NGTVALID)*0.75){
              islonger[crateNum] |= (0x1<<slotNum);
            }
          }
        }
      }
    }
  }
}

// Measure skipped with setOnly flag
float MeasureGTValid(int crateNum, int slotNum, int tac, float max_gtvalid, uint32_t max_isetm)
{
  lprintf(".");
  fflush(stdout);

  uint32_t result;
  float upper_limit = max_gtvalid;
  float lower_limit = GTMIN;
  float current_delay;
  uint32_t select_reg = FEC_SEL*slotNum;

  // set unmeasured TAC gtvalid to maximum so the tac we are looking at will fail first
  int othertac = (tac+1)%2;
  int error = xl3s[crateNum]->LoadsDac(d_isetm[othertac],max_isetm,slotNum);

  uint32_t slotMasks[20];
  for (int i=0;i<20;i++)
    slotMasks[i] = (0x1<<slotNum);

  while (1){
    // binary search for gtvalid
    current_delay = (upper_limit - lower_limit)/2.0 + lower_limit;
    uint16_t islonger[20];
    IsGTValidLonger((0x1<<crateNum),slotMasks,current_delay,islonger);
    if (islonger[crateNum])
      lower_limit = current_delay;
    else
      upper_limit = current_delay;

    if (upper_limit - lower_limit <= 1){
      break;
    }
  }


  // check if we actually found a good value or
  // if we just kept going to to the upper bound
  if (upper_limit == max_gtvalid){
    return -2;
  }else{
    // ok we know that lower limit is within the window, upper limit is outside
    // lets make sure its the right TAC failing by making the window longer and
    // seeing if the events show back up
    error = xl3s[crateNum]->LoadsDac(d_isetm[tac],max_isetm,slotNum);
    uint16_t islonger[20];
    
    IsGTValidLonger((0x1<<crateNum),slotMasks,upper_limit,islonger);
    if (islonger[crateNum]){
      return upper_limit;
    }else{
      lprintf("Uh oh, still not all the events! wrong TAC failing\n");
      return -1;
    }
  }
}


