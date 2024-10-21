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

LinkLayerRole role;

int llopen(LinkLayer connectionParameters)
{
    int serialPortFd  = openSerialPort(connectionParameters.serialPort, connectionParameters.baudRate);
    if (serialPortFd < 0) return -1;

    LinkLayerState linkLayerState = START;
    char byteRead;

    nRetransmissions = connectionParameters.nRetransmissions;
    timeout = connectionParameters.timeout;
    
    int nrBytesRead = 0; 

    role = connectionParameters.role;

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
                        if (byteRead == A_ER) linkLayerState = A_RCV;
                        else if (byteRead != FLAG) linkLayerState = START;
                        break;
                    case A_RCV:
                        if (byteRead == C_UA) linkLayerState = C_RCV;
                        else if (byteRead == FLAG) linkLayerState = FLAG_RCV;
                        else linkLayerState = START;
                        break;
                    case C_RCV:
                        if (byteRead == BCC(A_ER, C_UA)) linkLayerState = BCC1_OK;
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
    char *informationFrame = (char *)malloc(frameSize);
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

    int currentTransmission = 0;
    int rejected = 0, accepted = 0;

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

            if (c == C_REJ(0) || c == C_REJ(1)) {
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
    if (accepted) {
        return frameSize;
    } 
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

    while (linkLayerState != STOP) {
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
                    if (byteRead == A_ER) linkLayerState = A_RCV;
                    else if (byteRead != FLAG) linkLayerState = START;
                    break;
                case A_RCV:
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
                            linkLayerState = STOP;
                            sendFrameS(A_ER, C_RR(tramaRx));
                            
                            tramaRx = (tramaRx == 0) ? 1 : 0;
                            return i; 

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
    LinkLayerState linkLayerState = START;
    char byteRead;
    
    int nrBytesRead = 0;

    switch (role)
    {
    case LlTx:

        (void) signal(SIGALRM, alarmHandler);
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
        sendFrameS(A_RE, C_UA);

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
    char sFrame[5] = {FLAG, a, c, BCC(a,c), FLAG};
    return writeBytes(sFrame, 5);
}

void readSupervisionFrame(LinkLayerState linkLayerState) {
    char byteRead;
    while (readByte(&byteRead) > 0) {
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
            if (byteRead == C_UA) linkLayerState = C_RCV;
            else if (byteRead == FLAG) linkLayerState = FLAG_RCV;
            else linkLayerState = START;
            break;
        case C_RCV:
            if (byteRead == BCC(A_ER, C_UA)) linkLayerState = BCC1_OK;
            else if (byteRead == FLAG) linkLayerState = FLAG_RCV;
            else linkLayerState = START;
            break;
        case BCC1_OK:
            if (byteRead == FLAG) {
                linkLayerState = STOP;
                return;
            } else {
                linkLayerState = START;
            }
            break;
        default:
            linkLayerState = START;
            break;
        }
    }
}



unsigned char readCFrame() {
    char byteRead;
    unsigned char c = 0;
    LinkLayerState linkLayerState = START;

    while (linkLayerState != STOP && !alarmPlaying) {
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
                    if ((byteRead & 0xFF) == C_RR(0) || (byteRead & 0xFF) == C_RR(1) || (byteRead & 0xFF) == C_REJ(0) || (byteRead & 0xFF) == C_REJ(1) || (byteRead & 0xFF) == C_DISC) {
                        linkLayerState = C_RCV;
                        c = byteRead;
                    } else if (byteRead == FLAG) {
                        linkLayerState = FLAG_RCV;
                    } else {
                        linkLayerState = START;
                    }
                    break;
                case C_RCV:
                    if (byteRead == (A_ER ^ c)) linkLayerState = BCC1_OK;
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