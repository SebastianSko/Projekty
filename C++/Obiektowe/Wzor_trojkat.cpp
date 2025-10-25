/*
Program, który wczytuje z klawiatury dodatnią liczbę całkowitą n i wypisuje wzór trójkąta
prostokątnego o n rzędach z użyciem gwiazdek *. Wzór powinien być zgodny z następującymi zasadami:
przeciwprostokątna (przekątna) i lewa krawędź są wykonane z *, wnętrze składa się ze spacji, podstawa
(ostatni rząd) jest całkowicie wypełniona *. Na przykład dla n równego 5 program powinien wypisać na
ekranie wzór: 
*
**
* *
*  *
*   *
******
*/

#include "CMakeProject1.h"
#include<string>
using namespace std;

int main()
{
	int n;
	cout << "Podaj liczbe wierszy: " << endl;
	cin >> n;

	if (n <= 0) {
		cout << "Blad" << endl;
		return 1;
	}

	for (int i = 1; i <= n; i++) {
		if (i == 1) {
			cout << "*" << endl;

		}
		else if (i == n) {
			for (int j = 1; j <= n; j++) cout << "*";
			cout << endl;
			
		}
		else {
			cout << "*";
			for (int j = 1; j <= i - 2; j++) cout << " ";
			cout << "*" << endl;
			
		}
	}

	return 0;


}
