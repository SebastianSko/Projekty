/*
Program, który na początku tworzy napis o treści “Congratulations Mrs. <name>, you and
Mr. <name> are the lucky recipients of a trip for two to XXXXXX. Your trip to
XXX is already scheduled ”. 
Wykonuje następujące operacje na tym stringu:
a. Zastąp każde wystąpienie “<name>” słowem “Smith”,
b. zastąp każde wystąpienie XXXX (niezależnie od ilości liter X) słowem “Siberia”
c. dodaj słowo “un” bezpośrednio przed słowem “lucky”
d. dodaj słowo “in December” na końcu stringu.

*/
#include "CMakeProject1.h"
#include<string>
#include<iostream>

using namespace std;

int main() {
	string tekst = "Congratulations Mrs. <name>, you and Mr. <name> are the lucky recipients of a trip for two to XXXXXX.Your trip to XXX is already scheduled";
	
	
	string do_zmiany = "<name>";
	string nowy = "Smith";

	size_t pozycja = tekst.find(do_zmiany);
	while (pozycja != string::npos) {
		tekst.replace(pozycja, do_zmiany.length(), nowy);
		pozycja = tekst.find(do_zmiany, pozycja + nowy.length());

	}

	

	string miejsce = "Syberia";
	size_t i = 0;

	while (i < tekst.size()) {
		if (tekst[i] == 'X') {
			size_t start = i;

			while (i < tekst.size() && tekst[i] == 'X') {
				i++;
			}
			tekst.replace(start, i - start, miejsce);
			i = start + miejsce.length();

		}
		else {
			i++;

		}
	}

	string slowo = "lucky";
	size_t pos = tekst.find(slowo);
	if (pos != string::npos) {
		tekst.insert(pos, "un");  
	}

	tekst += " in December";

	cout << "Nowy tekst: " << tekst << endl;
	return 0;


}

