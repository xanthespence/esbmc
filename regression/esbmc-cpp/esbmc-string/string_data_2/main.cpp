#include <string>
#include <iostream>
#include <cassert>
#include <cstring>

using namespace std;

int main(){
	string str;
	char cstr[12];
	str = "Test string";
	strcpy(cstr, str.data());
	assert(!strcmp(cstr, "Test string"));
	cout << cstr << endl;
}
