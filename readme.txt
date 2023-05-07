Stefan Darius, 324CD

Tema 2 PCOM

DESCRIEREA IMPLEMENTARII:

Am ales sa implementez atat serverul, cat si clientul, ca 2 masini pe stari. Pentru aceasta am definit 
in cele doua fisiere .cpp (server.cpp si client.cpp) 2 enumuri cu constante, primele fiind cele care identifica 
starile masinilor, iar ultimele fiind cele care dau numarul de stari (NUM_STATES).
Serverul este o "masina", iar clientul alta, fiecare avand starile ei.

De exemplu, pentru clientul TCP avem:

typedef enum {
	STATE_CONNECT,
    STATE_POLL,
    STATE_RECEIVED_FROM_SERVER,
    STATE_CHECK_STDIN,
    STATE_EXIT,
	NUM_STATES
} state_t;

Pentru server avem:

typedef enum {
	STATE_POLL,
    STATE_CHECK_EXIT,
    STATE_RECEIVED_UDP,
    STATE_NEW_CONNECTION,
    STATE_RECEIVED_TCP,
    STATE_CLOSE_CONNECTION,
    STATE_SUBSCRIBE,
    STATE_UNSUBSCRIBE,
    STATE_SEND_STORED,
    STATE_EXIT,
	NUM_STATES
} state_t;

In aceste fisiere am definit urmatorul tip de functie, care modeleaza o stare a unei masini: 

state_t state_func_t(instance_data_t data)

Aceste functii primesc ca parametru un pointer catre o structura de date definita in acelasi 
fisier, care are rolul de a tansmite datele importante legate de parametrii masinii de la o stare 
la alta. Functiile "state_func_t" returneaza o valoare de tipul "state_t" care va identifica 
urmatoarea stare in care trebuie sa treaca masina dupa starea curenta.

In sever.cpp avem:

typedef struct {
	int udp_sockfd;  // socketul UDP unde primeste serverul mesajele

    int listen_tcp_sockfd;  // socketul TCP unde asteapta serverul conexiuni

    int recv_tcp_sockfd;  // socketul TCP pe care s-a primit comanda curenta sau conexiunea curenta

    int no_fds;  // numarul de file descriptori din poll

    int poll_size;  // numarul de file descriptori care incap momentan in poll

    struct sockaddr_in serv_addr;  // ip-ul si portul serverului

    uint16_t port;  // portul serverului

    struct pollfd *poll_fds;  // vectorul de file descriptori pentru multiplexare

    char stdinbuf[MAX_CLIENT_COMMAND_SIZE];  // un buffer pentru comenzile de la tastatura

    uint8_t exit_flag;  // flag-ul care marcheaza ca serverul trebuie sa se inchida

    char buffer[MAX_CLIENT_COMMAND_SIZE];  // un buffer pentru comenzile un/subscribe primite de la clientii TCP

    char id_client[MAX_ID_SIZE];  // buffer unde se pastreaza id-ul clientului curent

    // un dictionar in care se mapeaza id-ul unui client la o structura unde se pastreaza datele lui
    unordered_map<string, Tclient> clients;

    // un dictionar in care se mapeaza socketul pe care s-a conectat un client la id-ul lui
    // ajuta pentru comenzile de subscribe si unsubscribe
    unordered_map<int, string> socket_client_map;

    // un dictionar in care se mapeaza mesajele bufferate la numarul de clienti spre care trebuie trimis
    unordered_map<Tmessage, int> buffered_messages;
} instance_data, *instance_data_t;

In client.cpp avem:

typedef struct {
    int sockfd;  // socketul cu care se comunica cu serverul
    int no_fds;  // numarul de file descriptori din poll
    struct sockaddr_in serv_addr;  // ip-ul si portul serverului
    uint16_t server_port;  // portul serverului
    char ip_server[20];  // ip-ul serverului sub format a.b.c.d
    struct pollfd poll_fds[CLIENT_POLLFDS];  // vectorul de file descriptori pentru poll
    char id_client[MAX_ID_SIZE];  // id-ul clientului
    char stdinbuf[MAX_CLIENT_COMMAND_SIZE];  // bufferul pentru intrarea de la tastatura
    uint8_t exit_flag;  // flagul care marcheaza inchiderea clientului
} instance_data, *instance_data_t;

Am declarat apoi starile serverului, respectiv ale clientului, si functia:

state_t run_state(state_t cur_state, instance_data_t data)

Aceasta va apela functia starii curente, identificata de "cur_state", cu parametrul "data". Pentru 
a realiza corespondenta dintre valorile "state_t" si aceste stari, am declarat in ambele fisiere 
vectorii "state_table", care contin pointerii catre functiile care reprezinta starile, in ordinea corecta.

De exemplu, in server.cpp avem:

state_func_t* const state_table[NUM_STATES] = {
	do_poll,
    do_check_exit,
    do_received_udp,
    do_new_connection,
    do_received_tcp,
    do_close_connection,
    do_subscribe,
    do_unsubscribe,
    do_send_stored,
    do_exit
};

Functia "run_state" apeleaza functia care se gaseste in "state_table" la pozitia "cur_state" si 
intoarce rezultatul acesteia. Practic, in server.cpp si client.cpp se incepe cu initializarea 
structurilor interne si se fac operatiile pentru comunicarea prin socketi, apoi intr-o bucla 
se apeleaza functia "run_state" si actualizez starea curenta, pana se seteaza flag-ul de exit:

while (!data.exit_flag) {
    cur_state = run_state(cur_state, &data);
}

Am ales sa folosesc unordered_map pentru ca permite cautarea foarte rapida.

Structura de client TCP:

typedef struct {
    // flag care marcheaza daca clientul este conectat, ajuta in situatia in care se incearca reconectare
    uint8_t connected;

    // socketul prin care comunica serverul cu acest client (se poate modifica daca se deconecteaza)
    int socket;

    // adresa ip si portul clientului TCP
    struct sockaddr_in addr;

    // un dictionar in care se stocheaza topicurile la care este abonat clientul si in care 
    // acestea sunt mapate la o valoare booleana care indica daca au optiunea de store and forward sau nu
    unordered_map<string, bool> *topic_sf_map;

    // un vector cu pointeri catre mesajele care au fost stocate pentru client cat timp a fost deconectat
    vector<Tmessage> *stored_messages;
} client, *Tclient;

Structura in care se primeste un mesaj UDP:

typedef struct __attribute__ ((packed)) {
    char topic[MAX_TOPIC_SIZE];
    uint8_t data_type;
    char payload[MAX_PAYLOAD_SIZE];
} udp_message;

Structura care se foloseste pentru a trimite mesajele catre clientii TCP:

typedef struct __attribute__ ((packed)) {
    struct sockaddr_in udp_client_addr;  // adresa ip si portul clientului UDP care a scris mesajul
    char topic[MAX_TOPIC_SIZE];
    uint8_t data_type;
    char payload[MAX_PAYLOAD_SIZE];
} message, *Tmessage;

La primirea unui mesaj UDP serverul aloca memorie pe heap pentru un Tmessage 
si copiaza ce a primit de clientul UDP, adaugand si adresa ip si portul acestuia.
Se trece apoi prin toti clientii pe care ii are serverul inregistrati si se verifica
daca topicul mesajului se gaseste in dictionarul "topic_sf_map" asociat clientului.
Daca da, se verifica daca clientul este conectat si i se trimite mesajul pe socketul 
TCP corespunzator, altfel se adauga pointer catre acest mesaj in vectorul "stored_messages", 
si in dictionarul "buffered_messages" al serverului. Daca mesajul nu trebuie trimis sau 
stocat pentru niciun client se sterge structura creata.

La o eventuala reconectare a unui client se trece prin vectorul "stored_messages" asociat lui 
si se trimite fiecare mesaj de aici, decrementandu-se valoarea din "buffered_messages" pentru 
mesajul respectiv. Se verifica daca aceasta valoare devine 0, daca da se sterge structura.

Am ales sa fac interpretarea payloadului unui mesaj la nivelul clientului TCP pentru a separa 
aceasta functionalitate de server care gestioneaza deja conexiunile si dirijarea mesajelor.
Pentru multiplexarea conexiunilor TCP si UDP si inputului de la tastatura am folosit poll, ca in
laboratorul 7.
