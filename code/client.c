#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <string.h>
#include <signal.h>
#include "../data/common_data.h"

int main(int argc, char** argv){
    if(argc != 2){
        printf("Too few arguments\n");
        exit(1);
    }

    //disabilito sigpipe per evitare che il processo venga terminato immediatamente se il server termina
    signal(SIGPIPE, SIG_IGN);

    while(1){
        char menu_choice;
        printf("Trivia Quiz\n");
        printf("+++++++++++++++++++++++++++++++++++++\n");
        printf("Menù:\n");
        printf("1 - Comincia una sessione di Trivia\n");
        printf("2 - Esci\n");
        printf("+++++++++++++++++++++++++++++++++++++\n");
        menu_choice = fgetc(stdin);

        //se ho inserito più di un carattere porto avanti il pointer fino al new line e richiedo l'inserimento
        if(fgetc(stdin) != '\n'){
            while(fgetc(stdin) != '\n'){}
            continue;
        }

        switch(menu_choice){
            case '2':
                exit(0);
            break;
            case '1': //scelgo username e stabilisco la connessione col server
                int themes_played[MAX_THEMES];
                int n_themes_played = 0;
                int msg_dim;
                int n_read;
                int n_sent;
                time_t send_time;
                
                int server_fd;
                struct sockaddr_in server_sock;
                int ret;
            
                server_fd = socket(AF_INET, SOCK_STREAM, 0);
                check_client_error(server_fd);
                
                server_sock.sin_family = AF_INET;
                server_sock.sin_port = htons(atoi(argv[1]));
                inet_pton(AF_INET, "127.0.0.1", &server_sock.sin_addr);
                
                ret = connect(server_fd, (struct sockaddr*)&server_sock, sizeof(server_sock));
                check_client_error(ret);
                
                //invio dimensione e l'username al server
                printf("\nTrivia Quiz\n");
                printf("+++++++++++++++++++++++++++++++++++++");

                char operation_outcome[5] = "badx";
                char* username = NULL;
                size_t max_username_len = MAX_USERNAME_LEN;
                while(strcmp(operation_outcome, "good")){
                    printf("\nScegli un nickname (deve essere univoco):\n");
                    int username_len = getline(&username, &max_username_len, stdin) - 1;
                    username[username_len] = '\0';
                    msg_dim = htonl(username_len);
                    
                    n_sent = send(server_fd, &msg_dim, sizeof(int), 0);
                    check_server_disconnection(n_sent);
                    check_client_error(n_sent);
                    
                    n_sent = send(server_fd, username, username_len, 0);
                    check_server_disconnection(n_sent);
                    check_client_error(n_sent);
                    
                    //verifico esito da parte del server
                    n_read = recv(server_fd, operation_outcome, 4, 0);
                    check_client_error(n_read);
                    check_server_disconnection(n_read);
                    operation_outcome[n_read] = '\0';

                    if(strcmp(operation_outcome, "good"))
                        printf("Username già utilizzato\n");
                }

                free(username);
                //ricevo dimensione e i nomi dei temi
                char themes_buf[MAX_THEMES * (MAX_THEME_LEN + 1)] = "";
                char themes[MAX_THEMES][MAX_THEME_LEN];

                n_read = recv(server_fd, &msg_dim, sizeof(int), 0);
                check_client_error(n_read);
                check_server_disconnection(n_read);
                msg_dim = ntohl(msg_dim);

                n_read = recv(server_fd, themes_buf, msg_dim, 0);
                check_client_error(n_read);
                check_server_disconnection(n_read);
                themes_buf[n_read] = '\0';
                
                int n_choices = 0;
                while(1){    
                    //stampa i temi
                    printf("\nQuiz disponibili\n");
                    printf("+++++++++++++++++++++++++++++++++++++\n");
                    
                    if(!n_choices){
                        char* theme = strtok(themes_buf, "#");
                        for(int i = 0; theme != NULL; i++){
                            strcpy(themes[i], theme);
                            printf("%d - %s\n", n_choices + 1, theme);
                            theme = strtok(NULL, "#");
                            n_choices++;
                        }
                    }
                    else{
                        for(int i = 0; i < n_choices; i++){
                            printf("%d - %s\n", i + 1, themes[i]);
                        }
                    }

                    printf("+++++++++++++++++++++++++++++++++++++\n");
                    printf("La tua scelta: ");
                    
                    //chiedo l'input all'utente e se inserisce un comando errato o tema non presente ripropongo l'inserimento
                    char* theme_choice = NULL;
                    size_t theme_choice_len = THEME_CHOICE_LEN;
                    short repeat_condition = 1;
                    int theme_num;
                    while(repeat_condition){
                        n_read = getline(&theme_choice, &theme_choice_len, stdin) - 1;
                        theme_choice[n_read] = '\0';
                        theme_num = atoi(theme_choice);

                        if(theme_num == 0){ //riconoscimento comando
                            int i;
                            for(i = 0; i < N_COMMANDS; i++){
                                if(!strcmp(commands[i], theme_choice))
                                    break;
                            }
                            
                            //se il comando viene trovato non ripeto il ciclo
                            repeat_condition = (i < N_COMMANDS) ? 0 : 1;
                        }
                        else{ //riconoscimento numero del tema
                            for(int i = 0; i < n_themes_played; i++){
                                if(themes_played[i] == theme_num){
                                    theme_num = -1;
                                    break;
                                }
                            }
                            
                            repeat_condition = theme_num < 0 || theme_num > n_choices;
                        }

                        if(repeat_condition)
                            printf("Scegliere un tema/comando valido: ");
                    }
                    int is_theme_chosen = theme_num;

                    //invio dimensione e scelta del tema o comando
                    char msg[MAX_MSG_DIM] = "";
                    
                    if(is_theme_chosen){ //se è stato scelto un tema
                        strcpy(msg, theme_choice);

                        themes_played[n_themes_played++] = theme_num;
                        free(theme_choice);
                    }
                    else //se è stato inserito un comando
                        sprintf(msg, "%s#%lu\n", theme_choice, time(&send_time));
                    
                    msg_dim = strlen(msg);
                    
                    int dim_to_send = htonl(msg_dim);
                    ret = send(server_fd, &dim_to_send, sizeof(int), 0);
                    check_server_disconnection(n_sent);
                    check_client_error(ret);
                    
                    ret = send(server_fd, msg, msg_dim, 0);
                    check_server_disconnection(n_sent);
                    check_client_error(ret);
                    
                    //ricezione dimensione e domanda
                    if(is_theme_chosen){ //se è stato scelto un tema
                        size_t max_answer_len = MAX_TEXT_LEN;
                        for(int i = 0; i < QUESTION_NUM; i++){
                            char question[MAX_TEXT_LEN];
                            char* answer = NULL;
                            int answer_len;

                            n_read = recv(server_fd, &msg_dim, sizeof(int), 0);
                            check_client_error(n_read);
                            check_server_disconnection(n_read);
                            msg_dim = ntohl(msg_dim);
                            
                            n_read = recv(server_fd, question, msg_dim, 0);
                            check_client_error(n_read);
                            check_server_disconnection(n_read);
                            question[n_read] = '\0';
                            
                            printf("\nQuiz - %s\n", themes[theme_num - 1]);
                            printf("+++++++++++++++++++++++++++++++++++++\n");
                            printf("%s\n", question);
                            answer_len = getline(&answer, &max_answer_len, stdin) - 1;
                            if(answer_len > 0)
                                answer[answer_len] = '\0';

                            //invio dimensione e risposta
                            char answer_to_send[7 + MAX_TEXT_LEN] = "";
                            
                            if(i == QUESTION_NUM - 1)
                                sprintf(answer_to_send, "%s#%lu", answer, time(NULL)); //formato risposta finale
                            else
                                strcpy(answer_to_send, answer);
                            
                            free(answer);

                            answer_len = strlen(answer_to_send);
                            msg_dim = htonl(answer_len);
                            ret = send(server_fd, &msg_dim, sizeof(int), 0);
                            check_server_disconnection(n_sent);
                            check_client_error(ret);

                            ret = send(server_fd, answer_to_send, answer_len, 0);
                            check_server_disconnection(n_sent);
                            check_client_error(ret);
        
                            //ricevo feedback dal server (risposta corretta o no [good, badx])
                            char feedback[5];
                            n_read = recv(server_fd, feedback, 4, 0);
                            check_client_error(n_read);
                            check_server_disconnection(n_read);
                            feedback[n_read] = '\0';

                            printf("Risposta %s\n", (!strcmp(feedback, "good")) ? "Esatta" : "Errata");
                        }
                    }
                    else{//gestione comando
                        
                        if(!strcmp(theme_choice, "showscore")){
                            char score_buf[MAX_USERNAME_LEN + 3];
                            char username[MAX_USERNAME_LEN];
                            int value = 0;
                            int recvd_buf_len;
                            int score_buf_len;
                            int theme_index = 0;
                            free(theme_choice); //libero dopo che theme_choice viene valutato

                            printf("\nPunteggio tema \"%s\"\n", themes[theme_index]);
                            while(1){
                                //ricevo dimensione e la coppia (username, value)
                                n_read = recv(server_fd, &recvd_buf_len, sizeof(int), 0);
                                check_client_error(n_read);
                                check_server_disconnection(n_read);
                                score_buf_len = ntohl(recvd_buf_len);

                                n_read = recv(server_fd, score_buf, score_buf_len, 0);
                                check_client_error(n_read);
                                check_server_disconnection(n_read);
                                score_buf[n_read] = '\0';

                                sscanf(score_buf, "%[^#]#%d", username, &value);
                                if(value == -1){
                                    if(!strcmp(username, "good")){
                                        theme_index++;
                                        if(theme_index < n_choices)
                                            printf("\nPunteggio tema \"%s\"\n", themes[theme_index]);
                                        continue;
                                    }
                                    
                                    if(!strcmp(username, "done"))
                                        break;
                                }

                                printf(" - %s %d\n", username, value);
                            }
                        }

                        if(!strcmp(theme_choice, "endquiz")){
                            char bye_buf[3];
                            free(theme_choice);

                            n_read = recv(server_fd, bye_buf, 3, 0);
                            check_client_error(n_read);
                            check_server_disconnection(n_read);
                            bye_buf[3] = '\0';

                            if(!strcmp(bye_buf, "bye")){
                                close(server_fd);
                                break;
                            }
                        }
                    }
                }
            break;
        }
    }
}
