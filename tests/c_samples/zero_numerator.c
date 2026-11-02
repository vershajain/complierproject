int f(int b) {
    int r = 0 / b;
    return r;
}

int main() {
    int a = 0;
    int c = 0 / 5;
    int d = a % 7;
    return f(3) + c + d;
}
