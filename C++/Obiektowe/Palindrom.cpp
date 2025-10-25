/*
Program który sprawdzi czy podan wyraz jest palindromem
*/

#include <iostream>
#include <string>
using namespace std;

int main() {
    string wyraz;
    cout << "Podaj wyraz: ";
    cin >> wyraz;

    bool palindrom = true;
    int n = wyraz.length();

    for (int i = 0; i < n / 2; i++) {
        if (wyraz[i] != wyraz[n - 1 - i]) {
            palindrom = false;
            break;
        }
    }

    if (palindrom)
        cout << "To jest palindrom." << endl;
    else
        cout << "To nie jest palindrom." << endl;

    return 0;
}
