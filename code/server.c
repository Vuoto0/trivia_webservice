#include <signal.h>
#include "../data/server_aux_funcs.h"

int main(){
    int listen_fd;
    struct sockaddr_in listen_sock;
    struct theme_data_t theme_data[MAX_THEMES];
    struct cmd_save_info_t cmd_save_info;
    FILE* questions_fptr = NULL;
    FILE* answers_fptr = NULL;
    fd_set main_set, read_set, write_set;
    int max_read_fd = 0;
    int ret;
    char good_msg[] = "good";
    char bad_msg[] = "badx";
    char done_msg[] = "done";
    char bye_msg[] = "bye";
    
    clients_info.list_base = NULL;
    
    //disabilito sigpipe per evitare che il processo venga terminato immediatamente se il client termina
    signal(SIGPIPE, SIG_IGN);

    listen_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    check_socket_error(listen_fd);

    listen_sock.sin_family = AF_INET;
    listen_sock.sin_port = htons(8080);
    inet_pton(AF_INET, "127.0.0.1", &listen_sock.sin_addr);

    ret = bind(listen_fd, (struct sockaddr*)&listen_sock, sizeof(listen_sock));
    check_socket_error(ret);

    ret = listen(listen_fd, MAX_CONN_REQUESTS);
    check_socket_error(ret);
    
    FD_ZERO(&main_set);
    FD_ZERO(&read_set);
    FD_ZERO(&write_set);
    FD_SET(listen_fd, &main_set);
    
    max_read_fd = listen_fd;
    clients_info.list_base = NULL;
    init_cmd_save_info(&cmd_save_info);

    print_theme_names(&questions_fptr, theme_data);
    answers_fptr = fopen("../files/answers.txt", "r");

    while(1){
        read_set = main_set;

        ret = select(max_read_fd + 1, &read_set, NULL, NULL, NULL);
        check_socket_error(ret);
        
        for(int i = 0; i <= max_read_fd; i++){
            if(FD_ISSET(i, &read_set)){
                if(i == listen_fd){
                    //accetto la connect del client e lo registro tra i client
                    struct sockaddr_in client_sock;
                    socklen_t client_sock_len = sizeof(client_sock);
                    register_client(&clients_info);
                    
                    int client_fd = accept(listen_fd, (struct sockaddr*)&client_sock, &client_sock_len);
                    check_socket_error(client_fd);

                    FD_SET(client_fd, &main_set);
                    update_max_fd(&max_read_fd, client_fd);
                    clients_info.list_base->sock_fd = client_fd;
                    continue;
                }
                
                //utente associato all'fd i
                struct info_t* user_info = find_user(clients_info.list_base, i);

                switch(user_info->status){
                    case UNNAMED: //server non ha ancora ricevuto l'username
                        int n_read;

                        if(user_info->client_given_msg_dim == -1){
                            ret = recv_msg_dim(i, &user_info->client_given_msg_dim);
                            if(check_client_disconnection(ret, &ldbs, &clients_info, NULL, i, &main_set))
                                break;
                        }
                        else{
                            char username[MAX_USERNAME_LEN];
                            n_read = recv(i, username, user_info->client_given_msg_dim, 0);
                            check_socket_error(n_read);
                            if(check_client_disconnection(n_read, &ldbs, &clients_info, NULL, i, &main_set)) break;
                            username[n_read] = '\0';
                            user_info->client_given_msg_dim = -1;

                            //controllo utilizzo del nome
                            struct info_t* p = clients_info.list_base;
                            while(p != NULL){
                                if(p->status != UNNAMED && !strcmp(p->username, username))
                                    break;
                                
                                p = p->next;
                            }

                            if(p != NULL){ //se trovato invio bad
                                ret = send(i, &bad_msg, 4, 0);
                                if(check_socket_error(ret)) check_client_disconnection(ret, &ldbs, &clients_info, NULL, i, &main_set);
                                break;
                            } //altrimenti invio good
                            ret = send(i, &good_msg, 4, 0);
                            if(check_socket_error(ret) && check_client_disconnection(ret, &ldbs, &clients_info, NULL, i, &main_set))
                                break;

                            strcpy(user_info->username, username);
                            user_info->status = INQUIZ;

                            clients_info.clients_num++;
                            print_players(clients_info);
                    
                            if(send_themes(theme_data, i) && check_client_disconnection(ret, &ldbs, &clients_info, user_info->username, i, &main_set))
                                break;
                        }
                    break;
                    case INQUIZ: //l'utente ha ricevuto i temi e ha inviato la scelta del tema o un comando
                        if(user_info->client_given_msg_dim == -1){
                            ret = recv_msg_dim(i, &user_info->client_given_msg_dim);
                            if(check_client_disconnection(ret, &ldbs, &clients_info, user_info->username, i, &main_set))
                                break;
                        }
                        else{
                            char msg[MAX_MSG_DIM] = "";
                            n_read = recv(i, msg, user_info->client_given_msg_dim, 0);
                            check_socket_error(n_read);
                            if(check_client_disconnection(n_read, &ldbs, &clients_info, user_info->username, i, &main_set))
                                break;

                            msg[n_read] = '\0';
                            user_info->client_given_msg_dim = -1;

                            char command[MAX_COMMAND_LEN] = "";
                            char remaining_msg[MAX_MSG_DIM - MAX_COMMAND_LEN] = "";
                            sscanf(msg, "%[^#]#%[^\n]", command, remaining_msg);

                            if(!strlen(remaining_msg)){
                                user_info->index_theme_play = atoi(command) - 1;
                                if(send_question(questions_fptr, i, theme_data[user_info->index_theme_play].question_offset, user_info->num_answers_given))
                                    check_client_disconnection(ret, &ldbs, &clients_info, user_info->username, i, &main_set);
                                user_info->status = INTHEME;
                                break;
                            }

                            update_cmd_queue(&cmd_list_base, command, user_info->username, i, (time_t)atol(remaining_msg));
                            break;
                        }
                    break;
                    case INTHEME:
                        if(user_info->client_given_msg_dim == -1){
                            ret = recv_msg_dim(i, &user_info->client_given_msg_dim);
                            if(check_client_disconnection(ret, &ldbs, &clients_info, user_info->username, i, &main_set))
                                break;
                        }
                        else{
                            char msg[MAX_MSG_DIM] = "";
                            n_read = recv(i, msg, user_info->client_given_msg_dim, 0);
                            check_socket_error(n_read);
                            if(check_client_disconnection(n_read, &ldbs, &clients_info, user_info->username, i, &main_set))
                                break;
                            msg[n_read] = '\0';
                            user_info->client_given_msg_dim = -1;
                            
                            char answer_recvd[MAX_COMMAND_LEN] = "";
                            char timestamp_recvd[MAX_MSG_DIM - MAX_COMMAND_LEN] = "";
                            sscanf(msg, "%[^#]#%[^\n]", answer_recvd, timestamp_recvd);
                            
                            char* answer_buf = NULL;
                            size_t text_len = MAX_TEXT_LEN;
                            struct theme_data_t cur_theme = theme_data[user_info->index_theme_play]; 

                            fseek(answers_fptr, cur_theme.answer_offset, SEEK_SET);
                            skip_lines(answers_fptr, user_info->num_answers_given);

                            n_read = getline(&answer_buf, &text_len, answers_fptr);
                            file_error_testing(n_read, answer_buf);
                            answer_buf[n_read - 1] = '\0';
                            
                            //se la risposta è corretta invio good e aggiorno il punteggio
                            int is_answer_right = !strcmp(answer_buf, answer_recvd); 
                            if(is_answer_right)
                                ret = send(i, good_msg, 4, 0);
                            else
                                ret = send(i, bad_msg, 4, 0);
                            if(check_socket_error(ret) && check_client_disconnection(ret, &ldbs, &clients_info, user_info->username, i, &main_set))
                                break;
                            
                            time_t timestamp = (user_info->num_answers_given == 4) ? (time_t)atol(timestamp_recvd) : 0;
                            update_leaderboard(&(ldbs.theme_leaderboard[user_info->index_theme_play]), user_info->username, is_answer_right, timestamp);

                            free(answer_buf);                                
                            user_info->num_answers_given++;
                            
                            print_leaderboards(theme_data);

                            //se è l'ultima domanda del tema mi fermo
                            if(user_info->num_answers_given == QUESTION_NUM){
                                user_info->num_answers_given = 0;
                                user_info->index_theme_play = -1;
                                user_info->status = INQUIZ;
                                break;
                            }

                            //invio la domanda successiva
                            if(send_question(questions_fptr, i, cur_theme.question_offset, user_info->num_answers_given))
                                check_client_disconnection(ret, &ldbs, &clients_info, user_info->username, i, &main_set);
                        }
                    break;
                }
            }
        }

        //eseguo i comandi in ordine di timestamp
        if(cmd_list_base != NULL){
            struct command_request_t* q;
            struct command_request_t* p = (cmd_save_info.request == NULL) ? cmd_list_base : cmd_save_info.request;
            
            while(p != NULL){
                if(!strcmp(p->cmd, "showscore")){
                    int fd_not_ready = 0;
                    int client_disconnected = 0;
                    FD_SET(p->request_fd, &write_set);
                    
                    int len_to_send;
                    char end_msg[9];
                    int j = (cmd_save_info.theme_index == -1) ? 0 : cmd_save_info.theme_index;
                    while(strcmp(cmd_save_info.confirm_msg, done_msg) && j < ldbs.n_themes){
                        struct score_t* score_p = (cmd_save_info.score == NULL) ? ldbs.theme_leaderboard[j] : cmd_save_info.score;
                        while(strcmp(cmd_save_info.confirm_msg, good_msg) && score_p != NULL){
                            //invio dimensione e score se completion_time del tema è < timestamp del comando 
                            //se completion_time == 0 allora il tema non è ancora stato completato
                            //quindi il server non invia lo score di quell'utente
                            if(!score_p->completion_time || p->timestamp < score_p->completion_time){
                                score_p = score_p->next;
                                continue;
                            }
                            
                            if(cmd_save_info.dim_sent == -1){
                                //polling 1 - se fd non pronto => dim_sent = -1 salvo p, score_p, j e faccio break
                                if(showscore_poll(&cmd_save_info, p, score_p, write_set, j, "")){
                                    fd_not_ready = 1;
                                    break;
                                }
                                
                                char score_buf[MAX_USERNAME_LEN + 3];
                                cmd_save_info.dim_sent = sprintf(score_buf, "%s#%d", score_p->username, score_p->value);
                                len_to_send = htonl(cmd_save_info.dim_sent);
                                strcpy(cmd_save_info.score_buf, score_buf);
                                
                                ret = send(p->request_fd, &len_to_send, sizeof(int), 0);
                                if(check_socket_error(ret) && check_client_disconnection(ret, &ldbs, &clients_info, p->username, p->request_fd, &main_set)){
                                    client_disconnected = 1;
                                    break;
                                }
                            }

                            //polling 2 - se fd non pronto => dim_sent, salvo p, score_p, j e faccio break
                            if(showscore_poll(&cmd_save_info, p, score_p, write_set, j, "")){
                                fd_not_ready = 1;
                                break;
                            }

                            ret = send(p->request_fd, cmd_save_info.score_buf, cmd_save_info.dim_sent, 0);
                            if(check_socket_error(ret) && check_client_disconnection(ret, &ldbs, &clients_info, p->username, p->request_fd, &main_set)){
                                client_disconnected = 1;
                                break;
                            }

                            //resetto la dimensione da mandare
                            cmd_save_info.dim_sent = -1;

                            score_p = score_p->next;
                        }
                        
                        //se il client si è disconnesso passo alla prossima richiesta
                        if(client_disconnected) break;
                        
                        //sono alla fine della leaderboard del tema
                        //polling senza score perchè va inizializzato alla base del prossimo tema
                        //e segnalo la fine della leaderboard del tema j
                        if(cmd_save_info.dim_sent == -1){
                            if(showscore_poll(&cmd_save_info, p, NULL, write_set, j, good_msg)){
                                fd_not_ready = 1;
                                break;
                            }
                        
                            cmd_save_info.dim_sent = sprintf(end_msg, "%s#%d", good_msg, -1);
                            len_to_send = htonl(cmd_save_info.dim_sent);
                            strcpy(cmd_save_info.score_buf, end_msg);
                            
                            ret = send(p->request_fd, &len_to_send, sizeof(int), 0);
                            if(check_socket_error(ret) && check_client_disconnection(ret, &ldbs, &clients_info, p->username, p->request_fd, &main_set)){
                                client_disconnected = 1;
                                break;
                            }
                        }

                        if(showscore_poll(&cmd_save_info, p, score_p, write_set, j, good_msg)){
                            fd_not_ready = 1;
                            break;
                        }
                        
                        ret = send(p->request_fd, cmd_save_info.score_buf, cmd_save_info.dim_sent, 0);
                        if(check_socket_error(ret) && check_client_disconnection(ret, &ldbs, &clients_info, p->username, p->request_fd, &main_set)){
                            client_disconnected = 1;
                            break;
                        }

                        //resetto la dimensione da mandare
                        cmd_save_info.dim_sent = -1;
                        strcpy(cmd_save_info.confirm_msg, "");
                        j++;
                    }

                    //se ho riscontrato l'fd non pronto allora rimando la gestione del comando
                    if(fd_not_ready)
                        break;

                    //se il client si è disconnesso continuo con la gestione delle richieste
                    if(client_disconnected) continue;
                    
                    //sono alla fine dei temi
                    //polling senza request e score perchè ho finito le leaderboard
                    //e segnalo di aver inviato tutte le leaderboard
                    if(cmd_save_info.dim_sent == -1){
                        if(showscore_poll(&cmd_save_info, p, NULL, write_set, -1, done_msg))
                            break;
                        
                        cmd_save_info.dim_sent = sprintf(end_msg, "%s#%d", done_msg, -1);
                        len_to_send = htonl(cmd_save_info.dim_sent);
                        strcpy(cmd_save_info.score_buf, end_msg);

                        ret = send(p->request_fd, &len_to_send, sizeof(int), 0);
                        if(check_socket_error(ret) && check_client_disconnection(ret, &ldbs, &clients_info, p->username, p->request_fd, &main_set))
                            continue;
                    }

                    //se l'fd non è pronto in scrittura allora continuo con le prossime richieste
                    if(showscore_poll(&cmd_save_info, p, NULL, write_set, -1, done_msg))
                        break;
                    
                    ret = send(p->request_fd, cmd_save_info.score_buf, cmd_save_info.dim_sent, 0);
                    if(check_socket_error(ret) && check_client_disconnection(ret, &ldbs, &clients_info, p->username, p->request_fd, &main_set))
                        continue;

                    //resetto cmd_info e rimuovo l'fd della richiesta dal set di scrittura 
                    init_cmd_save_info(&cmd_save_info);
                    FD_CLR(p->request_fd, &write_set);
                }

                if(!strcmp(p->cmd, "endquiz")){
                    ret = send(p->request_fd, bye_msg, 3, 0);
                    if(check_socket_error(ret) && check_client_disconnection(ret, &ldbs, &clients_info, p->username, p->request_fd, &main_set))
                        continue;

                    //rimuovo dal set principale l'fd e le informazioni relative all'utente
                    remove_from_leaderboard(&ldbs, p->username);
                    remove_player(&clients_info, p->request_fd);
                    FD_CLR(p->request_fd, &main_set);
                    close(p->request_fd);
                }
                
                //rimuovo la command_request
                q = p;
                p = p->next;
                free(q);
            }
            
            //se ho terminato di gestire le richieste stampo le informazioni
            if(p == NULL){
                cmd_list_base = NULL;
                print_players(clients_info);
                print_leaderboards(theme_data);
            }
        }

        //sigpipe sono disattivate perché causerebbero l'immediata terminazione in caso di disconnessione dell'altro host

        //il client si rende conto della disconnessione del server appena tenta di inviare qualcosa; termina mandando a video un messaggio
        //il server si rende conto della disconnessione del client appena legge con recv zero caratteri
        //in tal caso il server provvede ad eliminare tutte le informazioni relative al client appena disconnesso
        
        //che struttura dati usare per riordinare le richieste di comandi?
        //command_request* che sarà la base di una lista di richieste di comandi, ordinata per timestamp crescenti
        //richieste inserite appena rilevate accedendo al read_set dopo la select
        
        //"good" / "badx" -> risposte alle domande ricevute correttamente / errore nella ricezione
    }
    
    return 0;
}