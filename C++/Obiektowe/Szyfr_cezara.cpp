/*
Klucz szyfru cezara to 3. Napisz program, który wczyta jedna linie tekst z klawiatury, wypisze ja na ekranie po zaszyfrowaniu. 
W kolejnej linii wypisz również tekst rozszyfrowany, za pomocą funkcji która rozszyfrowuje tekst. 

*/

#include <iostream>
#include <string>
using namespace std;


string deszyfruj(const string& tekst, int klucz) {
    string wynik = tekst;
    for (int i = 0; i < tekst.length(); i++) {
        if (tekst[i] >= 'a' && tekst[i] <= 'z') {
            wynik[i] = ((tekst[i] - 'a' - klucz + 26) % 26) + 'a';

        }
        else {
            wynik[i] = tekst[i];
        }
    }
    return wynik;
}

int main() {
    int klucz = 3;

    string tekst;
    cout << "Podaj tekst: " << endl;
    getline(cin, tekst);

    string zaszyfrowany = tekst;

    for (int i = 0; i < tekst.length(); i++) {
        if (tekst[i] >= 'a' && tekst[i] <= 'z') {
            zaszyfrowany[i] = ((tekst[i] - 'a' + klucz) % 26) + 'a';

        }
        else {
            zaszyfrowany[i] = tekst[i];
        }
    }

    string deszyfrowany = deszyfruj(zaszyfrowany, klucz);

    cout << "Zaszyfrowany: " << zaszyfrowany << endl;
    cout << "Zdeszyfrowany: " << deszyfrowany << endl;

    return 0;
}
