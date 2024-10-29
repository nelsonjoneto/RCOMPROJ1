// Application layer protocol implementation

#include "application_layer.h"
#include "link_layer.h"
#include <string.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#include <math.h>

void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename)
{
    LinkLayer linkLayer;
    
    linkLayer.baudRate = baudRate;
    linkLayer.nRetransmissions = nTries;
    linkLayer.timeout = timeout;
    strcpy(linkLayer.serialPort, serialPort);
    linkLayer.role = strcmp(role, "tx") ? LlRx : LlTx;

    int fd = llopen(linkLayer);
    if (fd < 0) {
        perror("Error: Connection went wrong\n");
        exit(-1);
    }

    switch (linkLayer.role)
    {
    case LlTx:

        FILE* file = fopen(filename, "rb");
        if (file == NULL) {
            perror("Error: file not found\n");
            exit(-1);
        }

        fseek(file, 0L, SEEK_END);
        long int fileSize = ftell(file);
        fseek(file, 0L, SEEK_SET);

        unsigned int controlPacketSize;
        unsigned char *startControlPacket = buildControlPacket(1, filename, fileSize, &controlPacketSize);
        
        if (llwrite(startControlPacket, controlPacketSize) == -1) {
            printf("Error: Couldn't write starter control packet\n");
            exit(-1);
        }

        unsigned char sequenceNumber = 0;
        unsigned char* fullData = (unsigned char*) malloc(sizeof(unsigned char) * fileSize);
        fread(fullData, sizeof(unsigned char), fileSize, file);

        long int bytes = fileSize;
        long int offset = 0;

        while (bytes > 0)
        {
            int dataToSendSize = bytes > (long int) MAX_PAYLOAD_SIZE ? MAX_PAYLOAD_SIZE : bytes;
            unsigned char* dataToSend = (unsigned char *) malloc(dataToSendSize);

            memcpy(dataToSend, fullData + offset, dataToSendSize);

            int dataPacketSize;
            unsigned char *dataPacket = buildDataPacket(sequenceNumber, dataToSend, dataToSendSize, &dataPacketSize);

            if (llwrite(dataPacket, dataPacketSize) == -1) {
                printf("Error: Couldn't write data packet\n");
                exit(-1);
            }

            bytes -= dataToSendSize;
            offset += dataToSendSize;
            sequenceNumber = (sequenceNumber + 1) % 99;

            free(dataToSend);
            free(dataPacket);
        }

        unsigned char *endControlPacket = buildControlPacket(3, filename, fileSize, &controlPacketSize);
        
        if (llwrite(endControlPacket, controlPacketSize) == -1) { 
            printf("Error: Couldn't write end control packet\n");
            exit(-1);
        }

        llclose(1);

        break;

    case LlRx:

        unsigned char* packet = (unsigned char *) malloc(MAX_PAYLOAD_SIZE);
        int readControlPacketSize = 0;
        int size = -1;
        while ((readControlPacketSize = llread(packet)) < 0);

        if (readControlPacketSize == 0) printf ("#asdasda\n");

        unsigned long int newFileSize = 0;
        unsigned char *fileName = processControlPacket(packet, &newFileSize);
        if (fileName == NULL) {
            printf("Error: fileName is NULL\n");
            exit(EXIT_FAILURE);
        }

        fileName = filename;
        FILE *newFile = fopen((char *) fileName, "wb+");
        if (newFile == NULL) {
            perror("Error opening file for writing");
            exit(EXIT_FAILURE);
        }

        while (1) {
            while ((size = llread(packet)) < 0);
            
            if (packet[0] == 2 && size != 0) {
                unsigned char *buffer = (unsigned char*) malloc(size);
                if (buffer == NULL) {
                    perror("Memory allocation failed for buffer");
                    exit(EXIT_FAILURE);
                }

                processDataPacket(packet, size, buffer);

                if (size - 4 > 0) {
                    size_t written = fwrite(buffer, sizeof(unsigned char), size - 4, newFile);
                    if (written != size - 4) {
                        perror("Error writing to file");
                    }
                } else {
                    fprintf(stderr, "Invalid size to write: %d\n", size - 4);
                }

                free(buffer);
            }

            else if (packet[0] == 3){
                llclose(1);
                fclose(newFile);
                break;
            }
        }

        break;

    default:
        exit(-1);
        break;
    }
}


unsigned char *processControlPacket(unsigned char* packet, unsigned long int *fileSize) {
    int i = 0;

    unsigned char controlField = packet[i++];
    if (controlField != 1 && controlField != 3) {
        return NULL;
    }

    unsigned char t1 = packet[i++];
    unsigned char l1 = packet[i++];

    *fileSize = 0;
    for (int j = 0; j < l1; j++) {
        *fileSize = (*fileSize << 8) | packet[i++];
    }


    unsigned char t2 = packet[i++];
    unsigned char l2 = packet[i++];


    if (l2 < 1 || i + l2 > 20) { 
        return NULL;
    }

    unsigned char *fileName = (unsigned char*)malloc(l2 + 1);
    if (!fileName) {
        return NULL;
    }

    memcpy(fileName, packet + i, l2);
    fileName[l2] = '\0';

    return fileName;
}



unsigned char * buildControlPacket(const unsigned int c, const char* filename, long int length, unsigned int* size) {

    int l1 = 0;
    long int tempLength = length;

    while (tempLength > 0) {
        l1++;
        tempLength >>= 8;
    }

    int l2 = strlen(filename);
    *size = 5 + l1 + l2; 

    unsigned char *controlPacket = (unsigned char*)malloc(*size);
    if (!controlPacket) {
        return NULL;
    }

    unsigned int i = 0;
    controlPacket[i++] = c;  
    controlPacket[i++] = 0;  
    controlPacket[i++] = l1;  


    for (int j = l1 - 1; j >= 0; j--) {
        controlPacket[i + j] = length & 0xFF;
        length >>= 8;
    }
    i += l1;

    controlPacket[i++] = 1;   
    controlPacket[i++] = l2; 


    memcpy(controlPacket + i, filename, l2);
    
    return controlPacket;
}

unsigned char *buildDataPacket(unsigned char sequenceNumber, unsigned char *data, long int dataSize, int *size) {
    
    *size = 4 + dataSize; 

    unsigned char *dataPacket = (unsigned char*)malloc(*size);
    if (!dataPacket) {
        return NULL; 
    }

    dataPacket[0] = 2;                
    dataPacket[1] = sequenceNumber;   
    dataPacket[2] = (dataSize >> 8) & 0xFF; 
    dataPacket[3] = dataSize & 0xFF;       

    memcpy(dataPacket + 4, data, dataSize);

    return dataPacket;

    
}

void processDataPacket (unsigned char* packet, int size, unsigned char* buffer) {
    unsigned char l2 = packet[2];
    unsigned char l1 = packet[3];
    int length = 256 * l2 + l1;

    if (length > 0 && size >= 4 + length) {
        memcpy(buffer, packet + 4, length);
    }
}