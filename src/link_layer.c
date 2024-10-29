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
LinkLayerState linkLayerState;

int totalFramesSent, totalFramesRead, totalTimeouts, totalRejections, totalDuplicates = 0;


int llopen(LinkLayer connectionParameters)
{
    (void) signal(SIGALRM, alarmHandler);
    int serialPortFd  = openSerialPort(connectionParameters.serialPort, connectionParameters.baudRate);
    if (serialPortFd < 0) return -1;

    linkLayerState = START;

    nRetransmissions = connectionParameters.nRetransmissions;
    timeout = connectionParameters.timeout;
    role = connectionParameters.role;

    switch (role)
    {
    case LlTx:
        unsigned char cValuesTx[] = {C_UA};
        sendSuperVisionFrameAndReadReply(A_ER, C_SET, cValuesTx, 1);
        if (linkLayerState != STOP) {
            printf("Error: Couldn't establish connection\n");
            return -1;
        }
        printf("Connection was established\n");
        break;

    case LlRx:
        unsigned char cValuesRx[] = {C_SET};
        readSupervisionFrameRx(A_ER, cValuesRx, 1);
        sendFrameS(A_ER,C_UA);
        printf("Connection was established\n");
        break;
    default:
        return -1;
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
        printf("Error: Couldn't allocate memory for the information frame\n");
        return -1; 
    }

    informationFrame[0] = FLAG;
    informationFrame[1] = A_ER;
    informationFrame[2] = C_N(tramaTx);
    informationFrame[3] = BCC(informationFrame[1], informationFrame[2]);

  
    int stuffedDataSize;
    unsigned char *stuffedBuf = byteStuffing(buf, bufSize, &stuffedDataSize);
    if (!stuffedBuf) {
        printf("Error: Error occurred while byte stuffing data to be sent\n");
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
        printf("Error: Error occurred while byte stuffing BCC2\n");
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
    

    linkLayerState = START;
    unsigned char cValues[] = {C_RR(0), C_RR(1), C_REJ(0), C_REJ(1)};

    while (retransmissions < nRetransmissions) {
        alarm(timeout);
        alarmPlaying = FALSE;
            
        if (writeBytesSerialPort(informationFrame, frameSize) < 0) {
            printf("Error: Something went wrong when writing an information frame to the serial port\n");
            free(informationFrame);
            return -1;
        } 
        totalFramesSent++;
        
        while (!alarmPlaying) {
            unsigned char c = readSupervisionFrame(A_ER, cValues, 4);
            if (linkLayerState == STOP) {
                if (c == C_REJ(0) || c == C_REJ(1)) {
                    totalRejections++;
                    printf("Error: Rejection received\n");
                    break;
                } 
                if (c == C_RR(0) || c == C_RR(1)) {
                    printf("Information frame successfully sent: %d bytes written\n", frameSize);
                    tramaTx = (tramaTx == 0) ? 1 : 0;
                    free(informationFrame);
                    return frameSize;
                } 
            }

        }

        if (alarmPlaying) retransmissions++;

    }
    printf("Error: Maximum number of retransmissions surpassed\n");
    
    free(informationFrame);
    return -1;
}

unsigned char currentFrameNum = 0;

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
int llread(unsigned char *packet)
{
    linkLayerState = START;
    unsigned char byteRead;
    unsigned char c;
    int i = 0;


    while (linkLayerState != STOP) {
        int nrBytesRead = readByteSerialPort(&byteRead);
        if (nrBytesRead < 0) {
            printf("Error: Couldn't read byte from serial port\n");
            return -1;
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
                        totalFramesRead++;

                        unsigned char bcc2 = packet[i-1];
                        i--;
                        packet[i] = '\0';
                        unsigned char acc = packet[0];

                        for (unsigned int j = 1; j < i; j++)
                            acc ^= packet[j];
                        

                        if (bcc2 == acc){
                            if (C_N(currentFrameNum) == c) {
                                printf("Information frame accepted: %d bytes read\n", i);
                                currentFrameNum = (currentFrameNum == 0) ? 1 : 0;
                                sendFrameS(A_ER, C_RR(tramaRx));
                                tramaRx = (tramaRx == 0) ? 1 : 0;
                                return i;
                            } else {
                                totalDuplicates++;
                                printf("Discarding duplicate information frame\n");
                                sendFrameS(A_ER, C_RR(currentFrameNum));
                                return 0;
                            }

                        } else {
                            totalRejections++;
                            printf("Error: Retransmission required\n");
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
    printf("Error: Couldn't read packet\n");
    return -1;
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llclose(int showStatistics)
{

    linkLayerState = START;
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
                readSupervisionFrame(A_RE, cValues, 1);
            }

            if (alarmPlaying) {
                retransmissions++;
            }
        }
        if (linkLayerState != STOP) return -1;

        if (showStatistics) {
            printf("\nCommunication Statistics:\n");
            printf("Number of frames sent: %d\n", totalFramesSent);
            printf("Number of frames rejected: %d\n", totalRejections);
            printf("Number of timeouts: %d\n", totalTimeouts);
            printf("Number of retransmissions: %d\n", totalTimeouts + totalRejections);
            printf("\nNumber of frames received: %d\n", totalFramesRead);
        }

        sendFrameS(A_RE, C_UA);

        break;
    case LlRx:
        readSupervisionFrameRx(A_ER, cValues, 1);
        cValues[0] = C_UA;
        linkLayerState = START;
        sendSuperVisionFrameAndReadReply(A_RE, C_DISC, cValues, 1);

        if (linkLayerState != STOP) printf("Error: Unnumbered Acknowledgment wasn't received.");
        printf("\nConnection closed\n");

        if (showStatistics) {
            printf("\nCommunication Statistics:\n");
            printf("Number of frames sent: %d\n", totalFramesSent);
            printf("Number of timeouts: %d\n", totalTimeouts);
            printf("Number of retransmissions: %d\n", totalTimeouts);

            printf("\nNumber of frames received: %d\n", totalFramesRead);
            printf("Number of duplicate frames: %d\n", totalDuplicates);
            printf("Number of frames rejected: %d\n", totalRejections);
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
    totalTimeouts++;
    printf("Alarm played: Retransmission needed\n");
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
    totalFramesSent++;
    unsigned char sFrame[5] = {FLAG, a, c, BCC(a,c), FLAG};
    return writeBytesSerialPort(sFrame, 5);
}

unsigned char readSupervisionFrame(unsigned char a, unsigned char *cValues, int cValuesCount) {
    unsigned char byteRead;
    unsigned char c = 0;

    while (readByteSerialPort(&byteRead) > 0) {
        switch (linkLayerState) {
            case START:
                if (byteRead == FLAG) linkLayerState = FLAG_RCV;
                break;
            case FLAG_RCV:
                if (byteRead == a) linkLayerState = A_RCV;
                else if (byteRead != FLAG) linkLayerState = START;
                break;
            case A_RCV:
                for (int i = 0; i < cValuesCount; i++) {
                    if (byteRead == cValues[i]) {
                        linkLayerState = C_RCV;
                        c = byteRead;
                        break;
                    }
                }
                if (linkLayerState != C_RCV) {
                    if (byteRead == FLAG) linkLayerState = FLAG_RCV;
                    else linkLayerState = START;
                }
                break;
            case C_RCV:
                if (byteRead == BCC(a, c)) linkLayerState = BCC1_OK;
                else if (byteRead == FLAG) linkLayerState = FLAG_RCV;
                else linkLayerState = START;
                break;
            case BCC1_OK:
                if (byteRead == FLAG) {
                    totalFramesRead++;
                    linkLayerState = STOP;
                    return c; 
                } else {
                    linkLayerState = START;
                }
                break;
            default:
                linkLayerState = START;
                break;
        }
    }

    return 0; 
}

void readSupervisionFrameRx(unsigned char a, unsigned char *cValues, int cValuesCount) {
    while (linkLayerState != STOP) {
        readSupervisionFrame(a, cValues, cValuesCount);
    }
}

void sendSuperVisionFrameAndReadReply(unsigned char a, unsigned char c, unsigned char *cValues, int cValuesCount) {
    int retransmissions = 0;
    while (linkLayerState != STOP && retransmissions < nRetransmissions) {
        sendFrameS(a, c);
        alarm(timeout);
        alarmPlaying = FALSE;

        while (!alarmPlaying && linkLayerState != STOP) {
            readSupervisionFrame(a, cValues, 1);

        }
        if (alarmPlaying) {
            retransmissions++;
        }
    }
}





/*
Testes: 
    - ligar ou desligar a porta série
    - tramas vão ter erros 
    - abortar a aplicação no caso de nao ligar o recetor



quizz 5 perguntas 15 minutos 

sigaction em vez de signal()

IMPORTANTE: No pacote de controlo fazer check do t1, depois ler file size, mas também pode ser file name primeiro
IMPORTANTE: Checkar numero de sequencia na mesma

Ver se o linklayerState != STOP no llwrite faz diferença

Perguntar dou discard ao file name? Imprimir
Perguntar disponibilidade da sala
Perguntar ao professor se é preciso um alarme para transmitir o DISC do lado do receptor (nao preciso de receber UA aborto a dizer que não recebeu UA)
Ver como funciona bem o caso de desconexão dos dois lados, porque há um comando como resposta de um comando
Perguntar ao professor se um reject conta como uma try 
Perguntar se é normal ocorrer uma rejeição logo após o alarme tocar (reject nao conta como try)

*/