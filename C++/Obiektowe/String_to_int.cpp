/*
Program, który obliczy sumę dwóch dodatnich wartości całkowitych. Suma do obliczenia jest podana
jako a ciąg, np. “123 + 37”, “78+ 99” itd. Program działa następująco:
a. Prosi użytkownika o wprowadzenie ciągu;
b. Eliminuje dodatkowe spacje z ciągu;
c. Wyodrębnia dwa podciągi reprezentujące dwa argumenty, takie jak „123” i „37”;
d. Oblicza wartości całkowite tych dwóch podciągów, więc „123” jest przekształcane na wartość 123 itd.
e. Wydrukuje sumę dwóch wartości

*/
#include "CMakeProject1.h"
#include<string>
using namespace std;

int main()
{
	string ciag;
	cout << "Wprowadz ciag:" << endl;
	getline(cin, ciag);

	string bezspacji = "";
	for (char c : ciag) {
		if (c != ' ') {
			bezspacji += c;

		}
	}
	cout << bezspacji << endl;
	int pos = -1;
	char op = 0;
	for (int i = 0; i < bezspacji.size(); i++) {
		if (bezspacji[i] == '+') {
			op = bezspacji[i];
			pos = i;
			break;
		}
	}
	string l1 = bezspacji.substr(0, pos);
	string l2 = bezspacji.substr(pos + 1);

	double zmienna1 = stod(l1);
	double zmienna2 = stod(l2);

	double wynik;

	switch (op) {
	case '+':
		wynik = zmienna1 + zmienna2;
		cout << wynik << endl;

	}



	return 0;
}
