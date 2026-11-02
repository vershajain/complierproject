#include <stdio.h>
#define LIMIT 10

int compute(int n) {
    int unused = 42;
    int zero = 0;
    int result = n / zero;
    if (n > LIMIT) {
        return result;
    }
}

int main() {
    int total;
    int count = 0;
    while (1) {
        count++;
    }
    printf("%d\n", total);
    return 0;
}
