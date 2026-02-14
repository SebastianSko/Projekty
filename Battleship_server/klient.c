/*
 * Klient gry w Statki
 * - Komunikacja z serwerem przez protokół TLV (Type-Length-Value)
 * - Strzał tylko po otrzymaniu MSG_YOUR_TURN z tokenem tury
 * 
 * Kompilacja: gcc -o client klient_projekt_v3.c
 * Uruchomienie: ./client <adres_serwera> [port]
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

// STAŁE KONFIGURACYJNE
#define DEFAULT_PORT 8888      // Domyślny port serwera
#define BOARD_SIZE   10        // Rozmiar planszy 10x10
#define MAX_SHIPS    10        // Maksymalna liczba statków (4+3+2+1)
#define BUFFER_SIZE  1024      // Rozmiar bufora do komunikacji

// TYPY KOMUNIKATÓW TLV (Type-Length-Value)
#define MSG_CONNECT      0x01  // Potwierdzenie połączenia z serwerem
#define MSG_SHIPS_CONFIG 0x02  // Wysłanie konfiguracji statków do serwera
#define MSG_MOVE         0x03  // Ruch gracza
#define MSG_MOVE_RESULT  0x04  // Wynik ruchu: x, y, result (pudło/trafiony/zatopiony)
#define MSG_GAME_START   0x05  // Start gry: informacja kto zaczyna
#define MSG_GAME_END     0x06  // Koniec gry: informacja kto wygrał
#define MSG_WAIT         0x07  // Oczekiwanie na drugiego gracza
#define MSG_YOUR_TURN    0x08  // Twoja kolej + token tury (turn_id)
#define MSG_PLAYER_ID    0x09  // Przypisane ID gracza (0 lub 1)
#define MSG_ERROR        0xFF  // Komunikat błędu od serwera

// WYNIKI STRZAŁU
#define RESULT_MISS 0  // Pudło (strzał w wodę)
#define RESULT_HIT  1  // Trafiony (statek trafiony, ale nie zatopiony)
#define RESULT_SUNK 2  // Trafiony, zatopiony

// Struktura wiadomości TLV: typ, długość, wartość
typedef struct { 
    uint8_t type;               // Typ wiadomości
    uint16_t length;            // Długość danych w bajtach
    uint8_t value[BUFFER_SIZE]; // Dane wiadomości
} TLVMessage;

// Struktura pojedynczego statku
typedef struct { 
    int x;          // Kolumna startowa (0-9)
    int y;          // Wiersz startowy (0-9)
    int length;     // Długość statku (1-4)
    int horizontal; // Orientacja: 1=poziomo, 0=pionowo
} Ship;

// Struktura plansz gry (własna i wroga)
typedef struct {
    char display[BOARD_SIZE][BOARD_SIZE]; // Nasza plansza (widoczne nasze statki)
    char enemy[BOARD_SIZE][BOARD_SIZE];   // Plansza wroga (widoczne nasze strzały)
} GameBoard;

static GameBoard board;                           // Plansze gry
static int my_turn = 0;                           // Flaga: czy teraz nasza tura
static int game_over = 0;                         // Flaga: czy gra się skończyła
static int my_player_id = -1;                     // Nasze ID gracza (0 lub 1, -1=nieznane)
static int last_shot_was_mine = 0;                // Flaga: czy ostatni strzał był nasz
static uint32_t current_turn_id = 0;              // Aktualny token tury od serwera
static uint8_t tried[BOARD_SIZE][BOARD_SIZE];     // Mapa pól gdzie już strzelaliśmy

/*
 * send_all - Wysyła wszystkie bajty przez socket
 * Działa w pętli dopóki nie wyśle wszystkich danych lub nie wystąpi błąd
 */
static int send_all(int s, const void *b, size_t l) { 
    const uint8_t *p = b;  // Wskaźnik na dane
    size_t o = 0;          
    
    while(o < l) { 
        ssize_t n = send(s, p+o, l-o, 0);  // Wyślij pozostałe bajty
        if(n <= 0)
            return -1;  // Błąd lub rozłączenie
        o += (size_t)n; 
    } 
    return 0;  // Sukces - wszystko wysłano
}

/*
 * recv_all - Odbiera dokładnie l bajtów przez socket
 * Działa w pętli dopóki nie odbierze wszystkich danych lub nie wystąpi błąd
 */
static int recv_all(int s, void *b, size_t l) { 
    uint8_t *p = b;  // Wskaźnik na bufor
    size_t o = 0;    
    
    while(o < l) { 
        ssize_t n = recv(s, p+o, l-o, 0);  // Odbierz pozostałe bajty
        if(n <= 0)
            return -1;  // Błąd lub rozłączenie
        o += (size_t)n; 
    } 
    return 0;  // Sukces - wszystko odebrano
}

/*
 * create_tlv_message - Tworzy wiadomość TLV
 * Ustawia typ, długość i kopiuje dane do struktury
 */
static void create_tlv_message(TLVMessage *m, uint8_t t, const void *d, uint16_t L) { 
    m->type = t;      // Ustaw typ wiadomości
    m->length = L;    // Ustaw długość danych
    
    // Skopiuj dane jeśli są niepuste
    if(d && L)
        memcpy(m->value, d, L);
}

/*
 * send_tlv_message - Wysyła wiadomość TLV przez socket 
 */
static int send_tlv_message(int s, const TLVMessage *m) { 
    uint8_t h[3];  // Nagłówek
    
    h[0] = m->type;                    // Bajt 0: typ wiadomości
    uint16_t nl = htons(m->length);    // Konwersja długości do network byte order (big-endian)
    memcpy(&h[1], &nl, 2);             // Bajty 1-2: długość
    
    // Wyślij nagłówek (3 bajty)
    if(send_all(s, h, 3) < 0)
        return -1;
    
    // Wyślij dane (jeśli są)
    if(m->length)
        return send_all(s, m->value, m->length);
    
    return 0;
}

/*
 * receive_tlv_message - Odbiera wiadomość TLV przez socket
 * Najpierw odbiera nagłówek, potem dane
 */
static int receive_tlv_message(int s, TLVMessage *m) { 
    uint8_t h[3];  // Bufor na nagłówek
    
    // Odbierz nagłówek
    if(recv_all(s, h, 3) < 0)
        return -1;
    
    m->type = h[0];  
    
    // Bajty 1-2: długość (konwersja z big-endian)
    uint16_t nl; 
    memcpy(&nl, &h[1], 2);
    m->length = ntohs(nl);
    
    // Sprawdź czy długość nie przekracza bufora
    if(m->length > BUFFER_SIZE)
        return -1;
    
    // Odbierz dane (jeśli są)
    if(m->length && recv_all(s, m->value, m->length) < 0)
        return -1;
    
    return (int)(3 + m->length);  // Zwróć całkowitą liczbę bajtów
}

/*
 * init_board - Inicjalizuje plansze na początku gry
 * Wypełnia wszystkie pola wodą ('~') i zeruje mapę strzelanych pól
 */
static void init_board(void) {
    for(int y = 0; y < BOARD_SIZE; ++y)
        for(int x = 0; x < BOARD_SIZE; ++x) {
            board.display[y][x] = '~';  // Nasza plansza: woda
            board.enemy[y][x] = '~';    // Plansza wroga: woda
            tried[y][x] = 0;            // Jeszcze nie strzelaliśmy
        }
}

/*
 * print_boards - Wyświetla obie plansze obok siebie
 * Lewa: nasza plansza (widać nasze statki)
 * Prawa: plansza wroga (widać nasze strzały)
 */
static void print_boards(void) {
    printf("\n     Twoja plansza                    Plansza wroga\n");
    printf("   A B C D E F G H I J            A B C D E F G H I J\n");
    
    // Wyświetl każdy wiersz obu plansz
    for(int y = 0; y < BOARD_SIZE; ++y) {
        // Nasza plansza
        printf("%2d ", y);
        for(int x = 0; x < BOARD_SIZE; ++x) 
            printf("%c ", board.display[y][x]);
        
        // Plansza wroga
        printf("       %2d ", y);
        for(int x = 0; x < BOARD_SIZE; ++x) 
            printf("%c ", board.enemy[y][x]);
        
        printf("\n");
    }
    
    printf("\nLegenda: ~ = woda, S = statek, X = trafiony, O = pudlo\n");
}

/*
 * get_ship_config - Interaktywne ustawianie statków przez gracza
 * Zbiera od gracza pozycje wszystkich 10 statków:
 * - 1x czteromasztowiec (długość 4)
 * - 2x trójmasztowiec (długość 3)
 * - 3x dwumasztowiec (długość 2)
 * - 4x jednomasztowiec (długość 1)
 * 
 * Sprawdza poprawność (czy mieści się, czy nie koliduje)
 */
static int get_ship_config(Ship *ships) {
    printf("\n=== USTAWIENIE STATKOW ===\n");
    printf("- 1x czteromasztowiec (4)\n");
    printf("- 2x trojmasztowiec (3)\n");
    printf("- 3x dwumasztowiec (2)\n");
    printf("- 4x jednomasztowiec (1)\n");
    printf("Statki nie moga sie stykac nawet rogami.\n");
    
    // Plan: kolejność długości statków do ustawienia
    int plan[10] = {4, 3, 3, 2, 2, 2, 1, 1, 1, 1};
    int cnt = 0;  // Licznik ustawionych statków
    
    // Dla każdego statku
    for(int i = 0; i < 10; ++i) {
        int L = plan[i];  // Długość aktualnego statku
        printf("\nUstawianie statku o dlugosci %d:\n", L);
        
        // Pętla dopóki gracz nie postawi poprawnie statku
        while(1) {
            char col;      // Kolumna (A-J)
            int row, ori;  // Wiersz (0-9), orientacja (0/1)
            
            // Pobierz kolumnę
            printf("Kolumna (A-J): "); 
            scanf(" %c", &col); 
            col = (char)toupper((unsigned char)col);
            
            // Pobierz wiersz
            printf("Wiersz (0-9): "); 
            scanf("%d", &row);
            
            // Pobierz orientację (tylko dla statków >1)
            if (L > 1) { 
                printf("Orientacja (0=pionowo, 1=poziomo): "); 
                scanf("%d", &ori);
            } else {
                ori = 1;  // Jednomasztowiec: orientacja nie ma znaczenia
            }
            
            // Konwersja kolumny A-J na współrzędną 0-9
            int x = col - 'A';
            int y = row;
            
            // Sprawdź czy współrzędne są w zakresie planszy
            if (x < 0 || x >= BOARD_SIZE || y < 0 || y >= BOARD_SIZE) { 
                puts("Zle wspolrzedne!"); 
                continue; 
            }
            
            // Sprawdź czy statek mieści się na planszy
            if (ori && x + L > BOARD_SIZE) { 
                puts("Nie miesci sie w poziomie!"); 
                continue; 
            }
            if (!ori && y + L > BOARD_SIZE) { 
                puts("Nie miesci sie w pionie!"); 
                continue; 
            }
            
            // Sprawdź kolizje z innymi statkami (wraz z odstępem 1 pola)
            int bad = 0;
            for(int j = 0; j < L && !bad; ++j) {
                // Współrzędne j-tego segmentu statku
                int cx = ori ? x + j : x;
                int cy = ori ? y : y + j;
                
                // Sprawdź wszystkie sąsiednie pola (3x3 wokół segmentu)
                for(int dx = -1; dx <= 1 && !bad; ++dx)
                    for(int dy = -1; dy <= 1 && !bad; ++dy) {
                        int nx = cx + dx;
                        int ny = cy + dy;
                        
                        // Jeśli pole w zakresie planszy
                        if(nx >= 0 && nx < BOARD_SIZE && ny >= 0 && ny < BOARD_SIZE)
                            // Jeśli pole zajęte przez inny statek
                            if(board.display[ny][nx] == 'S') 
                                bad = 1;  // Kolizja!
                    }
            }
            
            if (bad) { 
                puts("Koliduje/styka sie!"); 
                continue; 
            }
            
            // Statek OK - zapisz konfigurację
            ships[cnt] = (Ship){x, y, L, ori}; 
            cnt++;
            
            // Umieść statek na naszej planszy
            for(int j = 0; j < L; ++j) { 
                int cx = ori ? x + j : x;
                int cy = ori ? y : y + j;
                board.display[cy][cx] = 'S';  // Oznacz pole jako statek
            }
            
            print_boards();  // Pokaż aktualny stan planszy
            break;  // Przejdź do następnego statku
        }
    }
    
    return cnt;  // Zwróć liczbę statków (zawsze 10)
}

int main(int argc, char *argv[]) {
    if (argc < 2) { 
        printf("Uzycie: %s <adres> [port]\n", argv[0]); 
        return 1;
    }
    
    const char *addr = argv[1];  // Adres serwera
    int port = (argc >= 3) ? atoi(argv[2]) : DEFAULT_PORT;  // Port (domyślnie 8888)
    
    // Rozwiąż nazwę DNS na adres IP
    struct hostent *h = gethostbyname(addr);
    if (!h) { 
        printf("Nie znaleziono serwera: %s\n", addr); 
        return 1; 
    }

    // Utwórz socket TCP
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if(s < 0) {  
        perror("socket"); 
        return 1; 
    }

    // Przygotuj adres serwera
    struct sockaddr_in sa; 
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;  // IPv4
    sa.sin_port = htons(port);  // Port w network byte order
    memcpy(&sa.sin_addr, h->h_addr_list[0], (size_t)h->h_length);  // Adres IP

    // Połącz się z serwerem
    printf("Laczenie z %s:%d...\n", addr, port);
    if (connect(s, (struct sockaddr*)&sa, sizeof(sa)) < 0) { 
        perror("connect"); 
        close(s); 
        return 1; 
    }

    puts("Polaczono!");

    init_board();  // Wyczyść plansze

    Ship ships[MAX_SHIPS]; 
    int n = get_ship_config(ships);  // Gracz ustawia statki interaktywnie
    
    // Serializuj statki do formatu binarnego
    // Każdy statek: 4 inty po 4 bajty = 16 bajtów w network byte order
    uint8_t ship_data[MAX_SHIPS * 16];
    for(int i = 0; i < n; ++i) {
        int off = i * 16;  // Offset dla i-tego statku
        
        // Konwertuj każde pole do network byte order (big-endian)
        int32_t xn = htonl(ships[i].x);
        int32_t yn = htonl(ships[i].y);
        int32_t ln = htonl(ships[i].length);
        int32_t hn = htonl(ships[i].horizontal);
        
        // Skopiuj do bufora
        memcpy(&ship_data[off],      &xn, 4);  // x
        memcpy(&ship_data[off + 4],  &yn, 4);  // y
        memcpy(&ship_data[off + 8],  &ln, 4);  // length
        memcpy(&ship_data[off + 12], &hn, 4);  // horizontal
    }

    // Wyślij konfigurację statków do serwera
    TLVMessage out; 
    create_tlv_message(&out, MSG_SHIPS_CONFIG, ship_data, (uint16_t)(n * 16));
     
    if (send_tlv_message(s, &out) < 0) { 
        puts("Blad wysylania statkow."); 
        close(s); 
        return 1; 
    }
    
    while(!game_over) {
        // WYKONANIE RUCHU (gdy mamy turę)
        
        while (my_turn && !game_over) {
            char col; 
            int row;
            
            printf("\nTwoja kolej – podaj strzal.\nKolumna (A-J): ");
            if (scanf(" %c", &col) != 1) { 
                my_turn = 0; 
                break; 
            }
            col = (char)toupper((unsigned char)col);
            
            printf("Wiersz (0-9): ");
            if (scanf("%d", &row) != 1) { 
                my_turn = 0; 
                break; 
            }
            
            // Konwersja na współrzędne
            int x = col - 'A';
            int y = row;
            
            // Sprawdź poprawność współrzędnych
            if (x >= 0 && x < BOARD_SIZE && y >= 0 && y < BOARD_SIZE) {
                // Sprawdź czy już strzelaliśmy w to pole
                if (tried[y][x]) {
                    puts("Juz tam strzelales — podaj inne pole.");
                    continue;  // Nie wysyłaj do serwera, pobierz nowe współrzędne
                }
                
                uint8_t payload[6];
                uint32_t t_be = htonl(current_turn_id);  // Token tury w network byte order
                memcpy(&payload[0], &t_be, 4);  // Bajty 0-3: turn_id
                payload[4] = (uint8_t)x;        // Bajt 4: x
                payload[5] = (uint8_t)y;        // Bajt 5: y
                
                // Wyślij ruch do serwera
                TLVMessage mv; 
                create_tlv_message(&mv, MSG_MOVE, payload, 6);
                if (send_tlv_message(s, &mv) < 0) { 
                    puts("Blad wysylania ruchu."); 
                    game_over = 1; 
                    break; 
                }
                
                printf("Strzelam w %c%d (turn_id=%u)...\n", col, row, current_turn_id);
                
                // Zaznacz pole jako ostrzelane
                tried[y][x] = 1;
                last_shot_was_mine = 1;  // Pamiętaj że to był nasz strzał
                my_turn = 0;  // Oddajemy turę (czekamy na odpowiedź)
            } else {
                puts("Nieprawidlowe wspolrzedne!");
            }
        }
        
        TLVMessage in;
        if (receive_tlv_message(s, &in) <= 0) { 
            puts("Polaczenie przerwane."); 
            break; 
        }

        // Obsługa różnych typów komunikatów
        switch(in.type) {
            // MSG_CONNECT - Potwierdzenie połączenia
            case MSG_CONNECT: {
                char txt[BUFFER_SIZE + 1]; 
                size_t L = in.length < BUFFER_SIZE ? in.length : BUFFER_SIZE;
                memcpy(txt, in.value, L);
                txt[L] = '\0'; 
                puts(txt);  // Wyświetl komunikat powitalny
                break;
            }
            // MSG_PLAYER_ID - Przypisane ID gracza (0 lub 1)
            case MSG_PLAYER_ID:
                if (in.length == 1) { 
                    my_player_id = in.value[0]; 
                    printf("Twoje ID: %d\n", my_player_id + 1); 
                }
                break;

            // MSG_WAIT - Oczekiwanie na drugiego gracza
            case MSG_WAIT: 
                puts("Oczekiwanie na przeciwnika..."); 
                break;

            // MSG_GAME_START - Rozpoczęcie gry
            case MSG_GAME_START: {
                puts("\n=== GRA ROZPOCZETA ===");
                if (in.length == 1) {
                    uint8_t starting = in.value[0];
                    if (starting == (uint8_t)my_player_id) { 
                        puts("Ty zaczynasz!");  // Czekamy na MSG_YOUR_TURN
                    } else { 
                        puts("Przeciwnik zaczyna!"); 
                    }
                } else {
                    puts("Czekaj na komende Twoja kolej...");
                }
                print_boards();
                break;
            }

            // MSG_YOUR_TURN - Twoja kolej + token tury
            case MSG_YOUR_TURN:
                if (in.length == 4) {
                    // Odczytaj token tury z network byte order
                    uint32_t t_be; 
                    memcpy(&t_be, &in.value[0], 4);
                    current_turn_id = ntohl(t_be);
                    my_turn = 1;
                    printf("\nTwoja kolej! (turn_id=%u)\n", current_turn_id);
                } else {
                    my_turn = 1;  
                    puts("\nTwoja kolej!");
                }
                break;

           // ================================================================
            // MSG_MOVE_RESULT - Wynik strzału (naszego lub przeciwnika)
            // Serwer wysyła ten komunikat do OBU graczy:
            // - Gracz który strzelał: dostaje info o wyniku swojego strzału
            // - Gracz który został ostrzelany: dostaje info gdzie został trafiony
            // ================================================================
            case MSG_MOVE_RESULT: {
                if (in.length >= 3) {
                    int x = in.value[0];  // Kolumna strzału (0-9)
                    int y = in.value[1];  // Wiersz strzału (0-9)
                    int r = in.value[2];  // Wynik: 0=pudło, 1=trafiony, 2=zatopiony
                    
                    // Określ kto strzelał i zaktualizuj odpowiednią planszę
                    if (last_shot_was_mine) {
                        // ===== TO BYŁ NASZ STRZAŁ =====
                        // Aktualizujemy planszę wroga (prawą)
                        
                        if (r == RESULT_HIT || r == RESULT_SUNK) 
                            board.enemy[y][x] = 'X';  // Statek wroga trafiony
                        else 
                            board.enemy[y][x] = 'O';  // Pudło - woda
                        
                        last_shot_was_mine = 0;  // Zresetuj flagę
                    } else {
                        // ===== TO BYŁ STRZAŁ PRZECIWNIKA =====
                        // Aktualizujemy naszą planszę (lewą)
                        
                        if (r == RESULT_HIT || r == RESULT_SUNK) 
                            board.display[y][x] = 'X';  // Nasz statek trafiony
                        else if (board.display[y][x] == '~') 
                            board.display[y][x] = 'O';  // Pudło
                    }
                    
                    // Wyświetl komunikat tekstowy o wyniku strzału
                    if (r == RESULT_HIT) 
                        puts("Trafiony!");
                    else if (r == RESULT_SUNK) 
                        puts("Trafiony, zatopiony!");
                    else 
                        puts("Pudlo!");
                    
                    print_boards();  // Odśwież wyświetlanie obu plansz
                }
                break;
            }

            // ================================================================
            // MSG_GAME_END - Koniec gry, ogłoszenie zwycięzcy
            // Serwer wysyła ten komunikat gdy jeden z graczy zatopi wszystkie
            // statki przeciwnika (ships_remaining == 0)
            // ================================================================
            case MSG_GAME_END: {
                puts("\n=== GRA ZAKONCZONA ===");
                
                // Sprawdź czy mamy wystarczające dane i znamy swoje ID
                if (in.length >= 1 && my_player_id != -1) {
                    uint8_t w = in.value[0];  // ID zwycięzcy (0 lub 1)
                    
                    // Porównaj ID zwycięzcy z naszym ID
                    if (w == (uint8_t)my_player_id) 
                        puts("WYGRALES!");  // My wygraliśmy!
                    else 
                        puts("PRZEGRALES!");  // Przeciwnik wygrał
                }
                
                game_over = 1;  // Ustaw flagę końca gry (wyjście z pętli while)
                break;
            }

            // ================================================================
            // MSG_ERROR - Komunikat błędu od serwera
            // 
            // Serwer może wysłać błąd gdy:
            // - Nieprawidłowy token tury (current_turn_id nie zgadza się)
            // - Próba strzału poza turą
            // - Inne błędy walidacji
            // 
            // Po błędzie pozwalamy graczowi strzelić ponownie
            // ================================================================
            case MSG_ERROR: {
                char txt[BUFFER_SIZE + 1]; 
                
                // Skopiuj komunikat błędu z payload
                size_t L = in.length < BUFFER_SIZE ? in.length : BUFFER_SIZE;
                memcpy(txt, in.value, L); 
                txt[L] = '\0';  
                
                printf("Blad: %s\n", txt);  // Wyświetl błąd

                // ===== POZWÓL NA PONOWNY STRZAŁ W TEJ SAMEJ TURZE =====
                last_shot_was_mine = 0;  // Wyczyść flagę ostatniego strzału
                my_turn = 1;             // Daj graczowi ponowną próbę
                puts("Podaj ponownie strzal.");
                break;
            }
        }
    }
    
    close(s);  // Zamknij socket (połączenie TCP z serwerem)
    puts("\nDo zobaczenia!");
    return 0;  // Zakończ program z kodem sukcesu
}
