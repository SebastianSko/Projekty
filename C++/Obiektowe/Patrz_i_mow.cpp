/*
Brytyjski matematyk John Conway zaproponowal szezególny rodzaj ciagu liczbowego znany jako ciag "patrz i mów".
Konstrukcja kolejnych liczb w tym ciagu przebiega nastepujaco: 
Załóżmy, ze pierwszym elementem ciagu jest zestaw cyfr 55. Ciag ten sklada sie z dwóch piatek i obie te liczby, 2 i 5, wymówione przy opisie pierwszego wyrazu zestawiamy do drugiego wyrazu konstruowanego ciagu: 25. 
Ten nowy zestaw to z kolei jedna dwójka i jedna piatka czyli 1215. Zatem kazdy kolejny element konstruowanego ciagu powstaje zawsze przez wypowiedzenie poprzedniego i zapisanie wszystkich wypowiedzianych cyfr. 
Jesli zatem zaczniemy od ciagu cyfr 55, to pierwsze pieć wyrazow w naszym ciagu bedzie mieć postac: 55, 25, 1215, 11121115, 31123115. 
Napisz program, który najpierw wczyta z klawiatury jako string ciag cyfr bedacych pierwszym elementem konstruowanego ciagu, a nastepnie wczyta liczbę n. 
Na koniec program ma wypisać na ekranie n-ty wyraz ciagu "patrz i mów". Na przyklad, jesli wprowadzony zostanie ciag cyfr 55 oraz liczba 4 to zostanie wypisany ciag 11121115. 

*/

#include "CMakeProject1.h"
#include <iostream>
#include <string>

using namespace std;

int main() {
    string ciag;
    int n;

    cout << "Podaj n: " << endl;
    cin >> n;
    
    cout << "Podaj 1 wyraz ciagu: " << endl;
    cin >> ciag;

    for (int krok = 1; krok < n; krok++) {
        string nowy = "";
        int licznik = 1;

        for (int i = 0; i < ciag.length(); i++) {
            if (i + 1 < ciag.length() && ciag[i] == ciag[i + 1]) {
                licznik++;
            }
            else {
                nowy += to_string(licznik);
                nowy += ciag[i];
                licznik = 1;

            }
        }
        ciag = nowy;
    }

    cout << "Ostatni wyraz to: " << ciag << endl;
   


    return 0;

}
