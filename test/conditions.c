void main() {
    int a = 12;

    if (a + 5 > a - 2)
        a = 5;
    else if (a * 55.2342 >= 25442.242)
        a %= 2;
    else {
        float b = a / 5.556;
        b /= a;
    }

    /* Scopes are finally implemented, so you cant do
     * something like a = b here
     */
}
