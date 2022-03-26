#include <stdio.h>
#include <stdint.h>

#define p 17
#define q 14
int f = 1 << q;

int itox(int n)
{
	return n * f;
}

int xtoi (int x) {
	return x / f;
}

int xtoi_round (int x) {
	if(x >= 0) return (x + (1<<13)) / (1<<14);
	else return (x - (1<<13)) / (1<<14);
}

int addxy (int x, int y) {
	return x + y;
}

int subxy (int x, int y) {
	return x - y;
}

int addxn (int x, int n) {
	return x + n * f;
}

int subxn (int x, int n) {
	return x - n * f;
}

int mulxy (int x, int y) {
	return ((int64_t) x) * y / f;
}

int mulxn (int x, int n) {
	return x * n;
}

int divxy (int x, int y) {
	return ((int64_t) x) * f / y;
}

int divxn (int x, int n) {
	return x / n;
}