/********
 * Klondike ASIC Miner - klondike.c - cmd processing and host protocol support 
 * 
 * (C) Copyright 2013 Chris Savery. 
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
#include "GenericTypeDefs.h"
#include "Compiler.h"
#include <xc.h>
#include "klondike.h"

const IDENTITY ID = { 0x10, "K16", 0xDEADBEEF };
//const char FwPwd[] = FWPWD;

DWORD BankRanges[8] = { 0, 0x40000000, 0x2aaaaaaa, 0x20000000, 0x19999999, 0x15555555, 0x12492492, 0x10000000 };
BYTE WorkNow, BankSize, ResultQC, SlowTick, VerySlowTick, TimeOut, TempTarget, LastTemp, FanLevel;
BYTE SlaveAddress = MASTER_ADDRESS;
BYTE HashTime;
volatile WORKSTATUS Status = {'I',0,0,0,0,0,0,0,0, WORK_TICKS, 0 };
WORKCFG Cfg = { DEFAULT_HASHCLOCK, DEFAULT_TEMP_TARGET, 0, 0, 0 };
WORKTASK WorkQue[MAX_WORK_COUNT];
volatile BYTE ResultQue[6];
DWORD CLOCK_LOW_CHG;
DWORD ClockCfg[2];

INT16 Step, Error, LastError;

DWORD NonceRanges[8];

extern I2CSTATE I2CState;
extern DWORD PrecalcHashes[6];

void ProcessCmd(char *cmd)
{
    // cmd is one char, dest address 1 byte, data follows
    // we already know address is ours here
    switch(cmd[0]) {
        case 'W': // queue new work
            if( Status.WorkQC < MAX_WORK_COUNT-1 ) {
                WorkQue[ (WorkNow + Status.WorkQC++) & WORKMASK ] = *(WORKTASK *)(cmd+2);
                if(Status.State == 'R') {
                    AsicPushWork();
                }
            }
            SendCmdReply(cmd, (char *)&Status, sizeof(Status));
            Status.Noise = Status.ErrorCount = 0;
            break;
        case 'A': // abort work, reply status has hash completed count
            Status.WorkQC = WorkNow = 0;
            Status.State = 'R';
            SendCmdReply(cmd, (char *)&Status, sizeof(Status));
            Status.Noise = Status.ErrorCount = 0;
            break;
        case 'I': // return identity 
            SendCmdReply(cmd, (char *)&ID, sizeof(ID));
            break;
        case 'S': // return status 
            SendCmdReply(cmd, (char *)&Status, sizeof(Status));
            Status.Noise = Status.ErrorCount = 0;
            break;
        case 'C': // set config values 
            if( *(WORD *)&cmd[2] != 0 ) {
                Cfg = *(WORKCFG *)(cmd+2);
                if(Cfg.HashClock < MIN_HASH_CLOCK)
                    Cfg.HashClock = MIN_HASH_CLOCK;
                if(Cfg.HashClock > MAX_HASH_CLOCK)
                    Cfg.HashClock = MAX_HASH_CLOCK;
                UpdateClock(Cfg.HashClock);
                if(Cfg.TempTarget != 0)
                    TempTarget = Cfg.TempTarget;
                else
                    Cfg.TempTarget = DEFAULT_TEMP_TARGET;
            }
            SendCmdReply(cmd, (char *)&Cfg, sizeof(Cfg));
            break;
        case 'E': // enable/disable work
            HASH_CLK_EN = (cmd[2] == '1');
            Status.State = (cmd[2] == '1') ? 'R' : 'D';
            SendCmdReply(cmd, (char *)&Status, sizeof(Status));
            Status.Noise = Status.ErrorCount = 0;
            break;
        //case 'F': // enter firmware update mode
        //    for(BYTE n = 0; n < sizeof(FwPwd); n++)
	//	if(FwPwd[n] != cmd[2+n])
        //            return;
        //    UpdateFirmware();
        //    break;
        default:
            break;
        }
    LED_On();
}

void AsicPushWork(void)
{
    AsicPreCalc(&WorkQue[WorkNow]);
    Status.WorkID = WorkQue[WorkNow].WorkID;
    SendAsicData(&WorkQue[WorkNow]);
    WorkNow = (WorkNow+1) & WORKMASK;
    Status.HashCount = 0;
    TMR0 = HashTime;
    Status.State = 'W';
    Status.WorkQC--;
}

void DetectAsics(void)
{
    Status.ChipCount = 10;

    // pre-calc nonce range values
    BankSize = (Status.ChipCount)/2;
    Status.MaxCount = WORK_TICKS / BankSize / 2;

    NonceRanges[0] = 0;
    for(BYTE x = 1; x < BankSize; x++)
        NonceRanges[x] = NonceRanges[x-1] + BankRanges[BankSize-1];

    Status.State = 'R';
    Status.HashCount = 0;
}

// ISR functions
void WorkTick(void)
{
    TMR0 += HashTime;
    TMR0IF = 0;

    if((Status.State == 'W') && (++Status.HashCount == Status.MaxCount)) {
        if(Status.WorkQC > 0) {
            Status.State = 'P';
        }
        else {
            Status.State = 'R';
        }
    }

    if(++SlowTick == 0) {
        LED_Off();
        Status.Temp = ADRESH;
        ADCON0bits.GO_nDONE = 1;
        if((++VerySlowTick % 15) == 0) {
            UpdateFanLevel();
        }
        //CheckFanSpeed();
    }
   //if((SlowTick & 3) == 0)
   //I2CPoll();
}

void ResultRx(void)
{
    TimeOut = 0;

    while(ResultQC < 4) {
        if(RCIF) {
            ResultQue[2+ResultQC++] = RCREG;
            TimeOut = 0;
        }
        if(TimeOut++ > 32) {
            Status.Noise++;
            goto outrx;
        }
        if(RCSTAbits.OERR) { // error occured, either overun or no more bits
            if(Status.State == 'W') {
                Status.ErrorCount++;
            }
            goto outrx;
        }
    }
    
    if(Status.State == 'W') {
        ResultQue[0] = '=';
        ResultQue[1] = Status.WorkID;
        SendCmdReply(&ResultQue, &ResultQue+1, sizeof(ResultQue)-1);
    }

outrx:
    RESET_RX();
    ResultQC = 0;
    IOCBF = 0;
}

// Housekeeping functons
// function that adjusts fan speed according to target and current temperatures
void UpdateFanLevel(void)
{
    Error = Status.Temp - TempTarget;
    Step = 1*Error - 0.5*LastError - 8*(LastTemp - Status.Temp);

    if(FanLevel + Step > 75 && FanLevel + Step <= 255) {
        FanLevel += Step;
    }
    else if(FanLevel + Step > 255) {
        FanLevel = 255;
    }
    else if(FanLevel + Step < 75) {
        FanLevel = 75;
    }

    LastTemp = Status.Temp;
    LastError = Error;
    // this is just temporary - fan speed should be measured RPM of a fan (will change it when it will be functional)
    PWM1DCH = Status.FanSpeed = FanLevel;
}

// Init functions
void InitFAN(void)
{
    FAN_TRIS = 1;
    PWM1CON = 0;
    PR2 = 0xFF;
    PWM1CON = 0xC0;
    PWM1DCH = FanLevel = DEFAULT_FAN_TARGET;
    PWM1DCL = 0;
    TMR2IF = 0;
    T2CONbits.T2CKPS = 1;
    TMR2ON = 1;
    FAN_TRIS = 0;
    PWM1OE=1;

    // for Fan Tach reading
    //T1GSEL = 1;
    //IOCAN3 = 1;
    //IOCAF3 = 0;
}

void InitTempSensor(void)
{ 
    THERM_TRIS=1;
    //TEMP_INIT;
    //FVREN = 1;
    ADCON0bits.CHS = TEMP_THERMISTOR;
    ADCON0bits.ADON = 1;
    ADCON1bits.ADFM = 0;
    ADCON1bits.ADCS = 6;
    ADCON1bits.ADPREF = 0;
    ADCON2bits.TRIGSEL = 0;
    TempTarget = DEFAULT_TEMP_TARGET;
}

void InitWorkTick(void)
{
    UpdateClock(DEFAULT_HASHCLOCK);
    TMR0CS = 0;
    OPTION_REGbits.PSA = 0;
    OPTION_REGbits.PS = 7;
    TMR0 = HashTime;

    HASH_TRIS_0P = 0;
    HASH_TRIS_0N = 0;
    HASH_TRIS_1P = 0;
    HASH_TRIS_1N = 0;
    HASH_IDLE();
    HASH_CLK_TRIS = 0;
    HASH_CLK_EN = 0;
}

void InitResultRx(void)
{
    ResultQC = 0;
    TXSTAbits.SYNC = 1;
    RCSTAbits.SPEN = 1;
    TXSTAbits.CSRC = 0;
    BAUDCONbits.SCKP = 0;
    BAUDCONbits.BRG16 = 1;
    ANSELBbits.ANSB5 = 0;
    IOCBPbits.IOCBP7 = 1;
    INTCONbits.IOCIE = 1;
    IOCBF = 0;
    INTCONbits.GIE = 1;
    RCSTAbits.CREN = 1;
    RCREG = 0xFF;
}

void UpdateClock(DWORD SPEED)
{
    DWORD CLOCK_R_VALUE, CLOCK_F_VALUE, CLOCK_OD_VALUE;

/* removed due to flash usage on demo version of XC8
    if (SPEED >= 62 && SPEED <= 125)
    {
        CLOCK_R_VALUE = 2;
        CLOCK_F_VALUE = (SPEED + 0.78125) / 1.5625;
        CLOCK_OD_VALUE = 3;
    }

    else if (SPEED > 125 && SPEED <= 250)
    {
        CLOCK_R_VALUE = 2;
        CLOCK_F_VALUE = (SPEED + 1.5625) / 3.125;
        CLOCK_OD_VALUE = 2;
    }

    else */if (SPEED > 250 && SPEED <= 500)
    {
        CLOCK_R_VALUE = 2;
        CLOCK_F_VALUE = (SPEED + 3.125) / 6.25;
        CLOCK_OD_VALUE = 1;
    }

    else if (SPEED > 500)
    {
        CLOCK_R_VALUE = 2;
        CLOCK_F_VALUE = (SPEED + 6.25) / 12.5;
        CLOCK_OD_VALUE = 0;
    }

    // Report real speed back to mining software
    Cfg.HashClock = 25 * CLOCK_F_VALUE / CLOCK_R_VALUE / (1 << CLOCK_OD_VALUE);

    CLOCK_LOW_CHG = 0b00000000000000000000000001000111 | ((CLOCK_R_VALUE & 0b11111) << 16) | ((CLOCK_F_VALUE & 0b1111111) << 21) | ((CLOCK_OD_VALUE & 0b11) << 28);

    ClockCfg[0] = CLOCK_LOW_CHG;
    ClockCfg[1] = CLOCK_HIGH_CFG;
    HashTime = 256 - ((WORD)TICK_TOTAL/Cfg.HashClock);

// for testing: use this for direct-control of timeout-value controlled by cgminer (via temperature value)
//    HashTime = Cfg.TempTarget;
}

