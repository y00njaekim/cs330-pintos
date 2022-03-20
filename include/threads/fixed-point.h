#include <stdio.h>
#include <stdint.h>
#define p 17
#define q 14
#define f 1 << q

int itox (int n) {
	return n * f;
}

int xtoi (int x) {
	return x / f;
}

int xtoi_round (int x) {
	if(x >= 0) return (x + f / 2) / f;
	else return (x - f / 2) / f;
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