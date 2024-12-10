int get(int x, char z, float y) {
    return y;
}

float get2() {
    return get(12, get(12, 2.2, 2), 8.8);
}

void get3() {
    char c = get2();
}

void main() {
    get3();
}