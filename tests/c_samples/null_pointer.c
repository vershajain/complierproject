#include <stdlib.h>
int main() {
    int *p = NULL;
    *p = 5;
    int *q = malloc(sizeof(int));
    *q = 3;
    return 0;
}
