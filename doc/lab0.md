# Lab0: Slab Memory Allocator

| Name            | Student ID  | Email        | Date      |
| --------------- | ----------- | ------------ | --------- |
| Zecyel (朱程炀) | 23300240014 | i@zecyel.xyz | 2025.9.25 |

## Fuzz Test & Stress Test & Safety Test

Passed all the 29 tests.

| Single Thread Tests | Single Thread Tests  | Multi-Threads Tests |
| ------------------- | -------------------- | ------------------- |
| basic alloc         | alignment            | basic concurrent    |
| batch alloc         | stress               | race condition      |
| unaligned batch     | cache destroy        | cache sharing       |
| large batch         | error handling       | memory consistency  |
| huge batch          | multi cache          | performance         |
| random free         | extreme alloc        | stress concurrent   |
| ctor dtor           | corruption detection | fragmentation       |
| memory integrity    | fragmentation        | mixed sizes         |
| edge cases          | boundary conditions  | error handling      |
| reuse cycles        |                      | safety              |

## Statistics

| Algorithm   | Throughput (ops/s) | Delay (cycles) | Memory Utilize Rate |
| ----------- | ------------------ | -------------- | ------------------- |
| Object Pool |                    |                |                     |
| Slab        |                    |                |                     |
