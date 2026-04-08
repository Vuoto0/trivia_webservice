# trivia_webservice
<h3> Gestione delle Richieste </h3>
Il server è stato realizzato tramite I/O Multiplexing e socket non bloccanti per facilitare la
gestione delle strutture dati relative alla leaderboard che richiederebbero accesso in
mutua esclusione in un server multiprocesso concorrente.

Il server effettua un controllo iniziale su read_set, bloccandosi con la select, poi quando
un fd diventa pronto gestisce la richiesta, se l’fd è listen_fd accetta la richiesta di
connessione inserendo le informazioni relative nella struttura clients_info.

Se la richiesta è l’esecuzione di un comando allora si rimanda la sua gestione
inserendola nella lista cmd_list_base, questo perchè il server controlla gli fd in ordine
crescente e non necessariamente rispecchia l’ordine di arrivo delle richieste; infatti il
client, oltre al comando vero e proprio, invia anche un timestamp in modo da permettere
al server di ordinare le richieste.

I comandi showscore vengono gestiti effettuando polling sul fd richiedente verificando se
pronto in scrittura, questo perchè la classifica può essere molto grande e il buffer del set
può riempirsi del tutto a seconda della velocità di lettura del client. In caso il fd non sia
pronto si salva lo stato corrente della richiesta nella struttura cmd_save_info e si
riprende una volta pronto il fd. Inoltre il server invierà punteggi dei giocatori che avranno
timestamp di completamento minore del timestamp della richiesta showscore.

<h3> Formato dei Messaggi </h3>
I messaggi scambiati tra client e server sono tutti effettuati tramite text protocol poichè
maggior parte dei dati da scambiare sono caratteri (domande e risposte, username e
punteggi...) rendendo inefficiente l’utilizzo di binary protocol. Ogni messaggio è prima
preceduto dalla dimensione in byte, inviata separatamente.

Il server assegna ad ogni client uno status nella struttura user_info per determinare
come interpretare ogni messaggio ricevuto.

Gli status sono: UNNAMED, assegnato agli utenti appena registrati che non hanno
ancora scelto l’username; INQUIZ per specificare che l’utente deve ancora scegliere un
tema e che può inserire comandi; INTHEME quando invece ha già scelto un tema e sta
rispondendo alle domande.

<h3> Realizzazione Client </h3>
Il client è stato realizzato con socket bloccanti per consentire ad eventuali altri processi
di poter eseguire durante i periodi di attesa di risposta da parte del server.
L’utente dopo aver stabilito la connessione col server dovrà inserire il proprio username,
in caso già utilizzato per la sessione corrente da un altro utente allora il server invierà un
avviso al primo in modo tale da poterlo reinserire.
Stessa cosa avverrà in caso l’utente inserisca un comando non riconosciuto dal server.

<h3> Gestione Domande/Risposte e Errori </h3>
Domande e risposte sono contenute rispettivamente nei file files/questions.txt e
files/answers.txt.I contenuti di entrambi devono rispettare un certo formato, ed ogni
informazione deve essere separata dalla successiva con uno \n (a capo).

La prima cosa in questions.txt deve essere il numero dei temi disponibili, seguito dal
nome del primo tema, da un intero che rappresenta l’offset interno al file answers.txt
delle relative risposte, e infine dalle domande ognuna sempre su righe separate. 
L’offset delle risposte è utile per evitare di scorrere ogni volta tutto il file answers.txt.
Questo viene aperto solo quando vanno verificate le risposte inserite da un client,
mentre il file delle domande invece viene aperto una volta sola all’avvio del server per
stampare i nomi dei temi disponibili tramite la funzione di supporto print_theme_names(),
questa inoltre salva anche nell’array theme_data le informazioni nome_tema,
offset_domanda, offset_risposta per riutilizzarle successivamente senza dover poi
scorrere nuovamente alcun file.

Gli errori relativi all’apertura dei file utilizzati vengono rilevati con la funzione
file_error_testing() che spesso segue una getline() e verifica se il valore di ritorno è
negativo o meno.

Eventuali errori di socket sono rilevati con check_socket_error() eseguita dopo ogni
send() e recv() prendendo come argomento il valore di ritorno di queste.
In caso di errore di qualsiasi tipo verranno appositamente deallocate tutte le strutture in
memoria dinamica con free() per evitare memory leak.

In caso di disconnessione client o server viene settata la variabile errno ad EPIPE
oppure ECONNRESET, questa viene verificata dalle funzioni check_client_error() e
check_server_disconnection(), però di default il segnale EPIPE è associato ad un
handler che termina immediatamente il processo e quindi impedirebbe ulteriori azioni
come la rimozione del client dalla relativa lista nel server, o semplicemente mostrare un
messaggio di disconnessione server nel client. Per risolvere il problema si è deciso di
ignorare completamente ogni segnale EPIPE sia nel client che nel server con la funzione
signal(EPIPE, SIGIGN).

<h3> Strutture Dati Utilizzate e Funzioni di Supporto </h3>
Nel file data/server_data.h vi sono le strutture dati principali utilizzate esclusivamente dal
server, tra cui: theme_data_t, clients_info_t, leaderboards_t, command_request_t,
cmd_save_info_t.
In particolare clients_info_t contiene numero dei client connessi, e lista di client completa
di username, status, socket_fd.
leaderboards_t sarà una lista di punteggi relativi ad un solo tema: username, punteggio,
timestamp completamento.

command_request_t è la lista dei comandi in attesa di essere eseguiti, con relativo
comando, username richiedente, fd richiedente e timestamp.
Inoltre questo file contiene anche varie funzioni di gestione delle strutture dati già
menzionate, tra cui: register_client(), find_user(fd), update_leaderboard(),
remove_from_leaderboard(), remove_player(fd), update_cmd_queue(),
init_cmd_save_info(). Contiene inoltre tutte le funzioni di controllo degli errori.

Il file data/server_aux_funcs.h invece contiene funzioni di supporto al server, ma non di
manipolazione dati, come per esempio: print_theme_names(), print_leaderboards(),
recv_msg_dim() molto utile per riutilizzare il codice che riceve la dimensione del
messaggio che il client sta per inviare, send_themes() per inviare al client i nomi dei
temi, send_question(), showscore_poll() utile per fare polling in scrittura sull’fd del client
per verificare se scrivibile durante una showscore.
Infine il file data/common_data.h contiene check_client_error() e
check_server_disconnection().
