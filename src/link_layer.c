// Link layer protocol implementation

#include "link_layer.h"
#include "serial_port.h"

#define _POSIX_SOURCE 1

////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////

int nRetransmissions, timeout = 0;
int alarmPlaying = FALSE;

unsigned char tramaTx = 0;
unsigned char tramaRx = 1;

LinkLayerRole role;

int totalRetransmissions = 0;


int llopen(LinkLayer connectionParameters)
{
    int serialPortFd  = openSerialPort(connectionParameters.serialPort, connectionParameters.baudRate);
    if (serialPortFd < 0) return -1;

    LinkLayerState linkLayerState = START;

    nRetransmissions = connectionParameters.nRetransmissions;
    timeout = connectionParameters.timeout;

    int retransmissions = 0;

    role = connectionParameters.role;


    switch (connectionParameters.role)
    {
    case LlTx:
        unsigned char cValuesTx[] = {C_UA};
        (void) signal(SIGALRM, alarmHandler);
        while (linkLayerState != STOP && retransmissions < nRetransmissions) {
            sendFrameS(A_ER, C_SET);
            alarm(connectionParameters.timeout);
            alarmPlaying = FALSE;

            while (!alarmPlaying && linkLayerState != STOP) {
                readSupervisionFrame(&linkLayerState, A_ER, cValuesTx, 1);

            }
            if (alarmPlaying) {
                retransmissions++;
            }
        }
        totalRetransmissions += retransmissions;
        if (linkLayerState != STOP) return -1;
        break;

    case LlRx:
        unsigned char cValuesRx[] = {C_SET};
        readSupervisionFrameRx(&linkLayerState, A_ER, cValuesRx, 1);
        sendFrameS(A_ER,C_UA);
        break;
    default:
        return -1;
        break;
    }


    return serialPortFd;
}

////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////
int llwrite(const unsigned char *buf, int bufSize)
{

    int frameSize = 2 * bufSize + 7;
    unsigned char *informationFrame = (unsigned char *)malloc(frameSize);
    if (!informationFrame) {
        printf("Couldn't allocate memory for the information frame\n");
        return -1; 
    }

    informationFrame[0] = FLAG;
    informationFrame[1] = A_ER;
    informationFrame[2] = C_N(tramaTx);
    informationFrame[3] = BCC(informationFrame[1], informationFrame[2]);

  
    int stuffedDataSize;
    unsigned char *stuffedBuf = byteStuffing(buf, bufSize, &stuffedDataSize);
    if (!stuffedBuf) {
        free(informationFrame);
        return -1;
    }

    memcpy(informationFrame + 4, stuffedBuf, stuffedDataSize);

 
    unsigned char bcc2 = buf[0];
    for (unsigned int i = 1; i < bufSize; i++) {
        bcc2 ^= buf[i];
    }


    int stuffedBBC2Size;
    unsigned char *stuffedBCC2 = byteStuffing(&bcc2, 1, &stuffedBBC2Size);
    if (!stuffedBCC2) {
        free(stuffedBuf);
        free(informationFrame);
        return -1;
    }

    memcpy(informationFrame + 4 + stuffedDataSize, stuffedBCC2, stuffedBBC2Size);
    informationFrame[4 + stuffedDataSize + stuffedBBC2Size] = FLAG;

    frameSize = 5 + stuffedDataSize + stuffedBBC2Size;

    free(stuffedBuf);
    free(stuffedBCC2);

    int retransmissions = 0;
    

    LinkLayerState linkLayerState = START;
    unsigned char cValues[] = {C_RR(0), C_RR(1), C_REJ(0), C_REJ(1), C_DISC};
    unsigned char c = 0;
    while (retransmissions < nRetransmissions) {
        alarm(timeout);
        alarmPlaying = FALSE;
               
        if (writeBytes(informationFrame, frameSize) < 0) {
            free(informationFrame);
            return -1;
        } else printf ("Frame sent\n");
        
        while (!alarmPlaying) {
            unsigned char c = readSupervisionFrame(&linkLayerState, A_ER, cValues, 5);
            if (c == C_REJ(0) || c == C_REJ(1)) {
                break;
            } else if (c == C_RR(0) || c == C_RR(1)) { 
                tramaTx = (tramaTx == 0) ? 1 : 0; 

                totalRetransmissions += retransmissions;
                free(informationFrame);
                return frameSize;

            } else continue;
        }

        retransmissions++;

    }
    totalRetransmissions += retransmissions;
    
    free(informationFrame);
    return -1;
}

unsigned char currentFrameNum = 0;

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
int llread(unsigned char *packet)
{
    LinkLayerState linkLayerState;
    unsigned char byteRead;
    unsigned char c;
    int i = 0;
    linkLayerState = START;


    while (linkLayerState != STOP) {
        int nrBytesRead = readByte(&byteRead);
        if (nrBytesRead < 0) {
            break;
        }
        else if (nrBytesRead > 0) {
            switch (linkLayerState) {
                case START:
                    if (byteRead == FLAG) linkLayerState = FLAG_RCV;
                    break;
                case FLAG_RCV:
                    if (byteRead == A_ER) linkLayerState = A_RCV;
                    else if (byteRead != FLAG) linkLayerState = START;
                    break;
                case A_RCV:
                    if (byteRead == C_N(0) || byteRead == C_N(1)) {
                        linkLayerState = C_RCV;
                        c = byteRead;
                    } else if (byteRead == FLAG) {
                        linkLayerState = FLAG_RCV;
                    } else {
                        linkLayerState = START;
                    }
                    break;
                case C_RCV:
                    if (byteRead == (A_ER ^ c)) linkLayerState = READ_DATA;
                    else if (byteRead == FLAG) linkLayerState = FLAG_RCV;
                    else linkLayerState = START;
                    break;
                case READ_DATA:
                    if (byteRead == ESC) {
                        linkLayerState = STUFFED_BYTE;
                    } else if (byteRead == FLAG) {
                        unsigned char bcc2 = packet[i-1];
                        i--;
                        packet[i] = '\0';
                        unsigned char acc = packet[0];

                        for (unsigned int j = 1; j < i; j++)
                            acc ^= packet[j];
                        

                        if (bcc2 == acc){
                            if (C_N(currentFrameNum) == c) {
                                currentFrameNum = (currentFrameNum == 0) ? 1 : 0;
                                sendFrameS(A_ER, C_RR(tramaRx));
                                tramaRx = (tramaRx == 0) ? 1 : 0;
                                return i;
                            } else {
                                sendFrameS(A_ER, C_RR(currentFrameNum));
                                return 0;
                            }

                        } else{
                            sendFrameS(A_ER, C_REJ(tramaRx));
                            return -1;
                        }

                    } else {
                        packet[i++] = byteRead;

                    }
                    break;
                case STUFFED_BYTE:
                    packet[i++] = byteRead ^ 0x20;
                    linkLayerState = READ_DATA;
                    break;
                default:
                    break;
            }
        }

    }

    return -1;
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llclose(int showStatistics)
{   
    (void) signal(SIGALRM, alarmHandler);
    LinkLayerState linkLayerState = START;
    unsigned char cValues[1] = {C_DISC};

    int retransmissions = 0;

    switch (role)
    {
    case LlTx:
        while (retransmissions < nRetransmissions && linkLayerState != STOP) {
            sendFrameS(A_ER, C_DISC);
            alarm(timeout);
            alarmPlaying = FALSE;

            while (!alarmPlaying && linkLayerState != STOP) {
                readSupervisionFrame(&linkLayerState, A_RE, cValues, 1);
            }

            if (alarmPlaying) {
                retransmissions++;
            }
        }
        if (linkLayerState != STOP) return -1;
        sendFrameS(A_RE, C_UA);

        break;
    case LlRx:
        while (linkLayerState != STOP) {
            readSupervisionFrame(&linkLayerState, A_ER, cValues, 1);
        }
        cValues[0] = C_UA;
        retransmissions = 0;
        linkLayerState = START;
        while (retransmissions < nRetransmissions && linkLayerState != STOP) {
            sendFrameS(A_RE, C_DISC);
            alarm(timeout);
            alarmPlaying = FALSE;

            while (!alarmPlaying && linkLayerState != STOP) {
                readSupervisionFrame(&linkLayerState, A_RE, cValues, 1);
            }

            if (alarmPlaying) {
                retransmissions++;
            }
        }

        if (linkLayerState != STOP) return -1;
        break;
    
    default:
        return -1;
    }

    
    int clstat = closeSerialPort();
    return clstat;
}

void alarmHandler(int signal) {
    alarmPlaying = TRUE;
    printf("alarm played\n");
}

unsigned char *byteStuffing(const unsigned char *buf, int bufSize, int *newSize) {
    
    int frameSize = bufSize;
    
    for (int i = 0; i < bufSize; i++) {
        if (buf[i] == FLAG || buf[i] == ESC) {
            frameSize++;
        }
    }

    unsigned char *stuffedBuf = (unsigned char *)malloc(frameSize);
    int k = 0;
    for (int i = 0; i < bufSize; i++) {
        if (buf[i] == FLAG) {
            stuffedBuf[k++] = ESC;
            stuffedBuf[k++] = FLAG ^ 0x20;
        } else if (buf[i] == ESC) {
            stuffedBuf[k++] = ESC;
            stuffedBuf[k++] = ESC ^ 0x20;
        } else {
            stuffedBuf[k++] = buf[i];
        }
    }

    *newSize = k;
    return stuffedBuf;
}

int sendFrameS (unsigned char a, unsigned char c) {
    unsigned char sFrame[5] = {FLAG, a, c, BCC(a,c), FLAG};
    return writeBytes(sFrame, 5);
}

unsigned char readSupervisionFrame(LinkLayerState *linkLayerState, unsigned char a, unsigned char *cValues, int cValuesCount) {
    unsigned char byteRead;
    unsigned char c = 0;

    while (readByte(&byteRead) > 0) {
        switch (*linkLayerState) {
            case START:
                if (byteRead == FLAG) *linkLayerState = FLAG_RCV;
                break;
            case FLAG_RCV:
                if (byteRead == a) *linkLayerState = A_RCV;
                else if (byteRead != FLAG) *linkLayerState = START;
                break;
            case A_RCV:
                for (int i = 0; i < cValuesCount; i++) {
                    if (byteRead == cValues[i]) {
                        *linkLayerState = C_RCV;
                        c = byteRead;
                        break;
                    }
                }
                if (*linkLayerState != C_RCV) {
                    if (byteRead == FLAG) *linkLayerState = FLAG_RCV;
                    else *linkLayerState = START;
                }
                break;
            case C_RCV:
                if (byteRead == BCC(a, c)) *linkLayerState = BCC1_OK;
                else if (byteRead == FLAG) *linkLayerState = FLAG_RCV;
                else *linkLayerState = START;
                break;
            case BCC1_OK:
                if (byteRead == FLAG) {
                    *linkLayerState = STOP;
                    return c; 
                } else {
                    *linkLayerState = START;
                }
                break;
            default:
                *linkLayerState = START;
                break;
        }
    }
    return 0; 
}

void readSupervisionFrameRx(LinkLayerState *linkLayerState, unsigned char a, unsigned char *cValues, int cValuesCount) {
    while (*linkLayerState != STOP) {
        readSupervisionFrame(linkLayerState, A_ER, cValues, cValuesCount);
    }
}

void sendSuperVisionFrameAndReadReply(LinkLayerState *linkLayerState, unsigned char a, unsigned char c, unsigned char *cValues, int cValuesCount) {
    int retransmissions = 0;
    while (*linkLayerState != STOP && retransmissions < nRetransmissions) {
        sendFrameS(a, c);
        alarm(timeout);
        alarmPlaying = FALSE;

        while (!alarmPlaying && *linkLayerState != STOP) {
            readSupervisionFrame(linkLayerState, a, cValues, 1);

        }
        if (alarmPlaying) {
            retransmissions++;
        }
    }
    totalRetransmissions += retransmissions;
}





/*
quizz 5 perguntas 15 minutos 

sigaction em vez de signal()


Perguntar ao professor se é preciso um alarme para transmitir o DISC do lado do receptor
Ver como funciona bem o caso de desconexão dos dois lados, porque há um comando como resposta de um comando
Perguntar ao professor se um reject conta como uma try 
Perguntar se 3 tries falhar é muito mau?

*/