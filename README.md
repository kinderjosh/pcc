# TCC - Tuned C Compiler

> [!IMPORTANT]
> TCC is still a work-in-progress, expect bugs and missing features.

TCC is my own implementation of the C language. It only compiles for x86-64 Linux systems, assembled with [nasm](https://nasm.us). It doesn't use LLVM, the whole thing is built completely from scratch.

## Development Progress

- [x] - Main function
- [x] - Variables
- [x] - Returns
- [ ] - Calls
- [ ] - Math
- [ ] - Arrays
- [ ] - Includes
- [ ] - Begin standard library

## Dependencies

- gcc
- nasm
- make

## Quick Start

```bash
git clone https://github.com/kinderjosh/tcc.git
cd tcc
make
```

## Usage

```
./tcc [options...] <input file>
```

### Options

- ```-c``` - Output only object files.
- ```-o <output file>``` - Place the output into <output file>.
- ```-t <test directory>``` - (Development only) Test each file in <test directory>.
- ```-S``` - Output only assembly files.

## License

[MIT](./LICENSE)