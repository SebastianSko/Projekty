/*
Klasę o nazwie TQuadEq reprezentującą trójmian kwadratowy. Klasa zawierać:
a. pola reprezentująca współczynniki trójmianu
b. Konstruktor domyślny oraz parametryczny
c. Gettery oraz settery dla wszystkich pól klasy
d. metodę double ComputeDelta(void) const zwracającą wartość delty trójmianu
e. metodę int GetNumRoots(const double delta) const zwracającą liczbę pierwiastków
rzeczywistych trójmianu
f. metodę GetRoots(double& root1, double& root2) const zwracającą rzeczywiste
pierwiastki trójmianu.

*/
#include "CMakeProject1.h"
#include <iostream>

using namespace std;

class TQuadEq {
	double a, b, c;

public:
	TQuadEq() {
		a = 0.0;
		b = 0.0;
		c = 0.0;
		cout << "Domyslne wspolczynniki 0 , 0 , 0" << endl;
	}
	
	TQuadEq(double a_val = 0.0, double b_val = 0.0, double c_val = 0.0) : a{a_val}, b{b_val}, c{c_val} {}

	double getA() const { return a; }
	double getB() const { return b; }
	double getC() const { return c; }

	void setA(double val) { a = val; }
	void setB(double val) { b = val; }
	void setC(double val) { c = val; }

	double ComputeDelta(void) const { return b * b - 4 * a * c; }

	int GetNumRoots(const double delta) const {
		if (delta > 0) return 2;
		else if (abs(delta) < 1e-9) return 1;
		else return 0;
	}

	void GetRoots(double& root1, double& root2) const {
		double delta = ComputeDelta();
		int numRoots = GetNumRoots(delta);

		if (numRoots == 2) {
			root1 = (-b - sqrt(delta)) / (2 * a);
			root2 = (-b + sqrt(delta)) / (2 * a);
		}
		else if (numRoots == 1) {
			root1 = root2 = -b / (2 * a);
		}
		else {
		
			root1 = root2 = NAN; 
		}
		

	}
};

int main() {
	double ax, bx, cx;
	cout << "Podaj a: " << endl;
	cin >> ax;
	cout << "Podaj b: " << endl;
	cin >> bx;
	cout << "Podaj c: " << endl;
	cin >> cx;

	TQuadEq obj(ax, bx, cx);

	double delta = obj.ComputeDelta();
	double x1, x2;
	obj.GetRoots(x1, x2);

	cout << "\nTrojmian: " << ax << "x^2 + " << bx << "x + " << cx << " = 0" << endl;
	cout << "Delta = " << delta << endl;

	int numRoots = obj.GetNumRoots(delta);
	cout << "Liczba pierwiastkow rzeczywistych: " << numRoots << endl;

	if (numRoots == 2)
		cout << "Pierwiastki: x1 = " << x1 << ", x2 = " << x2 << endl;
	else if (numRoots == 1)
		cout << "Pierwiastek podwojny: x = " << x1 << endl;
	else
		cout << "Brak pierwiastkow rzeczywistych." << endl;

	return 0;
}
