// Link layer protocol implementation

#include "link_layer.h"
#include "serial_port.h"

// MISC
#define _POSIX_SOURCE 1 // POSIX compliant source

////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////

int nRetransmissions, timeout = 0;
int alarmPlaying = FALSE;

unsigned char tramaTx = 0;
unsigned char tramaRx = 1;

int llopen(LinkLayer connectionParameters)
{
    int serialPortFd  = openSerialPort(connectionParameters.serialPort, connectionParameters.baudRate);
    if (serialPortFd < 0) return -1;

    LinkLayerState linkLayerState = START;
    char byteRead;

    nRetransmissions = connectionParameters.nRetransmissions;
    timeout = connectionParameters.timeout;
    
    int nrBytesRead = 0; 

    switch (connectionParameters.role)
    {
    case LlTx:

        (void) signal(SIGALRM, alarmHandler);
        while (linkLayerState != STOP && connectionParameters.nRetransmissions > 0) {
            sendFrameS(A_ER, C_SET);
            alarm(connectionParameters.timeout);
            alarmPlaying = FALSE;


            //perguntar ao professor se devia guardar os bytes do A e C e fazer o check com o BCC
            // ou se ta bom assim
            while (!alarmPlaying && linkLayerState != STOP) {
                nrBytesRead = readByte(&byteRead);

                if (nrBytesRead < 0) {
                    //printf ("An error occurred while reading the Receiver Reply\n");
                    break;
                }
                else if (nrBytesRead > 0) {
                    switch (linkLayerState)
                    {
                    case START:
                        if (byteRead == FLAG) linkLayerState = FLAG_RCV;
                        break;
                    case FLAG_RCV:
                        if (byteRead == A_RE) linkLayerState = A_RCV;
                        else if (byteRead != FLAG) linkLayerState = START;
                        break;
                    case A_RCV:
                        if (byteRead == C_UA) linkLayerState = C_RCV;
                        else if (byteRead == FLAG) linkLayerState = FLAG_RCV;
                        else linkLayerState = START;
                        break;
                    case C_RCV:
                        if (byteRead == BCC(A_RE, C_UA)) linkLayerState = BCC1_OK;
                        else if (byteRead == FLAG) linkLayerState = FLAG_RCV;
                        else linkLayerState = START;
                        break;
                    case BCC1_OK:
                        if (byteRead == FLAG) linkLayerState = STOP;
                        else linkLayerState = START;
                        break;    
                    default:
                        break;
                    }
                }
            }
            connectionParameters.nRetransmissions--;
        }
        if (linkLayerState != STOP) return -1;
        break;

    case LlRx:
        while (linkLayerState != STOP) {
            nrBytesRead = readByte(&byteRead);
            if (nrBytesRead < 0) {
                //printf ("An error occurred while reading the SET frame\n");
                break;
            }
            else if (nrBytesRead > 0) {
                switch (linkLayerState)
                {
                case START:
                    if (byteRead == FLAG) linkLayerState = FLAG_RCV;
                    break;
                case FLAG_RCV:
                    if (byteRead == A_ER) linkLayerState = A_RCV;
                    else if (byteRead != FLAG) linkLayerState = START;
                    break;
                case A_RCV:
                    if (byteRead == C_SET) linkLayerState = C_RCV;
                    else if (byteRead == FLAG) linkLayerState = FLAG_RCV;
                    else linkLayerState = START;
                    break;
                case C_RCV:
                    if (byteRead == BCC(A_ER, C_SET)) linkLayerState = BCC1_OK;
                    else if (byteRead == FLAG) linkLayerState = FLAG_RCV;
                    else linkLayerState = START;
                    break;
                case BCC1_OK:
                    if (byteRead == FLAG) linkLayerState = STOP;
                    else linkLayerState = START;
                    break;    
                default:
                    break;
                }
            }
        }
        sendFrameS(A_RE,C_UA);
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
    printf("coiso 1\n");
    int frameSize = bufSize + 6;
    char *informationFrame = (char *)malloc(frameSize);
    if (!informationFrame) {
        return -1; 
    }

    informationFrame[0] = FLAG;
    informationFrame[1] = A_ER;
    informationFrame[2] = C_N(tramaTx);
    informationFrame[3] = BCC(informationFrame[1], informationFrame[2]);

  
    int stuffedSize;
    unsigned char *stuffedBuf = byteStuffing(buf, bufSize, &stuffedSize);
    if (!stuffedBuf) {
        free(informationFrame);
        return -1;
    }

    printf("coiso 2\n");
    memcpy(informationFrame + 4, stuffedBuf, stuffedSize);

 
    unsigned char bcc2 = buf[0];
    for (unsigned int i = 1; i < bufSize; i++) {
        bcc2 ^= buf[i];
    }


    int newStuffedSize;
    unsigned char *stuffedBCC2 = byteStuffing(&bcc2, 1, &newStuffedSize);
    if (!stuffedBCC2) {
        free(stuffedBuf);
        free(informationFrame);
        return -1;
    }

    printf("coiso 3\n");
    memcpy(informationFrame + 4 + stuffedSize, stuffedBCC2, newStuffedSize);
    informationFrame[4 + stuffedSize + newStuffedSize] = FLAG;


    free(stuffedBuf);
    free(stuffedBCC2);

    int currentTransmission = 0;
    int rejected = 0, accepted = 0;

    printf("coiso 4\n");
    while (currentTransmission < nRetransmissions) {
        alarmPlaying = FALSE;
        alarm(timeout);
        rejected = 0;
        accepted = 0;

        while (!alarmPlaying && !rejected && !accepted) {

            if (writeBytes(informationFrame, frameSize) < 0) {
                free(informationFrame);
                return -1;
            }


            unsigned char c = readCFrame();
            if (c == 0) {
                continue; 
            } else if (c == C_REJ(0) || c == C_REJ(1)) {
                rejected = 1;
            } else if (c == C_RR(0) || c == C_RR(1)) {
                accepted = 1; 
                tramaTx = (tramaTx == 0) ? 1 : 0; 
            }
            else continue;
        }

        if (accepted) {
            break;
        }
        currentTransmission++;
    }

    free(informationFrame);
    if (accepted) return frameSize;
    else {
        llclose(-1);
        return -1;
    } 
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
int llread(unsigned char *packet)
{
    char byteRead;
    unsigned char c;
    int i = 0;
    LinkLayerState linkLayerState = START;

    while (linkLayerState != STOP && !alarmPlaying) {
        int nrBytesRead = readByte(&byteRead);
        if (nrBytesRead < 0) {
            //printf ("An error occurred while reading the SET frame\n");
            break;
        }
        else if (nrBytesRead > 0) {
            switch (linkLayerState) {
                case START:
                    printf("start\n");
                    if (byteRead == FLAG) linkLayerState = FLAG_RCV;
                    break;
                case FLAG_RCV:
                    printf("flag\n");
                    if (byteRead == A_ER) linkLayerState = A_RCV;
                    else if (byteRead != FLAG) linkLayerState = START;
                    break;
                case A_RCV:
                    printf("A_RCV\n");
                    if (byteRead == C_N(0) || byteRead == C_N(1)) {
                        linkLayerState = C_RCV;
                        c = byteRead;
                    } else if (byteRead == FLAG) {
                        linkLayerState = FLAG_RCV;
                    } else if (byteRead == C_DISC) {
                        sendFrameS(A_RE, C_DISC);
                        return 0;
                    } else {
                        linkLayerState = START;
                    }
                    break;
                case C_RCV:
                    printf("C_RCV: %d\n", byteRead);
                    if (byteRead == (A_ER ^ c)) linkLayerState = READ_DATA;
                    else if (byteRead == FLAG) linkLayerState = FLAG_RCV;
                    else linkLayerState = START;
                    break;
                case READ_DATA:
                    printf("READ_DATA\n");
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
                            printf("i entered here, i = %d\n", i);
                            linkLayerState = STOP;
                            sendFrameS(A_RE, C_RR(tramaRx));
                            tramaRx = (tramaRx + 1)%2;
                            return i; 

                        } else{
                            printf("Error: retransmition\n");
                            sendFrameS(A_RE, C_REJ(tramaRx));
                            return -1;
                        }

                    } else {

                        packet[i++] = byteRead;

                    }
                    break;
                case STUFFED_BYTE:
                    printf("stuffed\n");
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
    //perguntar o que fazer com este inteiro showStatistics
    
    (void) signal(SIGALRM, alarmHandler);
    LinkLayerState linkLayerState = START;
    char byteRead;
    
    int nrBytesRead = 0; 

    while (nRetransmissions > 0 && linkLayerState != STOP) {
        sendFrameS(A_ER, C_DISC);
        alarm(timeout);
        alarmPlaying = FALSE;

        while (!alarmPlaying && linkLayerState != STOP) {
            nrBytesRead = readByte(&byteRead);
            if (nrBytesRead < 0) {
                //printf ("An error occurred while reading the SET frame\n");
                break;
            } else if (nrBytesRead > 0) {
                switch (linkLayerState)
                    {
                    case START:
                        if (byteRead == FLAG) linkLayerState = FLAG_RCV;
                        break;
                    case FLAG_RCV:
                        if (byteRead == A_RE) linkLayerState = A_RCV;
                        else if (byteRead != FLAG) linkLayerState = START;
                        break;
                    case A_RCV:
                        if (byteRead == C_DISC) linkLayerState = C_RCV;
                        else if (byteRead == FLAG) linkLayerState = FLAG_RCV;
                        else linkLayerState = START;
                        break;
                    case C_RCV:
                        if (byteRead == BCC(A_RE, C_DISC)) linkLayerState = BCC1_OK;
                        else if (byteRead == FLAG) linkLayerState = FLAG_RCV;
                        else linkLayerState = START;
                        break;
                    case BCC1_OK:
                        if (byteRead == FLAG) linkLayerState = STOP;
                        else linkLayerState = START;
                        break;    
                    default:
                        break;
                }
            } 
        }
        nRetransmissions--;
    }

    if (linkLayerState != STOP) return -1;
    sendFrameS(A_ER, C_UA);

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
    char sFrame[5] = {FLAG, a, c, BCC(a,c), FLAG};
    return writeBytes(sFrame, 5);
}

unsigned char readCFrame() {
    char byteRead;
    unsigned char c = 0;
    LinkLayerState linkLayerState = START;

    while (linkLayerState != STOP && !alarmPlaying) {
        int nrBytesRead = readByte(&byteRead);
        if (nrBytesRead < 0) {
            //printf ("An error occurred while reading the SET frame\n");
            break;
        }
        else if (nrBytesRead > 0) {
            switch (linkLayerState) {
                case START:
                    if (byteRead == FLAG) linkLayerState = FLAG_RCV;
                    break;
                case FLAG_RCV:
                    if (byteRead == A_RE) linkLayerState = A_RCV;
                    else if (byteRead != FLAG) linkLayerState = START;
                    break;
                case A_RCV:
                    if (byteRead == C_RR(0) || byteRead == C_RR(1) || byteRead == C_REJ(0) || byteRead == C_REJ(1) || byteRead == C_DISC) {
                        linkLayerState = C_RCV;
                        c = byteRead;
                    } else if (byteRead == FLAG) {
                        linkLayerState = FLAG_RCV;
                    } else {
                        linkLayerState = START;
                    }
                    break;
                case C_RCV:
                    if (byteRead == (A_RE ^ c)) linkLayerState = BCC1_OK;
                    else if (byteRead == FLAG) linkLayerState = FLAG_RCV;
                    else linkLayerState = START;
                    break;
                case BCC1_OK:
                    if (byteRead == FLAG) {
                        linkLayerState = STOP;
                    } else {
                        linkLayerState = START;
                    }
                    break;
                default:
                    break;
            }
        }
    }
    return c;
}


/*
quizz 5 perguntas 15 minutos 

sigaction em vez de signal()

*/