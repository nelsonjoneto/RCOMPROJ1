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
    strcpy(linkLayer.serialPort,serialPort);
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

        int prev = ftell(file);
        fseek(file, 0L, SEEK_END);
        long int fileSize = ftell(file) - prev;
        fseek(file, prev, SEEK_SET);


        unsigned int controlPacketSize;
        unsigned char *startControlPacket = buildControlPacket(1, filename, fileSize, &controlPacketSize);
        
        
        if(llwrite(startControlPacket, controlPacketSize) == -1){ 
            printf("Error: Couldn't write starter control packet\n");
            exit(-1);
        }

        unsigned char sequenceNumber = 0;;

        unsigned char* fullData = (unsigned char*) malloc (sizeof (unsigned char) * fileSize);

        long int bytes = fileSize;

        while (bytes >= 0)
        {
            int dataToSendSize = bytes > (long int) MAX_PAYLOAD_SIZE ? MAX_PAYLOAD_SIZE : bytes;

            unsigned char* dataToSend = (unsigned char *) malloc(dataToSendSize);

            memcpy(dataToSend, fullData, dataToSendSize);

            int dataPacketSize;

            unsigned char *dataPacket = buildDataPacket(sequenceNumber, dataToSend, dataToSendSize, &dataPacketSize);

            if (llwrite(dataPacket, dataPacketSize) == -1) {
                printf("Error: Couldn't write data packet\n");
                exit(-1);
            }

            bytes -= (long int) dataToSendSize;

            fullData += dataToSendSize;

            sequenceNumber = (sequenceNumber + 1) % 0xFF;
        }
        

        unsigned char *endControlPacket = buildControlPacket(3, filename, fileSize, &controlPacketSize);
        
        if(llwrite(endControlPacket, controlPacketSize) == -1){ 
            printf("Error: Couldn't write end control packet\n");
            exit(-1);
        }

        llclose(0);
        break;
    case LlRx:

        unsigned char* packet = (unsigned char *) malloc(MAX_PAYLOAD_SIZE);

        int size = -1;
        while ((controlPacketSize = llread(packet)) < 0);

        unsigned long int newFileSize = 0;

        unsigned char *fileName = processControlPacket(packet, size, &newFileSize);

        FILE *newFile = fopen((char *) fileName, "wb+");

        while(1) {

            while ((size = llread(packet)) < 0);

            if (size < 0) break;

            else if(packet[0] != 3){
                unsigned char *buffer = (unsigned char*)malloc(size);

                processDataPacket(packet, size, buffer);

                fwrite(buffer, sizeof(unsigned char), size - 4, newFile);

                free(buffer);
            }
            else continue;
        }

        fclose(newFile);
        break;
    default:
        exit(-1);
        break;
    }
}

unsigned char *processControlPacket(unsigned char* packet, int size, unsigned long int *fileSize) {
    if (size < 5) {
        return NULL;
    }

    int i = 0;
    unsigned char controlField = packet[i++];
    if (controlField != 1 && controlField != 3) {
        return NULL;
    }

    unsigned char t1 = packet[i++]; 
    unsigned char l1 = packet[i++]; 

    if (t1 != 0 || l1 < 1 || i + l1 > size) {
        return NULL;
    }

    *fileSize = 0;
    for (int j = 0; j < l1; j++) {
        *fileSize = (*fileSize << 8) | packet[i++];
    }

    unsigned char t2 = packet[i++];
    unsigned char l2 = packet[i++]; 

    if (t2 != 1 || l2 < 1 || i + l2 > size) {
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
    do {
        l1++;
        tempLength >>= 8;
    } while (tempLength > 0);

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
    //unsigned char controlField = packet[0];
    //unsigned char sequenceNumber = packet[1];
    unsigned char l2 = packet[2];
    unsigned char l1 = packet[3];
    int length = 256 * l2 + l1;

    if (length > 0 && size >= 4 + length) {
        memcpy(buffer, packet + 4, length);
    }
}