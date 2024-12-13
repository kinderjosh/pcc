<img height=100 src="res/shield.svg" alt="logo">

# Steel C

Steel C is a reimplementation of the C language from scratch. It aims to be stricter and safer through the introduction of immutable variables and a strongly typed syntax. It doesn't use LLVM, it has its own custom backend that only generates x86-64 Linux assembly.

This is NOT a replacement for C, because nothing will replace C. This is just a fun little project of mine.

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
- [ ] - Type casting
- [ ] - Arrays
- [ ] - Pointers
- [ ] - Includes
- [ ] - Begin standard library

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
