int test(int *ptr) {
    return *ptr;
}

void main() {
    int data = 12;
    int *ptr = &data;
    mut int data2 = test(ptr);
}