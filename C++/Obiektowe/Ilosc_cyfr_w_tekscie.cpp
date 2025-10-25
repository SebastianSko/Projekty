/*
Program, który wczyta jedną linię tekstu z klawiatury do obiektu klasy string a następnie wypisuje
na ekranie ilość cyfr występujących we wprowadzonym tekście.
*/

#include "CMakeProject1.h"
#include <iostream>
#include<string>

using namespace  std;

int main() {
	string wyraz;

	getline(cin, wyraz);

	int liczba_cyfr = 0;


	string cyfry = "0123456789";

	for (char c : cyfry) {
		size_t pozycja = wyraz.find(c);

		while (pozycja != string::npos) {
			liczba_cyfr++;
			pozycja = wyraz.find(c, pozycja + 1);
		}
	}
	cout << "Jest" << liczba_cyfr << endl;
	return 0;




}
