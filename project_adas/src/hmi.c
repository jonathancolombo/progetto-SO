#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h> /* For AFUNIX sockets */
#include <string.h>
#include <signal.h>
#include <sys/wait.h>

#define SIGSTART SIGUSR1
#define SIGPARK SIGUSR1
FILE *fileUtility;

pid_t ecuProcessPID; // pid del processo ECU
pid_t tailProcessPID; // pid del processo coda
int started = 0;
char **g_argv;

void sigParkHandler();
void sigWarningHandler();
void openFile(char filename[], char mode[], FILE **filePointer);

int main(int argc, char *argv[])
{
    g_argv = argv;
    /*
    * Controlla la tipologia di AVVIO impostata e restituisce un errore
     */

    if ((argc < 2) || (strcmp(argv[1] , "ARTIFICIALE") != 0 && strcmp(argv[1] , "NORMALE") != 0))
    {
        printf("Inserisci il comando di avvio NORMALE o ARTIFICIALE");
        //exit(EXIT_FAILURE);
    }

    // fork del processo ECU
    ecuProcessPID = fork();
    // controllo se il processo ECU è stato creato correttamente
    if (ecuProcessPID == -1)
    {
        perror("fork error");
        //exit (EXIT_FAILURE);
    } 
    else if (ecuProcessPID == 0)
    {
        // imposto il pgid ed eseguo una execv
        printf("Sono il processo figlio\n");
        setpgid(0, 0);
        argv[0] = "./ecu";
        execv(argv[0], argv);
        //exit(EXIT_SUCCESS);
    }
    else
    {
        tailProcessPID = fork();
        if (tailProcessPID < 0)
        {
            perror("fork error");
            exit (EXIT_FAILURE);
        }
        if (tailProcessPID == 0)
        {
            system("rm -f ECU.log; touch ECU.log; gnome-terminal -- sh -c \"echo OUTPUT:; tail -f ECU.log; bash\"");
        }
        else
        {
            // 3 signal da fare
            signal(SIGUSR1, sigParkHandler);
            signal(SIGUSR2, sigWarningHandler);
            signal(SIGINT, sigParkHandler);


            openFile("utility.data", "w", &fileUtility);

            if (fileUtility == NULL)
            {
                perror("open file error!");
                //exit (EXIT_FAILURE);
            }

            fprintf(fileUtility, "%d\n", 0);
            fclose(fileUtility); // chiusura file Utility

            openFile("ECU.log", "w", &fileUtility);

            if (fileUtility == NULL)
            {
                perror("open file error!");
                //exit (EXIT_FAILURE);
            }
            fprintf(fileUtility, "%d\n", 0);
            fclose(fileUtility); // chiusura file ECU.log

            printf("Ciao! Benvenuto nel simulatore di sistemi di guida autonoma. \nDigita INIZIO per avviare il veicolo,\no digita PARCHEGGIO per avviare la procedura di parcheggio e concludere il percorso.\n\n");

            // controllo comando d'input iniziale
            char input[30];
            started = 0;
            while(1)
            {
                if(fgets(input, 30, stdin) != NULL)
                {
                    if((started) == 0)
                    {
                        if(strcmp(input, "INIZIO\n") == 0)
                        {
                            printf("Veicolo avviato\n");
                            kill(ecuProcessPID, SIGSTART);
                            started = 1;
                        }
                        else if (strcmp(input, "PARCHEGGIO\n") == 0)
                        {
                            printf("Prima di poter parcheggiare devi avviare il veicolo.\nDigita INIZIO per avviare il veicolo.\n\n");
                        }
                        else
                        {
                            printf("Comando non ammesso.\n\n");
                        }
                    }
                    else
                    {
                        if(strcmp(input, "PARCHEGGIO\n") == 0)
                        {
                            printf("Sto fermando il veicolo...\n");
                            kill(ecuProcessPID, SIGPARK);
                            started = 0;
                        }
                        else
                        {
                            printf("Comando non ammesso. \nDigita PARCHEGGIO per parcheggiare il veicolo\n\n");
                        }
                    }
                }
            }
        }

        //printf("Sono il processo padre, il PID del figlio è %d\n", ecuProcessID);
        //exit(EXIT_SUCCESS);
    }
}


void sigParkHandler() {
    kill(ecuProcessPID, SIGTERM);
    kill(tailProcessPID, SIGTERM);
    kill(0, SIGTERM);

}

void recreateEcu() {
	ecuProcessPID = fork();
    if(ecuProcessPID<0) {
        perror("fork");
        exit(1);
    }
    if(ecuProcessPID == 0) {
    	setpgid(0, 0);
        execv("./ecu", g_argv);
    }
}

void sigWarningHandler() {
	signal(SIGUSR2, sigWarningHandler);
	kill(ecuProcessPID, SIGTERM);
	recreateEcu();
	printf("La macchina è stata arrestata per evitare un pericolo. \nPremi INIZIO per ripartire\n\n");
	started = 0;
}



void openFile(char filename[], char mode[], FILE **filePointer) {
	*filePointer = fopen(filename, mode);
	if (*filePointer == NULL) {
		printf("Errore nell'apertura del file");
		exit(1);
	}
}