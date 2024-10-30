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
        perror("Error: Connection establish connection\n\n");
        exit(EXIT_FAILURE);
    }
    printf("Connection was established\n\n");

    int bytesWrittenOrRead = 0;
    
    switch (linkLayer.role)
    {
    case LlTx:

        FILE* file = fopen(filename, "rb");
        if (file == NULL) {
            perror("Error: file not found\n");
            exit(EXIT_FAILURE);
        }

        fseek(file, 0L, SEEK_END);
        long int fileSize = ftell(file);
        fseek(file, 0L, SEEK_SET);

        printf("\nSending file: %s\n", filename);
        printf("File size: %ld bytes\n\n", fileSize);


        unsigned int controlPacketSize;
        unsigned char *startControlPacket = buildControlPacket(1, filename, fileSize, &controlPacketSize);

        if ((bytesWrittenOrRead = llwrite(startControlPacket, controlPacketSize)) < 0) {
            printf("Error: Couldn't write starter control packet\n");
            exit(EXIT_FAILURE);
        }
        printf("Start control packet successfully sent: %d bytes written\n", bytesWrittenOrRead);

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

            if ((bytesWrittenOrRead = llwrite(dataPacket, dataPacketSize)) < 0) {
                printf("Error: Couldn't write data packet\n");

                if (llclose(1) < 0) {
                    printf("Error: Couldn't close connection\n");
                }
                printf("Connection closed\n");
                exit(EXIT_FAILURE);
            }
            printf("Data packet sent: %d bytes written\n", bytesWrittenOrRead);

            bytes -= dataToSendSize;
            offset += dataToSendSize;
            sequenceNumber = (sequenceNumber + 1) % 99;

            free(dataToSend);
            free(dataPacket);
        }

        unsigned char *endControlPacket = buildControlPacket(3, filename, fileSize, &controlPacketSize);
        
        if ((bytesWrittenOrRead = llwrite(endControlPacket, controlPacketSize)) < 0) { 
            printf("Error: Couldn't write end control packet\n");

            if (llclose(1) < 0) {
                printf("Error: Couldn't close connection\n");
            }
            printf("Connection closed\n");

            exit(EXIT_FAILURE);
        }
        printf("End control packet successfully sent: %d bytes written\n", bytesWrittenOrRead);

        if (llclose(1) < 0) {
            printf("Error: Couldn't close connection\n");
            exit(EXIT_FAILURE);
        }
        printf("Connection closed\n");

        break;

    case LlRx:

        unsigned char* packet = (unsigned char *) malloc(MAX_PAYLOAD_SIZE);
        int readControlPacketSize = 0;
        int size = -1;
        while ((readControlPacketSize = llread(packet)) < 0);
        printf("Start control packet received: %d bytes read\n", readControlPacketSize);

        unsigned long int newFileSize = 0;
        unsigned char *fileName = processControlPacket(packet, &newFileSize);
        if (fileName == NULL) {
            printf("Error: filename is NULL\n");
            exit(EXIT_FAILURE);
        } 
        
        printf("\nReceiving file: %s\n", fileName);
        printf("File size: %lu bytes\n\n", newFileSize);

        const char *newFilename = filename;
        FILE *newFile = fopen((char *) newFilename, "wb+");
        if (newFile == NULL) {
            perror("Error opening file for writing");
            exit(EXIT_FAILURE);
        }

        while (1) {
            while ((size = llread(packet)) < 0) {
            }
            printf("Data packet received: %d bytes read\n", size);
            
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
                        perror("Error: Something went wrong when writing to file");
                    }
                } else {
                    fprintf(stderr, "Invalid size to write: %d\n", size - 4);
                }

                free(buffer);
            }

            else if (packet[0] == 3){
                printf("End control packet received\n");
                fclose(newFile);

                if (llclose(1) < 0) {
                    printf("Error: Couldn't close connection\n");
                    exit(EXIT_FAILURE);
                }
                printf("Connection closed\n");

                break;
            }
        }

        break;

    default:
        exit(-1);
        break;
    }
}

unsigned long int getFileSize(unsigned char* packet, int *i) {
    unsigned long int fileSize = 0;
    unsigned char l = packet[(*i)++];
    for (int j = 0; j < l; j++) {
        fileSize = (fileSize << 8) | packet[(*i)++];
    }
    return fileSize;
}

unsigned char* getFilename(unsigned char* packet, int *i) {
    unsigned char l = packet[(*i)++];
    unsigned char *fileName = (unsigned char*)malloc(l + 1);
    memcpy(fileName, packet + (*i), l);
    fileName[l] = '\0';
    return fileName;
}

unsigned char *processControlPacket(unsigned char* packet, unsigned long int *fileSize) {
    int i = 0;
    unsigned char *fileName = NULL;

    unsigned char controlField = packet[i++];
    if (controlField != 1 && controlField != 3) {
        return NULL;
    }
    
    unsigned char t1 = packet[i++];

    if  (t1 == 0) *fileSize = getFileSize(packet, &i);
    else if (t1 == 1) fileName = getFilename(packet, &i);


    unsigned char t2 = packet[i++];
    if  (t2 == 0) *fileSize = getFileSize(packet, &i);
    else if (t2 == 1) fileName = getFilename(packet, &i);

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