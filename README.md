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
- POP
- PUSH
- SUB
- RET
- XOR

Those instructions only can operate on registers and immediate (32 bits) values.

## Examples

### Count 100 times

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

## Fib

```
fib:
  cmp rdi, 2
  jl .fib_exit

  push r10
  push r11

  mov r10, rdi

  sub rdi, 1
  call fib

  mov r11, rax
  mov rdi, r10

  sub rdi, 2
  call fib

  add rax, r11

  pop r11
  pop r10

  ret

.fib_exit:
  mov rax, rdi
  ret
```
**output**: 0x4881ff020000000f8c2d000000415241534c8bd74881ef01000000e8e0ffffff4c8bd8498bfa4881ef02000000e8ceffffff4903c3415b415ac3488bc7c3
