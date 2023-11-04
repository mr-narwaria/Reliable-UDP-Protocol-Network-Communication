#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "reliable.h"

int getFileSize(char* filename);
struct input_server read_input(char * file_path);
void beginProcess(int fileSize, int sock, struct sockaddr_in clientAddr, char * filename);
void sendACK(int next_seq, int sock, struct sockaddr_in addr);
void selectiveRepeat(char* file_path, int sock, struct sockaddr_in clientAddr, int fileSize);
void stopAndWait(char* file_path, int sock, struct sockaddr_in clientAddr, int fileSize);
void gbn(char* file_path, int sock, struct sockaddr_in clientAddr, int fileSize);
void selectiveRepeat2(char* file_path, int sock, struct sockaddr_in clientAddr, int fileSize);
void selectiveRepeat3(char* file_path, int sock, struct sockaddr_in clientAddr, int fileSize);

int servSock;
int clntSock;
int max_window_size;
int plp;
int seed;

int main() {

    struct sockaddr_in servAddr;
    struct sockaddr_in clntAddr;

    unsigned short servPort;

    struct input_server input = read_input("support/server.in");

    /*Main Thread Setup*/
    servPort = input.portServer;   /* First arg: local port */
    max_window_size = input.max_window_size;
    plp = input.prob;
    seed = input.seed;
    srand(seed);

    /* Create socket for incoming connections */
    if ((servSock = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
        DieWithError("socket () failed");

    /* Construct local address structure */
    memset(&servAddr, 0, sizeof(servAddr));
    servAddr.sin_family = AF_INET;
    servAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servAddr.sin_port = htons(servPort);

    /* Construct local address structure */
    memset(&clntAddr, 0, sizeof(clntAddr));

    if (bind(servSock, (struct sockaddr *)&servAddr,sizeof(servAddr)) < 0)
        DieWithError("bind () failed");

    for (;;){
        struct packet buffFileName;
        int sizeCA = sizeof(clntAddr);

        printStr("Wait for rec the File name");
        //Wait until recieve the file name
        recvfrom(servSock, &buffFileName, sizeof(buffFileName), 0, (struct sockaddr *) &clntAddr, &sizeCA);
        printStr("The file name reced");

        int thrd_num = fork();

        if (thrd_num == 0) {
            //Now in the Child Process
            close(servSock);

            if ((clntSock = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
                DieWithError("Erroc Creating child Socket");

            int file_size = getFileSize(buffFileName.data);

            if (file_size > 0) {
                beginProcess(file_size, clntSock, clntAddr, buffFileName.data);
            } else
                perror("Error reading the file");

            exit(0);
        } else if (thrd_num > 0) {
            //Now in the Parent Process
            close(clntSock);
            clntSock = -1;
        } else {
            //Error in Creating The Child
            perror("Error Creating the Child");
        }
    }
    exit(0);
}

struct input_server read_input(char * file_path) {
    FILE * fp;
    char * line;
    size_t len = 0;
    struct input_server inpt;
    memset(&inpt, 0, sizeof(inpt));

    fp = fopen(file_path, "r");
    if (fp == NULL)
        DieWithError("Input File Not Found");

    int counter = 0;
    while ((getline(&line, &len, fp)) != -1) {
        switch (counter) {
            case 0:
                inpt.portServer = atoi(line);
                break;
            case 1:
                inpt.max_window_size = atoi(line);
                break;
            case 2:
                inpt.seed = atoi(line);
                break;
            case 3:
                inpt.prob = atof(line);
                break;
        }
        counter++;
    }
    fclose(fp);
    return inpt;
}

void beginProcess(int fileSize, int sock, struct sockaddr_in clientAddr, char * filename) {

    struct ack_packet ackPack;
    printStr("Sending The size back");
    //Send to the Client the ack with the file size
    sendACK(fileSize, sock, clientAddr);


    //Wait for Ack
    printStr("wait for Ack of the size");
    int tr = sizeof(clientAddr);
    recvfrom(sock, &ackPack, sizeof(ackPack), 0, (struct sockaddr * ) &clientAddr, &tr);

    printStr("Sending The size Size Rec Ack");

    // selectiveRepeat(filename, sock, clientAddr, fileSize);
    // stopAndWait(filename, sock, clientAddr, fileSize);
    gbn(filename, sock, clientAddr, fileSize);

}

void sendACK(int next_seq, int sock, struct sockaddr_in addr) {
    struct ack_packet acknowldge;
    memset(&acknowldge, 0, sizeof(acknowldge));

    acknowldge.len = HEADERSIZE;
    acknowldge.ackno = next_seq; // Works as File size for First send the size of the file
    acknowldge.cksum = CHKSUM;

    sendto(sock, &acknowldge, sizeof(struct ack_packet), 0, (struct sockaddr*) &addr, sizeof(addr));
}

int getFileSize(char * filename) {

    FILE *f = fopen(filename, "r");
    long size = 0;

    if (f == NULL)
        perror("error"); //handle error
    else
    {
        fseek(f, 0, SEEK_END);
        size = ftell(f);
    }
    printf("the file to read size is : %ld\n", size);
    return size;

}

void selectiveRepeat(char* file_path, int sock, struct sockaddr_in clientAddr, int fileSize){

    FILE * fp;
    int base = 0;
    int remSize = fileSize;
    int window_size = 1;

    printStr("Start Sending\n");

    fp = fopen(file_path, "r");
    if (fp == NULL) {
        DieWithError("File not Found");
    }

    int seq_number = 3 * max_window_size;
    char buffer[DATASIZE * seq_number];
    int acks[seq_number];
    int bufferSize = DATASIZE * seq_number;

    if (fread(buffer, DATASIZE, seq_number, fp) < 0)
        DieWithError("Cant read the file");

    for(int index = 0; remSize > 0; index = (index + 1) % seq_number) {

        //Send the first packet of the buffer
        struct packet pck;
        memset(&pck, 0, sizeof(pck));
        pck.len = DATASIZE + HEADERSIZE;
        pck.seqno =  index;

        memcpy(pck.data, buffer + (index * DATASIZE), DATASIZE);
        printStr(pck.data);
        printNum(index);

        // checksum checking
        pck.cksum = crc16(pck.data, strlen(pck.data));

        // Loss Simulation
        float random = ((float)rand()/(float)(RAND_MAX));
        if (random > plp)
            sendto(sock, (void *)&(pck), sizeof(struct packet), 0, (struct sockaddr*) &clientAddr, sizeof(clientAddr));

        // RECEVING ACK
        struct ack_packet ackPck;
        int ss = sizeof(clientAddr);

        // unBlocking Recieve for ACK
        while (recvfrom(sock, &ackPck, sizeof(struct ack_packet), MSG_DONTWAIT, (struct sockaddr*) &clientAddr, &ss) > 0){
            remSize -= DATASIZE;
            bufferSize -= DATASIZE;
            int ackNo = ackPck.ackno % seq_number;
            acks[ackNo] = 1;
            // TODO:Timer Handling
            if (ackNo == base) {
                //TODO: There is time handling here
                printStr("Window increase");
                acks[ackNo] = 0;
                base = (base + 1) % seq_number;

                while (acks[base] && base < seq_number) {
                    acks[base] = 0;
                    base = (base + 1) % seq_number;
                }
            } else {
                acks[ackNo] = 0;
            }
        }

        if (bufferSize == 0 && remSize > DATASIZE) {
            memset(buffer, 0, seq_number * DATASIZE);
            fread(buffer, DATASIZE, seq_number, fp);
            bufferSize = DATASIZE * seq_number;
        } else if(bufferSize == 0 && remSize > 0) {
            memset(buffer, 0, seq_number * DATASIZE);
            fread(buffer, remSize, 1, fp);
            bufferSize = remSize;
        }
    }

    printStrSp("Finished: File is Transfering Successfully");
}

void stopAndWait(char* file_path, int sock, struct sockaddr_in clientAddr, int fileSize) {

    FILE * fp;
    int remSize = fileSize;

    printStr("Start Sending\n");

    fp = fopen(file_path, "r");
    if (fp == NULL) {
        DieWithError("File not Found");
    }

    int seq_number = 3 * max_window_size;
    char buffer[DATASIZE * seq_number];
    int bufferSize = DATASIZE * seq_number;

    if (fread(buffer, DATASIZE, seq_number, fp) < 0)
        DieWithError("Cant read the file");

    for(int index = 0; remSize > 0; index = (index + 1) % seq_number) {

        //Send the first packet of the buffer
        struct packet pck;
        memset(&pck, 0, sizeof(pck));
        pck.len = DATASIZE + HEADERSIZE;
        pck.seqno =  index;

        memcpy(pck.data, buffer + (index * DATASIZE), DATASIZE);
        printStr(pck.data);

        pck.cksum = crc16(pck.data, strlen(pck.data));

        //LOSS Simulation
        float random = ((float)rand()/(float)(RAND_MAX));

        printStr("Send a pck");
        printNum(pck.seqno);
        if (random > plp)
            sendto(sock, (void *)&(pck), sizeof(struct packet), 0, (struct sockaddr*) &clientAddr, sizeof(clientAddr));

        printStr(pck.data);

        struct ack_packet ackPck;
        int ss = sizeof(clientAddr);
        printStr("Waiting Rec ack");
        //Blocking Recieve for ACK
        recvfrom(sock, &ackPck, sizeof(struct ack_packet), 0, (struct sockaddr*) &clientAddr, &ss);
        printStrSp("Ack Recieved");
        printNum(ackPck.ackno);

        if (pck.seqno != ackPck.ackno) {
            index--;
            continue;
        }

        remSize -= DATASIZE;
        bufferSize -= DATASIZE;

        if (bufferSize == 0 && remSize > DATASIZE) { //normal pckt check
            memset(buffer, 0, seq_number * DATASIZE);
            fread(buffer, DATASIZE, seq_number, fp);
            bufferSize = DATASIZE * seq_number;
        } else if(bufferSize == 0 && remSize > 0) {  // Last pckt check
            memset(buffer, 0, seq_number * DATASIZE);
            fread(buffer, remSize, 1, fp);
            bufferSize = remSize;
        }
    }
    printStrSp("Finished: File is Transfering Successfully");
}

void gbn(char* file_path, int sock, struct sockaddr_in clientAddr, int fileSize) {
    FILE * fp;
    int remSize = fileSize;

    printStr("Start Sending\n");

    fp = fopen(file_path, "r");
    if (fp == NULL) {
        DieWithError("File not Found");
    }

    int seq_number = 3 * max_window_size;
    char buffer[DATASIZE * seq_number];
    struct packet packBuff[seq_number];
    int bufferSize = DATASIZE * seq_number;

    int window = max_window_size;
    int base  = 0;
    int nextSeq = 0;

    if (fread(buffer, DATASIZE, seq_number, fp) < 0)
        DieWithError("Cant read the file");

    for(; remSize > 0;) {

        for (int i = nextSeq; i < base + window && bufferSize > 0; i = (i + 1) % seq_number) {
             struct packet pck;
             memset(&pck, 0, sizeof(pck));
             pck.len = DATASIZE + HEADERSIZE;
             pck.seqno = i;

             memcpy(pck.data, buffer + (i * DATASIZE), DATASIZE);
             printStr(pck.data);

             pck.cksum = crc16(pck.data, strlen(pck.data));
             packBuff[nextSeq] = pck; // difference between stop and wait and gbn

             //LOSS Simulation
             float random = ((float)rand()/(float)(RAND_MAX));

             printStr("Send a packet");
             printNum(pck.seqno);
             if (random > plp)
                 sendto(sock, (void *)&(pck), sizeof(struct packet), 0, (struct sockaddr*) &clientAddr, sizeof(clientAddr));

             printStrSp(pck.data);


             nextSeq = (nextSeq + 1) % seq_number;
             remSize -= DATASIZE;
             bufferSize -= DATASIZE;
        }

        struct ack_packet ackPck;
        int ss = sizeof(clientAddr);
        printStrSp("Waiting Rec ack");
        //Blocking Recieve for ACK
        recvfrom(sock, &ackPck, sizeof(struct ack_packet), 0, (struct sockaddr*) &clientAddr, &ss);
        printStrSp("Ack Recieved");
        // printNum(ackPck.ackno);
        base = (ackPck.ackno + 1) % seq_number;

        if (bufferSize == 0 && remSize > DATASIZE) {
            memset(buffer, 0, seq_number * DATASIZE);
            fread(buffer, DATASIZE, seq_number, fp);
            bufferSize = DATASIZE * seq_number;
        } else if(bufferSize == 0 && remSize > 0) {
            memset(buffer, 0, seq_number * DATASIZE);
            fread(buffer, remSize, 1, fp);
            bufferSize = remSize;
        }
        sleep(2);
    }
    printStrSp("Finished: File is Transfering Successfully");
}
