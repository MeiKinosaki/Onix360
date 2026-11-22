# WX360

**Windows Xbox 360 Emulator** — emulador experimental do Xbox 360 para Windows x64, com foco em computadores modestos.

> **Versão em desenvolvimento:** v0.3b. O projeto ainda não executa jogos comerciais.

## Objetivos

- Windows 10/11 de 64 bits.
- x86-64 com SSE2 como baseline; AVX e AVX2 não são requisitos.
- Meta de otimização para computadores com até 4 GB de RAM; isso não garante que jogos completos rodem nessa capacidade.
- Arquitetura modular para CPU Xenon, memória, GPU Xenos, áudio e kernel.

## Implementação atual — CPU PowerPC/Xenon

O interpretador inicial contém suporte experimental para:

- Aritmética/lógica: ADDI, ADDIS, ADD, SUBF, AND, OR e XOR.
- Comparação: CMP e CMPL.
- Rotação/máscara: RLWINM.
- Branches: B e BC.
- Memória: LWZ, LBZ, STW e STB, usando um buffer de memória fornecido pelo chamador e big-endian.
- Resultado de execução distingue instrução não suportada e falha de acesso ao buffer.

**Limitações conhecidas:** subconjunto muito pequeno da ISA; sem execução completa de threads, exceções, MMU, BAT/TLB, FPU/VMX, dispositivos, kernel ou GPU. A semântica de branches e condições ainda precisa de expansão e validação contra a especificação PowerPC/Xenon.

## Build no Windows

Requisitos: CMake 3.20+ e compilador C++20 (Visual Studio 2022 ou MinGW-w64 recente).

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

O projeto não ativa flags de AVX/AVX2. SSE2 é a base pretendida para x86-64.

## Estado do projeto

- [x] Estrutura C++/CMake e CI Windows/Linux.
- [x] Estado arquitetural inicial de uma thread (GPRs, PC, LR, CTR, XER, CR, MSR e FPSCR).
- [x] Interpretador PowerPC inicial e testes unitários.
- [x] B/BC, LWZ/LBZ/STW/STB e testes de buffer big-endian adicionados.
- [ ] Validação final do CI para o commit mais recente em Windows e Linux.
- [ ] Cobertura mais ampla da ISA Xenon, branches, exceções e semântica de overflow/carry.
- [ ] Memória do Xbox 360, MMU, BAT/TLB.
- [ ] GPU Xenos, áudio, kernel/HAL e carregador XEX.
- [ ] Execução de jogos.

## Plano de versões

| Versão | Marco |
|---|---|
| v0.1b | Estrutura e baseline SSE2 |
| v0.2b | Estado arquitetural Xenon |
| v0.3b | Interpretador PowerPC inicial e testes |
| v0.4b | Memória do sistema, MMU e exceções |
| v0.5b | Fundamentos da GPU Xenos |
| v0.6b | Áudio e subsistemas |
| v0.7b | Carregador XEX |
| v0.8b | Kernel/HAL e serviços |
| v0.9b | Integração e otimização |
| v1.0 | Marco de estabilidade baseado em evidências |

**Regra de qualidade:** não declarar builds, testes, compatibilidade ou execução de jogos como concluídos sem evidência reproduzível.
