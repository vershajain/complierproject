#include <stdio.h>
int main() {
    int count = 5;
    double avg = 2.5;
    char name[10];
    float f;
    printf("%\n", count);
    printf("%d %d\n", count);
    printf("%d\n", count, count);
    printf("%s\n", count);
    printf("%f\n", count);
    printf("%d\n", avg);
    printf("%q\n", count);
    printf("100%");
    printf("%d%%\n", count);
    scanf("%d", count);
    scanf("%f", &avg);
    scanf("%d", &f);
    scanf("%s", name);
    return 0;
}
