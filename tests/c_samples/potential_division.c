int safe(int a, int b) {
    if (b != 0) {
        return a / b;
    }
    return 0;
}

int risky(int a, int b) {
    return a / b;
}

int main() {
    return safe(4, 2) + risky(4, 2);
}
