#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <ctype.h>
#include <sys/socket.h>
#include <sys/un.h> /* For AFUNIX sockets */

#include "utility.h"
#include "utilitySocket.h"

#define DEFAULT_PROTOCOL 0
#define READ 0
#define WRITE 1

// VARIABILI GLOBALI    
int speed = 0;
int pidWithArgs[2]; //THROTTLE CONTROL, FORWARD FACING RADAR
int pidWithoutArgs[3];  //STEER BY WIRE, BRAKE BY WIRE, FRONT WINDHSIELD CAMERA
int inputPid;

// PROTOTIPI DI FUNZIONE
int getInput(int hmiInputFd, int hmiFd, FILE *log);
int isNumber(char *str);
int park(int clientFd);
void endProgram(int __sig);
void throttleFailure();


int main(int argc, char *argv[])
{
    FILE *log;
    int hmiFd, hmiInputFd, throttleFd, steerFd, brakeFd;
    int anonFd[2];
    char *msg;
    createLog("./ecu", &log);
    char socketStr[16];

    int inputReader; // PID del lettore di input
    int input = 0;   // Input proveniente da HMIInput
    int stopFlag = 0;   // Verifica se la procedura di arresto è già stata eseguita

    int ecuFd, clientFd, ecuLen, clientLen;

    struct sockaddr_un ecuUNIXAddress;    /* Indirizzo server */
    struct sockaddr *ecuSockAddrPtr;      /* Puntatore a indirizzo server */
    struct sockaddr_un clientUNIXAddress; /* Indirizzo client */
    struct sockaddr *clientSockAddrPtr;   /* Puntatore a indirizzo client */

    int isListening[2] = {0, 0};
    int sensor; // Indica quale sensore desidera inviare i dati

    hmiFd = createPipe("./ecuToHmiPipe");
    hmiInputFd = openPipeOnRead("./hmiInputToEcuPipe");
    read(hmiInputFd, &inputPid, sizeof(int));

    char *componentsWithArgs[] = {"./throttlebycontrol", "./throttlebycontrol"
        , "./forwardfacingradar", "./forwardfacingradar"};
    char *componentsWithoutArgs[] = {"./steerbywire", "./steerbywire"
        , "./brakebywire", "./brakebywire", "./frontwindshieldcamera"
        , "./frontwindshieldcamera"};

    signal(SIGUSR1, throttleFailure);   // Funzione di gestione dell'errore di controllo dell'acceleratore

    remove("./assist.log");    // Rimuove il file di log assist se esiste

    // Genero tutti i processi figli
    for (int i = 0; i < 1; i++)
    {
        if ((pidWithArgs[i] = fork()) < 0)
        {
            exit(EXIT_FAILURE);
        }
        else if (pidWithArgs[i] == 0)
        {
            execl(componentsWithArgs[2*i], componentsWithArgs[2*i+1], argv[1]);
        }
    }

    for (int i = 0; i < 3; i++)
    {
        if ((pidWithoutArgs[i] = fork()) < 0)
        {
            exit(EXIT_FAILURE);
        }
        else if (pidWithoutArgs[i] == 0)
        {
            execl(componentsWithoutArgs[2*i], componentsWithoutArgs[2*i+1], 0);
        }
    }

    ecuSockAddrPtr = (struct sockaddr *)&ecuUNIXAddress;
    ecuLen = sizeof(ecuUNIXAddress);
    clientSockAddrPtr = (struct sockaddr *)&clientUNIXAddress;
    clientLen = sizeof(clientUNIXAddress);

    ecuFd = socket(AF_UNIX, SOCK_STREAM, DEFAULT_PROTOCOL);
    ecuUNIXAddress.sun_family = AF_UNIX; /* Imposta il tipo di dominio */

    strcpy(ecuUNIXAddress.sun_path, "./ecuSocket");
    unlink("./ecuSocket");

    bind(ecuFd, ecuSockAddrPtr, ecuLen);
    listen(ecuFd, 3);

    pipe2(anonFd, O_NONBLOCK);

    if ((inputReader = fork()) < 0)
    {
        exit(EXIT_FAILURE);
    }
    else if (inputReader == 0)
    { // Legge dal terminale di input HMI
        close(anonFd[READ]);
        while (1)
        {
            input = getInput(hmiInputFd, hmiFd, log);
            write(anonFd[WRITE], &input, sizeof(int));
            if (input == 3)
            {
                write(hmiFd, "PARCHEGGIO", strlen("PARCHEGGIO") + 1);
                close(anonFd[WRITE]);
                exit(EXIT_SUCCESS);
            }
        }
    }

    throttleFd = createPipe("./throttlePipe");
    steerFd = createPipe("./steerPipe");
    brakeFd = createPipe("./brakePipe");

    close(anonFd[WRITE]);

    while (1)
    {
        read(anonFd[READ], &input, sizeof(int));
        if ((input == 1 || strcmp(socketStr, "PERICOLO") == 0) && stopFlag == 0 && strcmp(socketStr, "PARCHEGGIO") != 0)
        {
            writeMessageToPipe(hmiFd, "ARRESTO AUTO");
            kill(pidWithoutArgs[1], SIGTSTP);
            stopFlag = 1;   // La procedura di arresto è stata eseguita
            speed = 0;
            isListening[0] = 0;
            isListening[1] = 0;
        }
        else if (input == 2 && strcmp(socketStr, "PARCHEGGIO") != 0)
        {
            kill(pidWithoutArgs[1], SIGCONT);
            stopFlag = 0;   // In attesa del successivo arresto da richiamare
            isListening[0] = 1;
            isListening[1] = 0;
        }
        else if (input == 3 || strcmp(socketStr, "PARCHEGGIO") == 0)
        {
            // Pre-procedura di parcheggio avviata
            input = 3;
            while (speed > 0)
            {
                writeMessage(log, "FRENO 5");
                writeMessageToPipe(hmiFd, "FRENO 5");
                writeMessageToPipe(brakeFd, "FRENO 5");
                speed -= 5;
                sleep(1);
            }
            isListening[0] = 0;
            isListening[1] = 1;
            for (int i = 0; i < 1; i++)
            {
                kill(pidWithArgs[i], SIGSTOP);
            }
            for (int i = 0; i < 3; i++)
            {
                kill(pidWithoutArgs[i], SIGSTOP);
            }
            if ((pidWithArgs[1] = fork()) == 0) // Avvio di Park Assist
            {
                execl("./parkassist", "./parkassist", argv[1]);
            }
        }
        clientFd = accept(ecuFd, clientSockAddrPtr, &clientLen);
        while (recv(clientFd, &sensor, sizeof(int), 0) < 0)
            ;
        while (send(clientFd, &isListening[sensor], sizeof(int), 0) < 0)
            ;
        memset(socketStr, '\0', 16);
        if (sensor == 0 && isListening[0] == 1)
        {
            receiveString(clientFd, socketStr);
            if (isNumber(socketStr) == 1)
            { // Se l'ECU ha ricevuto un numero...
                int requestedSpeed = atoi(socketStr);
                if (requestedSpeed > speed)
                { // Accelera
                    while (requestedSpeed > speed)
                    {
                        read(anonFd[READ], &input, sizeof(int));
                        if (input != 2)
                        {
                            break;
                        }
                        writeMessage(log, "INCREMENTO 5");
                        writeMessageToPipe(hmiFd, "INCREMENTO 5");
                        writeMessageToPipe(throttleFd, "INCREMENTO 5");
                        speed += 5;
                        sleep(1);
                    }
                }
                else if (requestedSpeed < speed)
                { // Freno
                    while (requestedSpeed < speed)
                    {
                        read(anonFd[READ], &input, sizeof(int));
                        if (input != 2)
                        {
                            break;
                        }
                        writeMessage(log, "FRENO 5");
                        writeMessageToPipe(hmiFd, "FRENO 5");
                        writeMessageToPipe(brakeFd, "FRENO 5");
                        speed -= 5;
                        sleep(1);
                    }
                }
            }
            else if (strcmp(socketStr, "SINISTRA") * strcmp(socketStr, "DESTRA") == 0)
            {   // Sterza
                int count = 0;
                while(count < 4) {
                    writeMessageToPipe(steerFd, "%s", socketStr);
                    writeMessage(log, "STERZATA A %s", socketStr);
                    writeMessageToPipe(hmiFd, "STERZATA A %s", socketStr);
                    read(anonFd[READ], &input, sizeof(int));
                    if (input != 2)
                    {
                        break;
                    }
                    sleep(1);
                    count++;
                }
            }
        }
        if (sensor == 1 && isListening[1] == 1)
        {
            if (park(clientFd) != 0)
            {
                close(clientFd);
                break;
            }
        }
        close(clientFd);
    }

    close(anonFd[READ]);
    fclose(log);
    close(hmiInputFd);
    close(hmiFd);
    endProgram(SIGINT);
    exit(EXIT_SUCCESS);
}


int getInput(int hmiInputFd, int hmiFd, FILE *log)
{ // GET INPUT FROM HMI INPUT
    char str[32];
    if (readline(hmiInputFd, str) == 0)
    {
        if (strcmp(str, "ARRESTO") == 0)
            return 1;
        else if (strcmp(str, "INIZIO") == 0)
            return 2;
        else if (strcmp(str, "PARCHEGGIO") == 0)
            return 3;
        return -1;
    }
}

int isNumber(char *str)
{ // CHECKING IF STRING IS A NUMBER
    for (int i = 0; str[i] != '\0'; i++)
    {
        if (!isdigit(str[i]))
            return 0;
    }
    return 1;
}

int park(int clientFd)
{ // PARKING METHOD
    char str[8];
    int result = 1;
    for (int count = 0; count < 120; count++)
    {
        receiveString(clientFd, str);
        result *= strcmp(str, "0x172a");
        result *= strcmp(str, "0xd693");
        result *= strcmp(str, "0x0000");
        result *= strcmp(str, "0xbdd8");
        result *= strcmp(str, "0xfaee");
        result *= strcmp(str, "0x4300");
        if (result != 0)
            result = 1;
    }
    return result;
}

void endProgram(int __sig)
{    //ENDING PROGRAM WITH DESIRED SIGNAL
    kill(pidWithoutArgs[1], SIGTSTP);
    for (int i = 0; i < 2; i++)
    {
        kill(pidWithArgs[i], __sig);
    }
    for (int i = 0; i < 3; i++)
    {
        kill(pidWithoutArgs[i], __sig);
    }
    kill(inputPid, SIGINT);
    unlink("./ecuSocket");
    unlink("./throttlePipe");
    unlink("./steerPipe");
    unlink("./brakePipe");
    unlink("./ecuToHmiPipe");
}

void throttleFailure() {    //Gestione del fallimento dell'acceleratore
    speed = 0;
    kill(pidWithoutArgs[1], SIGTSTP);
    kill(getppid(), SIGUSR1);
    endProgram(SIGUSR1);
    exit(EXIT_FAILURE);
}