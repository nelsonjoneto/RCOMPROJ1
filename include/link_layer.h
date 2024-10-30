// Link layer header.
// NOTE: This file must not be changed.

#ifndef _LINK_LAYER_H_
#define _LINK_LAYER_H_

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <signal.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>


#define FLAG 0x7E
#define A_ER 0x03
#define A_RE 0x01

#define C_SET 0x03
#define C_DISC 0x0B
#define C_UA 0x07

#define C_N(n) (n << 7)
#define C_RR(n) (0xAA + n)
#define C_REJ(n) (0x54 + n)

#define BCC(a,c) (a ^ c)
#define ESC 0x7D

#define ROLE_TX 0
#define ROLE_RX 1


typedef enum
{
    LlTx,
    LlRx,
} LinkLayerRole;

typedef enum
{
    START,
    FLAG_RCV,
    A_RCV,
    C_RCV,
    BCC1_OK,
    BCC2_OK,
    STOP,
    STUFFED_BYTE,
    READ_DATA,
    DISCONNECTED
} LinkLayerState;

typedef struct
{
    char serialPort[50];
    LinkLayerRole role;
    int baudRate;
    int nRetransmissions;
    int timeout;
} LinkLayer;

extern LinkLayerState linkLayerState; 

// SIZE of maximum acceptable payload.
// Maximum number of bytes that application layer should send to link layer
#define MAX_PAYLOAD_SIZE 1000

// MISC
#define FALSE 0
#define TRUE 1

// Open a connection using the "port" parameters defined in struct linkLayer.
// Return "1" on success or "-1" on error.
int llopen(LinkLayer connectionParameters);

// Send data in buf with size bufSize.
// Return number of chars written, or "-1" on error.
int llwrite(const unsigned char *buf, int bufSize);

// Receive data in packet.
// Return number of chars read, or "-1" on error.
int llread(unsigned char *packet);

// Close previously opened connection.
// if showStatistics == TRUE, link layer should print statistics in the console on close.
// Return "1" on success or "-1" on error.
int llclose(int showStatistics);

/*void alarmHandler(int signal);

unsigned char readSupervisionFrame(unsigned char a, unsigned char *cValues, int cValuesCount);

int sendFrameS (unsigned char a, unsigned char c);

unsigned char *byteStuffing(const unsigned char *buf, int bufSize, int *newSize);

void readSupervisionFrameRx(unsigned char a, unsigned char *cValues, int cValuesCount);

void sendSuperVisionFrameAndReadReply(unsigned char a, unsigned char c, unsigned char *cValues, int cValuesCount);*/

#endif // _LINK_LAYER_H_
