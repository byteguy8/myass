# MyAss - Toy x86_64 Assembler

This is just a toy x86_64 'assembler' I'm building for learning purposes. The reason I started this project, beyond my curiosity about how this things work, is to use it for futures (toys ofcourse) projects, like a JIT (toy) compiler.

For now, it supports a few instructions:

- ADD
- CMP
- IDIV
- IMUL
- JE
- JG
- JGE
- JLE
- JMP
- MOV
- SUB
- RET
- XOR

Those instructions only can operate on registers and immediate (32 bits) values.

## Examples

```
  mov r10, 1
  mov r11, 100

.iter:
  cmp r10, r11
  jg .exit

  add r10, 1
  jmp .iter

.exit:
  ret
```
**output**: 0x49c7c20100000049c7c3640000004d3bd30f8f0c0000004981c201000000e9ebffffffc3
