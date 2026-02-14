/*
 * Serwer gry w Statki
 *
 * Najważniejsze założenia protokołu i serwera:
 * - Komunikacja TCP w formacie TLV.
 * - Jedna gra = dokładnie 2 graczy = jeden wątek zarządzający tą parą.
 * - Tury są naprzemienne (gracz A, potem B, itd.).
 * - Każda tura ma własny unikalny TOKEN (turn_id), sprawdzany przy każdym ruchu,
 *   żeby zapobiec duplikatom i starym pakietom.
 * - Duplikat strzału w to samo pole -> MSG_ERROR i gracz strzela ponownie
 *   bez utraty tury.
 *
 * Kompilacja:  gcc -o server serwer-projekt_v3.c -lpthread
 * Uruchomienie: ./server [port] [-d]
 *   -d  uruchamia jako demona 
 */

#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <syslog.h>
#include <signal.h>
#include <errno.h>
#include <time.h>

#define DEFAULT_PORT 8888
#define BOARD_SIZE   10
#define MAX_SHIPS    10
#define BUFFER_SIZE  1024

/* Typy komunikatów TLV */
#define MSG_CONNECT      0x01  /* Krótka wiadomość powitalna od serwera */
#define MSG_SHIPS_CONFIG 0x02  /* [lista statków] 10 struktur po 16 bajtów każda (x,y,len,horiz) */
#define MSG_MOVE         0x03  /* uint32_t turn_id, uint8_t x, uint8_t y */
#define MSG_MOVE_RESULT  0x04  /* uint8_t x, uint8_t y, uint8_t result (MISS/HIT/SUNK) */
#define MSG_GAME_START   0x05  /* uint8_t starting_player (0/1) */
#define MSG_GAME_END     0x06  /* uint8_t winner (0/1) */
#define MSG_WAIT         0x07  /* Pusta wiadomość: "czekaj na swoją turę" */
#define MSG_YOUR_TURN    0x08  /* uint32_t turn_id: token do walidacji ruchu */
#define MSG_PLAYER_ID    0x09  /* uint8_t id (0/1) – kim jesteś w parze */
#define MSG_ERROR        0xFF  /* Komunikat błędu */

/* Wynik strzału – używane w MSG_MOVE_RESULT */
#define RESULT_MISS 0
#define RESULT_HIT  1
#define RESULT_SUNK 2

/* TLVMessage – bufor odebranej/wysyłanej ramki TLV */
typedef struct {
    uint8_t  type;               /* typ wiadomości (T) */
    uint16_t length;             /* długość danych (L) */
    uint8_t  value[BUFFER_SIZE]; /* dane (V) */
} TLVMessage;

/* Pojedynczy statek gracza */
typedef struct { int x,y,length,horizontal; } Ship;

/* Plansza i flota jednego gracza */
typedef struct {
    int  board[BOARD_SIZE][BOARD_SIZE]; /* 0=pusto, 1=statek, 2=trafione, 3=pudło */
    Ship ships[MAX_SHIPS];
    int  ships_count;                   /* ile statków na starcie (powinno być 10) */
    int  ships_remaining;               /* ile statków pozostało do zatopienia */
    int  ship_sunk[MAX_SHIPS];          /* znacznik "czy ten statek już policzony jako zatopiony" */
} PlayerBoard;

/* Struktura reprezentująca połączenie i stan gracza */
typedef struct {
    int         socket;     /* deskryptor gniazda TCP */
    PlayerBoard board;      /* własna plansza z ułożonymi statkami */
    int         player_id;  /* 0/1 w ramach pary */
    int         ready;      /* 1 gdy przesłał poprawną konfigurację statków */
} Player;

/* Kontekst jednej gry (obsługiwany przez jeden wątek) */
typedef struct {
    Player  players[2];
    int     current_player; /* 0/1 – kto ma turę */
    int     game_active;    /* flaga trwania gry */
    uint32_t turn_id;       /* rosnący token tury – waliduje poprawność ruchu */
} Game;

/* --- Zmienne globalne do obsługi sygnałów */
static volatile sig_atomic_t server_running = 1; /* 0 -> pętla główna zakończy pracę */
static int g_server_sock = -1;                   /* do przerwania accept() po SIGINT/SIGTERM */

/* Wysyłanie/odbieranie dokładnie N bajtów (albo błąd) */
static int send_all(int s, const void *buf, size_t len) {
    const uint8_t *p=(const uint8_t*)buf; 
    size_t off=0;
    while(off<len) { 
        ssize_t n=send(s,p+off,len-off,0); 
        if(n<=0) 
            return -1; 
        off+=(size_t)n; 
    }
    return 0;
}
static int recv_all(int s, void *buf, size_t len) {
    uint8_t *p=(uint8_t*)buf; 
    size_t off=0;
    while(off<len) { 
        ssize_t n=recv(s,p+off,len-off,0); 
        if(n<=0) 
            return -1;
        off+=(size_t)n; 
        }
    return 0;
}

/* Operacje TLV: tworzenie, wysyłka i odbiór pojedynczej ramki */
static void create_tlv_message(TLVMessage *m, uint8_t t, const void *d, uint16_t L) {
    m->type=t; 
    m->length=L; 
    if(d&&L) 
        memcpy(m->value,d,L);
}
static int send_tlv_message(int s, const TLVMessage *m) {
    /* Nagłówek TLV: 1B type + 2B length (network-endian) */
    uint8_t hdr[3]; 
    hdr[0]=m->type; 
    uint16_t nl=htons(m->length);
    memcpy(&hdr[1], &nl, 2);
    if (send_all(s, hdr, 3) < 0) 
        return -1;
    /* Dane (Value) */
    if (m->length) 
        return send_all(s, m->value, m->length);
    return 0;
}
static int receive_tlv_message(int s, TLVMessage *m) {
    /* Odbiór nagłówka */
    uint8_t hdr[3]; 
    if (recv_all(s, hdr, 3) < 0) 
        return -1;
    m->type=hdr[0]; 
    uint16_t nl; 
    memcpy(&nl,&hdr[1],2); 
    m->length=ntohs(nl);
    if (m->length>BUFFER_SIZE) return -1; /* za duży payload – błąd */
    /* Odbiór payloadu jeśli jest */
    if (m->length && recv_all(s,m->value,m->length)<0) return -1;
    return (int)(3+m->length);
}

/* Opróżnij bufor gniazda z wszystkiego co klient wysłał "na zapas" */
static void drain_socket(int s) {
    uint8_t tmp[1024];
    for(;;){
        ssize_t n=recv(s,tmp,sizeof(tmp),MSG_DONTWAIT);
        if (n<=0) break;  /* nic więcej nie czeka lub błąd EWOULDBLOCK/EAGAIN */
    }
}

/* --- Walidacja i ustawianie statków na planszy --- */

/* Zasada: statki nie mogą się stykać ani krawędziami, ani rogami.
   Funkcja sprawdza, czy dowolne segmenty statków 'a' i 'b' są w odległości <=1. */
static int ships_touch(const Ship *a, const Ship *b) {
    for(int i=0;i<a->length;++i) {
        int x1=a->horizontal?a->x+i:a->x, 
            y1=a->horizontal?a->y:a->y+i;
        for(int j=0;j<b->length;++j){
            int x2=b->horizontal?b->x+j:b->x, 
                y2=b->horizontal?b->y:b->y+j;
            if (abs(x1-x2)<=1 && abs(y1-y2)<=1) 
                return 1; /* stykają się – niedozwolone */
        }
    }
    return 0;
}

/* Sprawdzenie reguł floty:
   - Dokładnie 10 statków.
   - Dopuszczalne długości 1..4 i proporcje: 4x1, 3x2, 2x3, 1x4.
   - Każdy w granicach planszy.
   - Żadne dwa nie stykają się (ani rogiem, ani bokiem). */
static int validate_ships(const Ship *ships, int count){
    if (count!=10) 
        return 0;
    int types[5]={0}; /* indeks = długość (1..4) */
    for(int i=0;i<count;++i) {
        if (ships[i].length<1 || ships[i].length>4) 
            return 0;
        types[ships[i].length]++;
        /* Walidacja współrzędnych względem orientacji */
        if (ships[i].horizontal) {
            if (ships[i].x<0 || ships[i].x+ships[i].length>BOARD_SIZE) 
                return 0;
            if (ships[i].y<0 || ships[i].y>=BOARD_SIZE) 
                return 0;
        } else {
            if (ships[i].y<0 || ships[i].y+ships[i].length>BOARD_SIZE) 
                return 0;
            if (ships[i].x<0 || ships[i].x>=BOARD_SIZE) 
                return 0;
        }
    }
    /* Dokładny zestaw statków: 4 jedno-, 3 dwu-, 2 trzy-, 1 czteromasztowiec */
    if (!(types[1]==4 && types[2]==3 && types[3]==2 && types[4]==1)) 
        return 0;
    /* Sprawdzenie stykania się statków parami */
    for (int i=0;i<count;++i) 
        for (int j=i+1;j<count;++j) 
            if (ships_touch(&ships[i],&ships[j])) 
                return 0;
    return 1;
}

/* Inicjalizacja planszy gracza na podstawie poprawnej konfiguracji statków */
static void setup_player_board(PlayerBoard *pb, const Ship *ships, int count) {
    memset(pb->board,0,sizeof(pb->board));
    pb->ships_count=count; 
    pb->ships_remaining=count;
    for(int i=0;i<count;++i) {
        pb->ships[i]=ships[i];
        pb->ship_sunk[i]=0; /* na starcie żaden nie jest policzony jako zatopiony */
    }
    /* Oznacz pola zajęte przez statki jako 1 */
    for(int i=0;i<count;++i)
        for(int j=0;j<ships[i].length;++j) {
            int x=ships[i].horizontal?ships[i].x+j:ships[i].x;
            int y=ships[i].horizontal?ships[i].y:ships[i].y+j;
            pb->board[y][x]=1;
        }
}

/* Zwróć indeks statku, którego segment leży na (x,y), lub -1 gdy woda */
static int find_ship_at(const PlayerBoard *pb, int x, int y){
    for(int i=0;i<pb->ships_count;++i) {
        const Ship *s=&pb->ships[i];
        for(int j=0;j<s->length;++j) {
            int sx=s->horizontal? s->x + j : s->x;
            int sy=s->horizontal? s->y     : s->y + j;
            if (sx==x && sy==y) 
                return i;
        }
    }
    return -1;
}

/* Sprawdź, czy dany statek (po indeksie) jest już całkowicie trafiony.
   Jeśli którykolwiek segment ma jeszcze '1' (nietrafiony), to nie jest zatopiony. */
static int is_ship_sunk(const PlayerBoard *pb, int idx){
    const Ship *s=&pb->ships[idx];
    for(int j=0;j<s->length;++j) {
        int sx=s->horizontal? s->x + j : s->x;
        int sy=s->horizontal? s->y     : s->y + j;
        if (pb->board[sy][sx]==1) 
            return 0; /* znaleziona nieuszkodzona część – jeszcze pływa */
    }
    return 1;
}

/* Przetworzenie pojedynczego strzału na planszy w punkt (x,y).
   Zwraca:
     -1 -> poza planszą,
     -2 -> strzał w pole już wcześniej ostrzelane,
      0 -> pudło (MISS),
      1 -> trafienie (HIT),
      2 -> zatopienie (SUNK).
*/
static int process_move(PlayerBoard *pb, int x, int y){
    if (x<0||x>=BOARD_SIZE||y<0||y>=BOARD_SIZE) return -1;
    if (pb->board[y][x]==2 || pb->board[y][x]==3) return -2; /* duplikat strzału */

    if (pb->board[y][x]==1){
        pb->board[y][x]=2; /* oznacz trafienie */
        int idx = find_ship_at(pb, x, y);
        if (idx >= 0) {
            if (is_ship_sunk(pb, idx)) {
                if (!pb->ship_sunk[idx]) {      /* licz tylko raz ten konkretny statek */
                    pb->ship_sunk[idx]=1;
                    pb->ships_remaining--;
                }
                return RESULT_SUNK;
            } else {
                return RESULT_HIT;
            }
        } else {
            /* Sytuacja graniczna: 1 bez przypisanego statku – traktujemy jak HIT */
            return RESULT_HIT;
        }
    } else {
        pb->board[y][x]=3; /* pudło */
        return RESULT_MISS;
    }
}

/* --- Uruchamianie jako demon i obsługa sygnałów zakończenia --- */
static void daemonize(void) {
    pid_t pid=fork(); 
    if(pid<0)exit(EXIT_FAILURE); 
    if(pid>0)exit(EXIT_SUCCESS);
    if (setsid()<0) exit(EXIT_FAILURE);
    signal(SIGCHLD,SIG_IGN); 
    signal(SIGHUP,SIG_IGN);
    pid=fork(); 
    if(pid<0)exit(EXIT_FAILURE); 
    if(pid>0)exit(EXIT_SUCCESS);
    umask(0); 
    chdir("/"); 
    fclose(stdin); 
    fclose(stdout); 
    fclose(stderr);
}

/* Handler SIGINT/SIGTERM: zatrzymuje pętlę, przerywa accept() i loguje. */
static void signal_handler(int sig) {
    (void)sig;
    server_running=0;
    if (g_server_sock>=0) 
        close(g_server_sock); /* przerwij blokujący accept() w main */
    syslog(LOG_INFO,"Signal received, shutting down server");
}

/* --- Wysyłanie wiadomości powitalnej i ID gracza po nawiązaniu połączenia --- */
static void send_connect_and_id(Player *p, int idx) {
    char welcome[64];
    snprintf(welcome,sizeof(welcome),"Polaczono jako gracz %d", idx+1);
    TLVMessage cm; 
    create_tlv_message(&cm, MSG_CONNECT, welcome, (uint16_t)strlen(welcome));
    send_tlv_message(p->socket,&cm);
    uint8_t pid=(uint8_t)idx;
    TLVMessage idm; 
    create_tlv_message(&idm, MSG_PLAYER_ID, &pid, 1);
    send_tlv_message(p->socket,&idm);
}

/* Odbiór konfiguracji statków od jednego gracza
*  Po walidacji ustawiamy planszę i odsyłamy MSG_WAIT.
*/
static int recv_ships_config(int sock, Player *pl){
    TLVMessage msg;
    if (receive_tlv_message(sock,&msg)<=0) return -1;
    if (msg.type!=MSG_SHIPS_CONFIG || (msg.length%16)!=0) {
        TLVMessage em; 
        create_tlv_message(&em, MSG_ERROR, "Invalid SHIPS_CONFIG", 21);
        send_tlv_message(sock,&em);
        return -1;
    }
    int count=msg.length/16; 
    Ship ships[MAX_SHIPS];
    for (int i=0;i<count && i<MAX_SHIPS;++i) {
        int off=i*16; int32_t xn,yn,ln,hn;
        memcpy(&xn,&msg.value[off],4); 
        memcpy(&yn,&msg.value[off+4],4);
        memcpy(&ln,&msg.value[off+8],4);
        memcpy(&hn,&msg.value[off+12],4);
        ships[i].x=(int)ntohl(xn); 
        ships[i].y=(int)ntohl(yn);
        ships[i].length=(int)ntohl(ln); 
        ships[i].horizontal=(int)ntohl(hn);
    }
    if (!validate_ships(ships,count)) {
        TLVMessage em; 
        create_tlv_message(&em, MSG_ERROR, "Invalid ships configuration", 26);
        send_tlv_message(sock,&em);
        return -1;
    }
    setup_player_board(&pl->board, ships, count);
    pl->ready=1;

    /* Gracz poczeka na drugiego – sygnał MSG_WAIT */
    TLVMessage wm; 
    create_tlv_message(&wm, MSG_WAIT, NULL, 0); 
    send_tlv_message(sock,&wm);
    return 0;
}

/* Wątek gry: obsługuje dwóch graczy na dwóch gniazdach */
typedef struct { int s0, s1; } PairArgs;

static void* game_thread(void *arg){
    PairArgs *pa=(PairArgs*)arg;
    Game game; 
    memset(&game,0,sizeof(game));
    game.players[0].socket=pa->s0; game.players[0].player_id=0;
    game.players[1].socket=pa->s1; game.players[1].player_id=1;
    free(pa);

    syslog(LOG_INFO,"Game thread started: s0=%d s1=%d", game.players[0].socket, game.players[1].socket);

    /* Powitania + identyfikatory graczy */
    send_connect_and_id(&game.players[0], 0);
    send_connect_and_id(&game.players[1], 1);

    /* Odbiór i walidacja statków obu graczy (po kolei) */
    if (recv_ships_config(game.players[0].socket, &game.players[0])<0) goto end;
    if (recv_ships_config(game.players[1].socket, &game.players[1])<0) goto end;

    /* Start właściwej gry: losuj kto zaczyna i losuj początkowy token tury */
    game.game_active    = 1;
    game.current_player = rand() % 2;
    game.turn_id        = (uint32_t)(rand() & 0x7fffffff) + 1; /* dodatni początek */

    /* Wyślij start + przygotuj bufory i token pierwszej tury */
    {
        uint8_t starting=(uint8_t)game.current_player;
        TLVMessage sm; 
        create_tlv_message(&sm, MSG_GAME_START, &starting, 1);
        send_tlv_message(game.players[0].socket,&sm);
        send_tlv_message(game.players[1].socket,&sm);

        /* Prewencyjnie oczyść bufor obu graczy */
        drain_socket(game.players[0].socket);
        drain_socket(game.players[1].socket);

        /* Wyślij jednemu MSG_YOUR_TURN z tokenem, drugiemu MSG_WAIT */
        uint32_t t_be=htonl(game.turn_id);
        TLVMessage yt; 
        create_tlv_message(&yt, MSG_YOUR_TURN, &t_be, 4);
        TLVMessage wt; 
        create_tlv_message(&wt, MSG_WAIT, NULL, 0);
        send_tlv_message(game.players[ game.current_player ].socket,&yt);
        send_tlv_message(game.players[ 1 - game.current_player ].socket,&wt);

        syslog(LOG_INFO,"Game started, player %d begins, turn_id=%u", game.current_player, game.turn_id);
    }

    /* Główna pętla tur: czyta WYŁĄCZNIE z gniazda aktywnego gracza */
    while (server_running && game.game_active){
        int cp = game.current_player;            
        int ap = 1 - cp;                         
        int cp_sock = game.players[cp].socket;
        int ap_sock = game.players[ap].socket;

        TLVMessage msg;
        if (receive_tlv_message(cp_sock, &msg) <= 0) {
            /* Rozłączenie aktywnego gracza podczas swojej tury kończy grę */
            syslog(LOG_INFO,"Player %d disconnected during turn", cp);
            break;
        }

        /* Oczekiwany pakiet: MOVE z 6B danych (turn_id,x,y) */
        if (msg.type != MSG_MOVE || msg.length != 6) {
            TLVMessage em; 
            create_tlv_message(&em, MSG_ERROR, "Expected MOVE(turn_id,x,y)", 25);
            send_tlv_message(cp_sock,&em);
            drain_socket(cp_sock);
            continue; /* tura trwa, nie zmieniamy tokenu */
        }

        /* Weryfikacja TOKENU TURY */
        uint32_t tok_be; 
        memcpy(&tok_be, &msg.value[0], 4);
        uint32_t tok = ntohl(tok_be);
        uint8_t x = msg.value[4], y = msg.value[5];

        if (tok != game.turn_id) {
            TLVMessage em; 
            create_tlv_message(&em, MSG_ERROR, "Invalid or stale turn token", 26);
            send_tlv_message(cp_sock,&em);
            drain_socket(cp_sock);
            continue; /* nadal ta sama tura */
        }

        /* Poprawny token – wykonaj ruch na planszy przeciwnika */
        int result = process_move(&game.players[ap].board, x, y);
        if (result == -1) {
            TLVMessage em; 
            create_tlv_message(&em, MSG_ERROR, "Invalid move (coords/out of board)", 35);
            send_tlv_message(cp_sock,&em);
            drain_socket(cp_sock); 
            continue;              /* bez zmiany tokenu/tury */
        }
        if (result == -2) {
            TLVMessage em; 
            create_tlv_message(&em, MSG_ERROR, "Already shot here. Try again.", 30);
            send_tlv_message(cp_sock,&em);
            drain_socket(cp_sock); /* powtórzony strzał w to samo pole */
            continue;              /* bez zmiany tokenu/tury */
        }

        syslog(LOG_INFO,"Turn %u: P%d shot (%d,%d): %s",
               game.turn_id, cp, x, y,
               (result==RESULT_MISS?"MISS":(result==RESULT_HIT?"HIT":"SUNK")));

        /* Wyślij wynik strzału obu graczom (symetryczna informacja) */
        uint8_t res[3]={(uint8_t)x,(uint8_t)y,(uint8_t)result};
        TLVMessage rm; create_tlv_message(&rm, MSG_MOVE_RESULT, res, 3);
        send_tlv_message(cp_sock,&rm);
        send_tlv_message(ap_sock,&rm);

        /* Po przyjęciu poprawnego ruchu – wyczyść "sklejone" pakiety aktywnego gracza */
        drain_socket(cp_sock);

        /* Sprawdź warunek końca: wszystkie statki przeciwnika zatopione */
        if (game.players[ap].board.ships_remaining == 0) {
            game.game_active = 0;
            uint8_t winner=(uint8_t)cp;
            TLVMessage endm; 
            create_tlv_message(&endm, MSG_GAME_END, &winner, 1);
            send_tlv_message(game.players[0].socket,&endm);
            send_tlv_message(game.players[1].socket,&endm);
            syslog(LOG_INFO,"Game ended, winner=%d", cp);
            break;
        }

        /* Zmiana tury ZAWSZE po poprawnym ruchu + nowy TOKEN */
        game.current_player = ap;
        game.turn_id++;                   /* nowy unikalny token */
        uint32_t t_be = htonl(game.turn_id);

        /* Dla pewności: wyczyść bufory obu graczy */
        drain_socket(game.players[0].socket);
        drain_socket(game.players[1].socket);

        /* Rozesłanie: nowa tura + token dla gracza aktywnego, WAIT dla drugiego */
        TLVMessage yt; create_tlv_message(&yt, MSG_YOUR_TURN, &t_be, 4);
        TLVMessage wt; create_tlv_message(&wt, MSG_WAIT, NULL, 0);
        send_tlv_message(game.players[ game.current_player ].socket,&yt);
        send_tlv_message(game.players[ 1 - game.current_player ].socket,&wt);
    }

end:
    /* Porządki: zamykanie gniazd graczy, log o końcu wątku */
    if (game.players[0].socket>=0) close(game.players[0].socket);
    if (game.players[1].socket>=0) close(game.players[1].socket);
    syslog(LOG_INFO,"Game thread finished");
    return NULL;
}

/* Funkcja main: nasłuchiwanie, parowanie graczy i uruchamianie wątków gier */
int main(int argc, char *argv[]){
    int port=DEFAULT_PORT; 
    int as_daemon=0;
    if (argc>=2) 
        port=atoi(argv[1]);
    if (argc>=3 && strcmp(argv[2],"-d")==0) 
        as_daemon=1;

    openlog("battleship_server", LOG_PID|LOG_CONS, LOG_DAEMON);
    if (as_daemon) daemonize();

    srand((unsigned)time(NULL));
    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);

    /* Tworzenie gniazda nasłuchującego TCP */
    int s=socket(AF_INET,SOCK_STREAM,0);
    if (s<0) { 
        syslog(LOG_ERR,"socket: %s", strerror(errno)); 
        return 1; 
    }
    g_server_sock=s;

    /* Pozwól ponownie użyć portu po szybkim restarcie */
    int opt=1; 
    setsockopt(s,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));

    /* BIND na dowolny adres lokalny, wybrany port */
    struct sockaddr_in sa; 
    memset(&sa,0,sizeof(sa));
    sa.sin_family=AF_INET; 
    sa.sin_addr.s_addr=INADDR_ANY; 
    sa.sin_port=htons(port);
    if (bind(s,(struct sockaddr*)&sa,sizeof(sa))<0) { 
        syslog(LOG_ERR,"bind: %s", strerror(errno)); 
        close(s); 
        return 1; 
    }
    if (listen(s,16)<0) { 
        syslog(LOG_ERR,"listen: %s", strerror(errno)); 
        close(s); 
        return 1; 
    }

    syslog(LOG_INFO,"Server started on port %d", port);
    printf("Server started on port %d\n", port);

    int waiting_socket=-1; /* gniazdo pierwszego oczekującego gracza (parujemy w dwójki) */

    /* Pętla przyjmowania połączeń i parowania graczy */
    while (server_running){
        struct sockaddr_in ca; 
        socklen_t clen=sizeof(ca);
        int cs=accept(s,(struct sockaddr*)&ca,&clen);
        if (cs<0){
            if (server_running) syslog(LOG_ERR,"accept: %s", strerror(errno));
            break;
        }
        syslog(LOG_INFO,"New connection from %s", inet_ntoa(ca.sin_addr));

        if (waiting_socket<0){
            /* Pierwszy gracz czeka na parę */
            waiting_socket=cs;
        } else {
            /* Mamy parę – start nowego wątku gry dla tych dwóch gniazd */
            typedef struct { int s0, s1; } PairArgsLocal;
            PairArgsLocal *pa=(PairArgsLocal*)malloc(sizeof(PairArgsLocal));
            pa->s0 = waiting_socket;
            pa->s1 = cs;

            pthread_t gt;
            pthread_create(&gt, NULL, game_thread, pa);
            pthread_detach(gt);

            waiting_socket=-1;
            syslog(LOG_INFO,"Game created (thread)");
        }
    }

    /* Porządki przy zamykaniu serwera */
    if (waiting_socket>=0) 
        close(waiting_socket);
    if (g_server_sock>=0) 
        close(g_server_sock);
    g_server_sock=-1;

    syslog(LOG_INFO,"Server stopped");
    closelog();
    return 0;
}
