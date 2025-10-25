/*
Program, który wczyta plik tekstowy i obliczy następujące statystyki:
a. Całkowita liczba wierszy.
b. Całkowita liczba słów (rozdzielone białymi znakami).
c. Całkowita liczba znaków (w tym spacje i znaki interpunkcyjne).
*/

#include "CMakeProject1.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>

using namespace std;

int main() {
    string nazwaPliku;
    cout << "Podaj nazwe pliku do analizy: ";
    cin >> nazwaPliku;

    ifstream plik(nazwaPliku);
    if (!plik.is_open()) {
        cerr << "❌ Blad: nie udalo sie otworzyc pliku " << nazwaPliku << endl;
        return 1;
    }

    int liczbaWierszy = 0;
    int liczbaSlow = 0;
    int liczbaZnakow = 0;
    string linia;

    while (getline(plik, linia)) {
        liczbaWierszy++;

        // liczba znaków (w tym spacje i interpunkcja)
        liczbaZnakow += linia.size() + 1; // +1 za znak nowej linii

        // liczba słów (rozdzielone białymi znakami)
        stringstream ss(linia);
        string slowo;
        while (ss >> slowo) {
            liczbaSlow++;
        }
    }

    plik.close();

    cout << "\n=== STATYSTYKI PLIKU ===" << endl;
    cout << "Liczba wierszy: " << liczbaWierszy << endl;
    cout << "Liczba slow:    " << liczbaSlow << endl;
    cout << "Liczba znakow:  " << liczbaZnakow << endl;

    return 0;
}
