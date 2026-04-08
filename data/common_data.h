#include <time.h>
#include <errno.h>
#include "constants.h"

int check_client_error(int result){
    if(errno == EPIPE || errno == ECONNRESET)
        return 1;
    
    if(result == -1){   
        perror("Error: ");
        exit(1);
    }

    return 0;
}

//funzione per controllare se il server si è disconnesso
void check_server_disconnection(int result){
    if(!result || errno == EPIPE || errno == ECONNRESET){
        printf("Server disconnected\n");
        exit(0);
    }
}

char commands[N_COMMANDS][MAX_COMMAND_LEN] = {"showscore", "endquiz"};
