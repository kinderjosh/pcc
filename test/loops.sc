void main() {
    for (mut int i = 0; i < 10; i += 1) {
        mut float a = i;
        a = a / i * 9.9;
    }

    mut int i = 10;

    while (i > 5 && i < 20)
        i /= 2;

    do {
        i *= 2.2;
        float a = i;
    } while (i < 100);
}