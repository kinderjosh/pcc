int ret(int i) {
    return i;
}

void main() {
    mut int data[5];

    for (mut int i = 0; i < 5; i += 1)
        data[ret(i)] = ret(i);
}