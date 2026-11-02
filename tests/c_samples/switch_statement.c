#include <stdio.h>
int grade(int s) {
    switch (s) {
        case 1: printf("one\n");
        case 2: printf("two\n"); break;
        case 2: break;
        default: return 0;
    }
}
int all(int s) {
    switch (s) {
        case 1: return 1;
        default: return 2;
    }
}
int main() {
    int x;
    switch (3) { case 1: x = 1; break; default: x = 2; }
    return x + grade(1) + all(2);
}
