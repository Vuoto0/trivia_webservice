#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <sys/time.h>
#include "../data/server_data.h"

//stampa i nomi dei temi, scrive nell'oggetto leaderboards il numero dei temi e scrive il nome dei temi nella struttura theme_data
//n temi scritto in cima al file, offset delle domande ottenuto con ftells(), 
//offset nel file risposte scritto tra nome tema e domande
void print_theme_names(FILE** questions_fptr, struct theme_data_t* t_data){
    char* n_themes_ptr = NULL;
    int n_themes;
    size_t max_themes = MAX_NUMBER_THEMES_CHARS;
    size_t max_theme_len = MAX_THEME_LEN;
    size_t max_offset_len = MAX_FILE_OFFSET_CHARS; //numero caratteri massimo dell'offset nel file delle risposte
    ssize_t n_read;

    *questions_fptr = fopen("../files/questions.txt", "r");
    
    //leggi il numero di temi
    n_read = getline(&n_themes_ptr, &max_themes, *questions_fptr);
    if(n_read > MAX_NUMBER_THEMES_CHARS){
        printf("ERROR: too many themes\n");
        free(n_themes_ptr);
        exit(1);
    }
    
    n_themes = atoi(n_themes_ptr);
    free(n_themes_ptr);

    //stampa temi
    printf("Trivia Quiz\n");
    printf("+++++++++++++++++++++++++++++++++++++\n");
    printf("Temi: ");
    for(int i = 0; i < n_themes; i++){
        char* name_buf = NULL;
        char* answer_offset_buf = NULL;

        //imposta l'offset della domanda relativo al file delle domande e legge il nome del tema
        n_read = getline(&name_buf, &max_theme_len, *questions_fptr);
        file_error_testing(n_read, name_buf);
        name_buf[n_read - 1] = '\0';
        
        //stampa il nome del tema
        strcpy(t_data[i].theme, name_buf);
        printf("\n%d - %s", i + 1, name_buf);
        free(name_buf);
        
        //legge l'offset della risposta relativo al file delle risposte
        n_read = getline(&answer_offset_buf, &max_offset_len, *questions_fptr);
        file_error_testing(n_read, answer_offset_buf);
        answer_offset_buf[n_read - 1] = '\0';
        t_data[i].answer_offset = atoi(answer_offset_buf);
        free(answer_offset_buf);
        
        //salva l'offset delle domande
        t_data[i].question_offset = ftell(*questions_fptr);
        //sposta il file pointer QUESTION_NUM righe in avanti
        for(int j = 0; j < QUESTION_NUM; j++){
            char* skip_buf = NULL;
            size_t skip_buf_len = MAX_TEXT_LEN;
            getline(&skip_buf, &skip_buf_len, *questions_fptr);
            free(skip_buf);
        }
    }
    printf("\n+++++++++++++++++++++++++++++++++++++\n\n");
    
    init_leaderboards(&ldbs, n_themes);
}

//stampa le classifiche per ogni tema, con le classifiche correnti
//stampa anche la lista degli utenti che hanno giocato ad un tema, per ognuno di essi
//chiamata ad ogni messaggio ricevuto se status client è INTHEME
void print_leaderboards(struct theme_data_t* theme_data){
    for(int i = 0; i < ldbs.n_themes; i++){
        printf("\nPunteggio \"%s\"\n", theme_data[i].theme);
        for(struct score_t* p = ldbs.theme_leaderboard[i]; p != NULL; p = p->next)
            printf(" - %s %d\n", p->username, p->value);
    }
    printf("\n");

    for(int i = 0; i < ldbs.n_themes; i++){
        printf("\nQuiz \"%s\" completato\n", theme_data[i].theme);
        for(struct score_t* p = ldbs.theme_leaderboard[i]; p != NULL; p = p->next){
            if(p->completion_time)
                printf(" - %s\n", p->username);
        }
    }

    printf("\n+++++++++++++++++++++++++++++++++++++\n\n");
}

//funzione per ricevere le dimensioni dei messaggi da parte del client
int recv_msg_dim(int fd, int* dim){
    int msg_dim;
    int n_read;
    n_read = recv(fd, &msg_dim, sizeof(int), 0);
    check_socket_error(n_read);

    msg_dim = ntohl(msg_dim);
    *dim = msg_dim;
    return n_read;
}

void skip_lines(FILE* file, int n_lines_to_skip){
    char* buf = NULL;
    size_t len = MAX_TEXT_LEN;

    for(int j = 0; j < n_lines_to_skip; j++){ 
        getline(&buf, &len, file);
        free(buf);
        buf = NULL;
    }
}

int send_themes(struct theme_data_t* theme_data, int fd){
    //invio dimensione e nomi temi al client
    int ret;
    char theme_buf[MAX_THEMES * (MAX_THEME_LEN + 1)] = "";
    int total_themes_len = 0;
    
    for(int j = 0; j < ldbs.n_themes; j++){
        char work_buf[MAX_THEME_LEN + 1];
        total_themes_len += sprintf(work_buf, "%s#", theme_data[j].theme);
        strcat(theme_buf, work_buf);
    }
    
    int len_to_send = htonl(total_themes_len);
    ret = send(fd, &len_to_send, sizeof(int), 0);
    if(check_socket_error(ret)) return 1;

    ret = send(fd, theme_buf, total_themes_len, 0);
    if(check_socket_error(ret)) return 1;

    return 0;
}

int send_question(FILE* fptr, int fd, int question_offset, int num_questions_to_skip){
    char* temp_question_buf = NULL;
    size_t text_len = MAX_TEXT_LEN;
    int ret, n_read;

    //sposto il pointer alla domanda da inviare
    fseek(fptr, question_offset, SEEK_SET);
    skip_lines(fptr, num_questions_to_skip);

    //leggo la domanda da inviare
    n_read = getline(&temp_question_buf, &text_len, fptr);
    file_error_testing(n_read, temp_question_buf);
    temp_question_buf[n_read - 1] = '\0'; 

    char question_buf[n_read];
    strcpy(question_buf, temp_question_buf);
    free(temp_question_buf);

    int len_to_send = htonl(n_read - 1);
    ret = send(fd, &len_to_send, sizeof(int), 0);
    if(check_socket_error(ret)) return 1;
    
    ret = send(fd, question_buf, n_read - 1, 0);
    if(check_socket_error(ret)) return 1;
    
    return 0;
}

//funzione per effettuare il polling sui descrittori in scrittura per il comando showscore
//attributo dim_sent di save_info modificato durante l'invio dei punteggi, non in questa funzione
//ritorna 1 se il descrittore non è pronto
//0 altrimenti
int showscore_poll(struct cmd_save_info_t* save_info, struct command_request_t* request_p, struct score_t* score_p, fd_set write_set, int theme_index, const char* confirm_msg){
    struct timeval polling_timeout;
    polling_timeout.tv_sec = 0;
    polling_timeout.tv_usec = 0;

    select(request_p->request_fd + 1, NULL, &write_set, NULL, &polling_timeout);
    if(!FD_ISSET(request_p->request_fd, &write_set)){
        save_info->request = request_p;
        save_info->score = score_p;
        save_info->theme_index = theme_index;
        strcpy(save_info->confirm_msg, confirm_msg);
        return 1;
    }

    return 0;
}
