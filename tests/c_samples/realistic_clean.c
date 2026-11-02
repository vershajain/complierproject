#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define N 5
#define SQ(x) ((x)*(x))

static int table[N] = {1, 2, 3, 4, 5};
double avg(int *arr, int n) {
    int sum = 0;
    for (int i = 0; i < n; i++) sum += arr[i];
    return n > 0 ? (double)sum / n : 0.0;
}
int fact(int n) {
    if (n <= 1) return 1;
    return n * fact(n - 1);
}
void swap(int *a, int *b) { int t = *a; *a = *b; *b = t; }
int main(int argc, char *argv[]) {
    int a, b;
    char name[20];
    char *msg = "hi";
    scanf("%d %d", &a, &b);
    if (b != 0) printf("%d\n", a / b);
    int i = 0;
    do { i++; } while (i < N);
    for (;;) { if (i > 100) break; i += 2; }
    int *buf = (int *)malloc(N * sizeof(int));
    if (buf == NULL) return 1;
    buf[0] = SQ(3);
    free(buf);
    snprintf(name, sizeof(name), "%s", msg);
    swap(&a, &b);
    printf("%s %d %f\n", name, fact(5), avg(table, N));
    unsigned long big = 100000L;
    char c = 'a';
    c = c + 1;
    return big > 0 ? 0 : 1;
}
