int get() {
    char a = 'A';
    return 22 - 88.2 * a;
}

void main() {
    float a = 22.24;
    float b = 12 - get() / a + 5;
    int c = a + b / 8;
    int d = c % 64;
}