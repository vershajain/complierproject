int main() {
    int a[5];
    int ok[3] = {1, 2, 3};
    int auto_sized[] = {4, 5};
    a[4] = 1;
    a[5] = 2;
    ok[3] = 9;
    auto_sized[2] = 1;
    int *p = &a[5];
    int m[2][2];
    m[5][5] = 0;
    return a[-1] + ok[0] + auto_sized[1] + (p != 0);
}
