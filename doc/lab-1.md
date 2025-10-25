# Environment Setup



## Setup clang-format

Find the latest version of clang-format.

```bash
apt search clang | grep format
```

Install clang-format-20.

```bash
sudo apt install clang-format-20
```

Create a symbolic link for clang-format.

```bash
sudo ln -s /usr/bin/clang-format-20 /usr/bin/clang-format
```

Check the installed version.

```bash
clang-format --version
```

## Configure clang-format

Create the `.clang-format` file.

```bash
clang-format -style=google -dump-config > .clang-format
```

## Open Benchmarks

### By default (tests OFF):

```bash
make clean
make qemu
```

The kernel will boot normally without running any slab tests.

### To enable tests:

```bash
make clean
make CFLAGS="-DENABLE_SLAB_TESTS" qemu
```
