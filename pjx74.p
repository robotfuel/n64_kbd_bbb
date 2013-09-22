.origin 0
.entrypoint START

//Jumos to label via r20 if dst and src are not equal
.macro JNEQ
    .mparam lbl,dst,src
        QBNE    JUMP1, dst, src
        JMP     JUMP0
        JUMP1:
            JAL r20, lbl
        JUMP0:
.endm


#include "pjx74.hp"

#define GPIO1 0x4804c000
#define G_IO_CONFIG 0x134
#define G_SET_LO 0x190
#define G_SET_HI 0x194
#define G_DATAIN 0x138
#define G_DATAOUT 0x13c
#define SLEEP_COUNTER_1 0x00000040 // used to sleep 1 us
#define SLEEP_COUNTER_2 0x00000020 // used to sleep 0.5 us
#define SLEEP_COUNTER_3 0x00000010 // used to sleep 0.25 us
#define HILOW_COUNTER 0x00000019

//General Registers are r1 through r29. Use r20 through r29 for Jump and Link

START:
    //NORMAL:
    // Enable OCP master port
    LBCO    r0, CONST_PRUCFG, 4, 4
    CLR     r0, r0, 4         // Clear SYSCFG[STANDBY_INIT] to enable OCP master port
    SBCO    r0, CONST_PRUCFG, 4, 4

    // Configure the programmable pointer register for PRU0 by setting c28_pointer[15:0]
    // field to 0x0120.  This will make C28 point to 0x00012000 (PRU shared RAM).
    MOV     r0, 0x00000120
    MOV     r1, CTPPR_0
    ST32    r0, r1

    // Configure the programmable pointer register for PRU0 by setting c31_pointer[15:0]
    // field to 0x0010.  This will make C31 point to 0x80001000 (DDR memory).
    MOV     r0, 0x00100000
    MOV     r1, CTPPR_1
    ST32    r0, r1

    //Load values from external DDR Memory into Registers r20/r21/r22
    //LBCO      r20, CONST_DDR, 0, 12

    //Store values from read from the DDR memory into PRU shared RAM
    //SBCO      r20, CONST_PRUSHAREDRAM, 0, 12

//USED REGISTERS
//r20 - return register 1
//r21 - return register 2
//r22 - return register 3

//r2 - Read Addr
//r3 - IO addr
//r4 - Set Hi Addr
//r5 - Set Lo Addr
//r6 - PIN to set
//r17 - Sleep counter

//R8 on is allowed...


 //INIT:
    MOV r2, GPIO1 | G_DATAIN
    MOV r3, GPIO1 | G_IO_CONFIG
    MOV r4, GPIO1 | G_SET_HI
    MOV r5, GPIO1 | G_SET_LO
    MOV r6, 1<<12
    MOV r7, 1<<31
    LDI       r17, 0

// Shared memory
// 0-3      flag for keep going or exit
// 4-7      state
// 8-11     status
// 12-15    null
// 16-19    PRU exited flag

//==============================================================================================
//================================== * READING FROM THE N64 * ==================================
//==============================================================================================
//any jumps must be with r22 or higher. Available registers r9 and above
READ_N64: //MSB comes first (TODO: Add escape sequence in case line is unpluged)
                                                //-Set pin mode to input
    LBBO    r9, r3, 0, 4                        //  Copy GPOI config into r9
    SET     r9, r9, 12                          //  Set bit 12 in r9 to 1
    SBBO    r9, r3, 0, 4                        //  Copy r9 to GPIO config

                                                //-Init variables
    LDI     r12, 8                              //  bit counter
    LDI     r9, 0                               //  mybit


    //wait for line to drop
    WAIT_FOR_DROP:
                                                //-Determine if we should exit:
        LBCO    r8, CONST_PRUSHAREDRAM, 0, 4    //  Load byte from shared ARM memory [0,1,2,3]
        QBEQ    EXIT, r8, 0                     //  exit if exit flag is 0
                                                //-Check line value
        LBBO    r13, r2, 0, 4                   //  Copy GPOI Read value into r13
        QBBS    WAIT_FOR_DROP, r13, 12          //  If bit 12 in r13 is 1, Keep looking


    READ_LOOP:
        SUB r12,r12,1                           //-Keep track of bit counter
                                                //-Init variables
        LDI   r10, 0                            //  lo counter
        LDI   r11, 0                            //  hi counter


    COUNT_LOWS:                                 //-Line dropped:
        ADD     r10, r10, 1                      //  While line dropped, lo counter ++
        LBBO    r13, r2, 0, 4                   //  Copy GPOI Read value into r13
                                                //-Determine if we should exit:
        LBCO    r8, CONST_PRUSHAREDRAM, 0, 4    //  Load byte from shared ARM memory [0,1,2,3]
        QBEQ    EXIT, r8, 0                     //  exit if exit flag is 0

        QBBC    COUNT_LOWS, r13, 12             //  If bit 12 in r13 is 1, Keep counting

    COUNT_HIS:                                  //-Line raised:
        LBCO    r8, CONST_PRUSHAREDRAM, 0, 4    //  Load byte from shared ARM memory [0,1,2,3]
        QBEQ    EXIT, r8, 0                     //  exit if exit flag is 0
        
        ADD     r11,r11, 1                      //  While line raised, hi counter ++
        LBBO    r13, r2, 0, 4                   //  Copy GPOI Read value into r13
        QBBS    COUNT_HIS, r13, 12              //  If bit 12 in r13 is 1, Keep counting

                                                //-Line Dropped again: (can be next bit or stop bit)
    QBGT    SETBIT1, r10,r11                    //  Set bit 1 if we had more highs than lows (r11 > r10)
    JMP     SETBIT0                             //  Set bit 0 if we had more lows than highs

    SETBIT1:                                    //-Set bit to 1
        MOV     r17, r10                        //--There were less lows, so set the sleep counter accordingly
        SET     r9,r9,r12                       //  Set bit r12 in r9 to 1; (7,6,5,4,3,2,1,0)
        JMP     CHECK_STUFF

    SETBIT0:                                    //-Set bit to 0
        MOV     r17, r11                        //--There were less highs, so set the sleep counter accordingly
        NOP0    r0,r0,r0                        //  Set bit r12 in r9 to 0; Do a NOP to have equal time in SETBIT
        JMP     CHECK_STUFF

    CHECK_STUFF:                                //-Check to see if we got all 8 bits.
        QBNE    READ_LOOP, r12, 0

                                                //-Respond to the request:
                                                //  -Set ourselves to output
    LBBO    r10, r3, 0, 4                       //      Copy GPOI config into r9
    CLR     r10, r10, 12                        //      Set bit 12 in r9 to 0
    SBBO    r10, r3, 0, 4                       //      Copy r9 to GPIO config

    SBCO    r9, CONST_PRUSHAREDRAM, 16, 4      //  -Store req value to shared ARM memory [16,17,18,19], Uint 4 in 0-4

    JAL     r20, SLEEP_1_00US                   //  Sleep
    JAL     r20, SLEEP_1_00US                   //  Sleep
    JAL     r20, SLEEP_1_00US                   //  Sleep
    JAL     r20, SLEEP_1_00US                   //  Sleep

    JAL     r20, SLEEP_1_00US                   //  Sleep
    JAL     r20, SLEEP_1_00US                   //  Sleep
    JAL     r20, SLEEP_1_00US                   //  Sleep
    JAL     r20, SLEEP_1_00US                   //  Sleep

    QBEQ    SEND_STATUS, r9, 0x00              //  If N64 req = 0, send status
    QBEQ    SEND_STATE, r9, 0x01               //  If N64 req = 1, send state
    QBEQ    SEND_STATUS, r9, 0xFF              //  If N64 req = ff, send status

    JMP     READ_N64                                //-Start over if we got weird ass values




//Hi, lo use 8
//==============================================================================================
//================================== * SENDING TO THE N64 * ====================================
//==============================================================================================
SEND_STATE:
    LBCO    r10, CONST_PRUSHAREDRAM, 4, 4        //Load byte from shared ARM memory [4,5,6,7]
    LDI     r9, 32                               //init loop counter
    LDI     r20, #RETURN1                        //put counter of RETURN1 in r20
    SLOOP1:
        SUB     r9,r9,1
        QBBS    SEND1, r10, r9                   //If bit r9 in r10 is set, send 1, ret = RETURN1
        QBBC    SEND0, r10, r9                   //Else send 0, ret to r20 = RETURN1
        RETURN1:
        QBNE    SLOOP1, r9, 0

    JAL     r20, SEND1                          //Send stop bit
    JMP     READ_N64                                //Go back to start


SEND_STATUS:
    LBCO    r10, CONST_PRUSHAREDRAM, 8, 4        //Load byte from shared ARM memory [4,5,6,7]
    LDI     r9, 24                               //init loop counter
    LDI     r20, #RETURN2                        //put counter of RETURN1 in r20
    SLOOP2:
        SUB     r9,r9, 1
        QBBS    SEND1, r10, r9                   //If bit r9 in r10 is set, send 1, ret = RETURN1
        QBBC    SEND0, r10, r9                   //Else send 0, ret to r20 = RETURN1
        RETURN2:
        QBNE    SLOOP2, r9, 0                   //loop 24 times

    JAL     r20, SEND1                          //Send stop bit
    JMP     READ_N64                                //Go back to start


//======================================SEND 1 -return to r20
SEND1:
    JAL     r21, SETLO_1US
    JAL     r21, SETHI_1US
    JAL     r21, SETHI_1US
    JAL     r21, SETHI_1US
    JMP     r20

//======================================SEND 0
SEND0:
    JAL     r21, SETLO_1US
    JAL     r21, SETLO_1US
    JAL     r21, SETLO_1US
    JAL     r21, SETHI_1US
    JMP     r20

//======================================SET LINE LO
SETLO_1US:

    MOV     r8, r17
    LSL     r8,r8,2
    LOOPLO:
        SBBO    r6, r5, 0, 4       //Set low
        SUB     r8,r8,1
        QBNE    LOOPLO, r8, 0

    JMP     r21

//======================================SET LINE HI

SETHI_1US:
    MOV     r8, r17
    LSL     r8,r8,2                 //cause we do 4x less ops here than when we go the counter
    LOOPHI:
        SBBO    r6, r4, 0, 4       //Set hi
        SUB     r8,r8,1
        QBNE    LOOPHI, r8, 0

    JMP r21

//======================================SLEEP 1us

SLEEP_1_00US:
    MOV     r8, SLEEP_COUNTER_1
    LOOPSLP1:
        //Add or remove nops here to get a good balance
        NOP0    r8,r8,r8
        SUB     r8,r8,1
        QBNE    LOOPSLP1, r8, 0

        JMP r20

//======================================EXIT
EXIT:
                // Send notification to Host for program completion
                MOV r31.b0, PRU0_ARM_INTERRUPT+16
                HALT  // Halt the processor
                                                                                                                                                                                                 
