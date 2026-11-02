#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int helper(int a);

int main() {
    int i;
    int arr[5];
    for (i = 0; i <= 5; i++) { arr[i] = i; }
    for (int j = 0; j < 10; j--) { printf("%d\n", j); }
    int *p;
    *p = 3;
    int *q = malloc(sizeof(int));
    if (q == NULL) return 1;
    *q = 1;
    free(q);
    *q = 2;
    free(q);
    char *leak = (char *)malloc(10);
    leak[0] = 'a';
    int x = 5;
    if (x > 2) { x = 1; }
    int flag = 0;
    if (flag) { x = 2; }
    float avg = x / 2;
    char buf[4];
    strcpy(buf, "hello");
    gets(buf);
    if (avg == 0.5) x = 0;
    int r = 7 % 1;
    int z = x * 0;
    x == 3;
    int k = 300; char c = 300;
    return helper(x) + r + z + k + c;
}
