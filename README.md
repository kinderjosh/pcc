[![build](https://github.com/kinderjosh/steelc/actions/workflows/build.yml/badge.svg)](https://github.com/kinderjosh/steelc/actions/workflows/build.yml) ![GitHub License](https://img.shields.io/github/license/kinderjosh/steelc)
 ![Static Badge](https://img.shields.io/badge/written_in-c-blue) ![Static Badge](https://img.shields.io/badge/i_hate-rust-red)



# The Steel C Language

Steel C is an immutable, strongly-typed language that aims to be safer and more convenient than C. The language is written completely from scratch without the use of LLVM or any third-party libraries. This means that the compiler **only targets x86-64 Linux systems and no other architectures**.

> [!WARNING]
> This language is still in development and not designed to be used yet. Expect breaking bugs and missing features.

## Development Progress

- [x] - Main function
- [x] - Variables
- [x] - Returns
- [x] - Calls
- [x] - Math
- [x] - Conditions
- [x] - Loops
- [x] - Arrays
- [x] - Pointers
- [ ] - Preprocessing
- [ ] - Begin standard library
- [ ] - Begin documentation
- [ ] - Build tool

## Dependencies

- gcc
- nasm
- make

## Quick Start

```bash
git clone https://github.com/kinderjosh/steelc.git
cd steelc
make
```

## Usage

```
./steelc [options...] <input file>
```

### Options

- ```-c``` - Output only object files.
- ```-o <output file>``` - Place the output into ```<output file>```.
- ```-t <test directory>``` - (Development only) Test each file in ```<test directory>```.
- ```-S``` - Output only assembly files.

## License

[MIT](./LICENSE)
