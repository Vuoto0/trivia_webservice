#include <string.h>
#include <netinet/in.h>
#include "common_data.h"

struct question_t{
    char text[MAX_TEXT_LEN];
};

struct answer_t{
    char text[MAX_TEXT_LEN];
};

//dati implementativi del tema, attributi inizializzati accedendo al file delle domande
struct theme_data_t{
    char theme[MAX_THEME_LEN];
    int question_offset;
    int answer_offset;
};

//strutture per le informazioni sugli utenti
enum status_t{UNNAMED, INQUIZ, INTHEME};

struct info_t{
    char username[MAX_USERNAME_LEN];
    enum status_t status;
    int sock_fd;
    int client_given_msg_dim; //dimensione del messaggio che il client sta per inviare (-1 se ancora non nota)
    int num_answers_given;
    int index_theme_play;
    struct info_t* next;
};

//struttura per la gestione delle informazioni dei client
//implementato come lista, invece che come array, per facilità di modifica in caso di disconnessione client e endquiz
struct clients_info_t{
    struct info_t* list_base;
    int clients_num;
};

//implementato come lista, e non come array, per facilità di modifica in caso di disconnessione client e endquiz 
struct score_t{
    char username[MAX_USERNAME_LEN];
    int value;
    time_t completion_time;
    struct score_t* next;
};

//struttura per contenere le leaderboard e relativo numero di temi
struct leaderboards_t{
    struct score_t* theme_leaderboard[MAX_THEMES];
    int n_themes;
};

//struttura per la richiesta di un comando (showscore, endquiz ...)
struct command_request_t{
    char cmd[MAX_COMMAND_LEN];
    char username[MAX_USERNAME_LEN];
    int request_fd;
    time_t timestamp;
    struct command_request_t* next;
};

struct cmd_save_info_t{
    struct command_request_t* request;
    struct score_t* score;
    int theme_index;
    int dim_sent;
    char score_buf[MAX_USERNAME_LEN + 3];
    char confirm_msg[5];
};

//variabili globali per ridurre il numero di parametri da passare alle funzioni di gestione
//e per facilitare la liberazione di memoria nello heap
struct leaderboards_t ldbs;
struct clients_info_t clients_info;
struct command_request_t* cmd_list_base = NULL;

int check_socket_error(int result){
    if(errno == EPIPE || errno == ECONNRESET)
        return 1;
    
    if(result == -1){   
        perror("Error: ");
        
        //dealloco lo heap utilizzato
        for(int i = 0; i < ldbs.n_themes; i++){
            if(ldbs.theme_leaderboard[i] != NULL)
                free(ldbs.theme_leaderboard[i]);
        }
        if(clients_info.list_base != NULL) free(clients_info.list_base);
        if(cmd_list_base != NULL) free(cmd_list_base);

        //termino il server
        exit(1);
    }

    return 0;
}

//funzione per controllare eof o errori nella gestione dei file
void file_error_testing(int n_read, void* ptr){
    if(n_read <= 0){
        printf("ERROR: file error %d\n", n_read);
        free(ptr);
        exit(1);
    }
}

//funzione per aggiornare l'fd massimo per l'utilizzo della select
void update_max_fd(int* max_fd, int cur_fd){
    if(*max_fd < cur_fd)
        *max_fd = cur_fd;
}

//funzione per inserire un nuovo client nella lista dei client, modificando la base
void register_client(struct clients_info_t* clients_info){
    //se base è null, ovvero non abbiamo ancora client, alloco memoria per la base
    if(clients_info->list_base == NULL){
        clients_info->list_base = (struct info_t*)malloc(sizeof(struct info_t));
        clients_info->list_base->next = NULL;
        clients_info->clients_num = 0;
    }
    else{
        struct info_t* new_elem = (struct info_t*)malloc(sizeof(struct info_t));
        new_elem->next = clients_info->list_base;
        clients_info->list_base = new_elem;
    }

    clients_info->list_base->status = UNNAMED;
    clients_info->list_base->num_answers_given = 0;
    clients_info->list_base->client_given_msg_dim = -1;
    clients_info->list_base->index_theme_play = -1;
    strcpy(clients_info->list_base->username, "");
}

//trova l'utente che corrisponde al socket fd
struct info_t* find_user(struct info_t* base, int fd){
    for(struct info_t* p = base; p != NULL; p = p->next){
        if(p->sock_fd == fd)
            return p;
    }
    return NULL;
}

void init_leaderboards(struct leaderboards_t* ldbs, int n_themes){
    ldbs->n_themes = n_themes;
    for(int i = 0; i < MAX_THEMES; i++)
        ldbs->theme_leaderboard[i] = NULL;
}

//value vale 1 se la risposta alla domanda è corretta, altrimenti 0
void update_leaderboard(struct score_t** leaderboard, const char* username, int value, time_t timestamp){
    //se la lista è vuota creo la lista e inizializzo il primo punteggio
    if(*leaderboard == NULL){
        *leaderboard = (struct score_t*)malloc(sizeof(struct score_t));
        (*leaderboard)->next = NULL;
        strcpy((*leaderboard)->username, username);
        (*leaderboard)->value = value;
    }
    else{
        //se la lista e non è vuota inserisco un nuovo elemento in ordine di punteggio decrescente
        struct score_t* new_elem;

        struct score_t* reposition_p = *leaderboard; //puntatore al primo score con punteggio +1 rispetto a quelli degli score correnti 
        struct score_t* q = NULL;
        struct score_t* p = *leaderboard;
        while(p != NULL){
            //se value del corrente è minore di quello precedente aggiorno reposition_p
            if(q != NULL && q->value > p->value){
                reposition_p = q;
            }

            //se trovo il punteggio precedente lo elimino
            if(!strcmp(p->username, username)){
                new_elem = p;
                if(q != NULL)
                    q->next = p->next;
                break;
            }
            
            q = p;
            p = p->next;
        }

        if(p == NULL){
            new_elem = (struct score_t*)malloc(sizeof(struct score_t));
            strcpy(new_elem->username, username);
            reposition_p = q;
            new_elem->value = 0;
        }

        //aggiorno il valore
        new_elem->value += value;
        
        //se lo score dell'user username è la base allora non faccio altro
        if(p == *leaderboard){
            (*leaderboard)->completion_time = timestamp;
            return;
        }

        //rendo il nuovo elemento base se ha punteggio maggiore della base
        if(new_elem->value > (*leaderboard)->value){
            new_elem->next = *leaderboard;
            *leaderboard = new_elem;
        }
        else{ //sposto l'elemento nella posizione corretta
            new_elem->next = reposition_p->next;
            reposition_p->next = new_elem;
        }

        new_elem->completion_time = timestamp;
    }
}

//rimuove un giocatore dalle leaderboard
void remove_from_leaderboard(struct leaderboards_t* ldbs, const char* username){
    for(int i = 0; i < ldbs->n_themes; i++){
        struct score_t* p = ldbs->theme_leaderboard[i];
        struct score_t* q = NULL;
        while(p != NULL){
            if(!strcmp(p->username, username)){
                if(q == NULL)
                    ldbs->theme_leaderboard[i] = p->next;
                else
                    q->next = p->next;
                
                free(p);
                break;
            }

            q = p;
            p = p->next;
        }
    }
}

//rimuove un giocatore dalla lista dei client
void remove_player(struct clients_info_t* info, int fd){
    struct info_t* p = info->list_base;
    struct info_t* q = NULL;
    
    if(info->clients_num > 0) info->clients_num--;

    while(p != NULL){
        if(p->sock_fd == fd){
            if(q == NULL)
                info->list_base = p->next;
            else
                q->next = p->next;
            
            free(p);
            return;
        }
        
        q = p;
        p = p->next;
    }
}

//inserisce nella coda dei comandi un comando con relative informazioni
void update_cmd_queue(struct command_request_t** base, const char* cmd, const char* username, int fd, time_t timestamp){
    if(*base == NULL){
        *base = (struct command_request_t*)malloc(sizeof(struct command_request_t));
        (*base)->next = NULL;
        strcpy((*base)->cmd, cmd);
        strcpy((*base)->username, username);
        (*base)->request_fd = fd;
        (*base)->timestamp = timestamp;
    }
    else{
        struct command_request_t* p = *base;
        struct command_request_t* q = NULL;
        
        while(p != NULL && p->timestamp < timestamp){
            q = p;
            p = p->next;
        }

        //se base ha timestamp maggiore lo metto in base e sposto base
        struct command_request_t* new_elem = (struct command_request_t*)malloc(sizeof(struct command_request_t));
        new_elem->next = p;
        if(q == NULL)
            *base = new_elem;
        else
            q->next = new_elem;
    
        strcpy(new_elem->cmd, cmd);
        strcpy(new_elem->username, username);
        new_elem->request_fd = fd;
        new_elem->timestamp = timestamp;
    }
}

//salva le informazioni dei comandi
void init_cmd_save_info(struct cmd_save_info_t* cmd_info){
    cmd_info->request = NULL;
    cmd_info->score = NULL;
    cmd_info->theme_index = -1; 
    cmd_info->dim_sent = -1;
    strcpy(cmd_info->confirm_msg, "");
    strcpy(cmd_info->score_buf, "");
}

//chiamata quando un client sceglie il suo username, dopo aver aggiornato clients_info
//o quando un client si disconnette
void print_players(struct clients_info_t info){
    printf("Partecipanti: (%d)\n", info.clients_num);

    for(struct info_t* p = info.list_base; p != NULL; p = p->next)
        printf(" - %s\n", p->username);

    printf("\n");
}

//funzione per verificare se il client si è disconnesso
int check_client_disconnection(int result, struct leaderboards_t* ldbs, struct clients_info_t* clients_info, const char* username, int fd, fd_set* set){
    if(!result){
        if(username != NULL) remove_from_leaderboard(ldbs, username);
        remove_player(clients_info, fd);
        FD_CLR(fd, set);
        close(fd);
        print_players(*clients_info);    

        return 1;
    }
    return 0;
}
