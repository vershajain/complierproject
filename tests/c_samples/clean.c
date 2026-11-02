#include <stdio.h>

int add(int a, int b) {
    return a + b;
}

int main() {
    int total = add(2, 3);
    for (int i = 0; i < 3; i++) {
        total += i;
    }
    printf("%d\n", total);
    return 0;
}
