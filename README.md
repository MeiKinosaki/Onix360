# WX360

**Windows Xbox 360 Emulator** — projeto experimental de emulação do Xbox 360 para Windows x64, com foco em computadores modestos.

> **Estado:** v0.3b — estado arquitetural e subconjunto inicial do interpretador PowerPC implementados; CI em correção após falha de configuração CMake. Ainda não executa jogos comerciais.

## Objetivos de compatibilidade

- Windows 10/11 de 64 bits.
- Processadores x86-64 com **SSE2 como baseline**.
- Não exigir AVX nem AVX2.
- Meta de uso: computadores com até 4 GB de RAM (não é garantia de que jogos completos rodarão nessa memória).
- Arquitetura modular para CPU Xenon, memória, GPU Xenos, áudio e kernel.

## Princípios técnicos

1. Separar instruções PowerPC em módulos testáveis.
2. Manter caminhos escalares/SSE2; AVX e AVX2 nunca podem ser requisitos.
3. Medir memória por subsistema e evitar alocações grandes desnecessárias.
4. Não declarar compatibilidade, testes, builds ou execução de jogos sem evidência reproduzível.
5. Distinguir código implementado de funcionalidades ainda planejadas.

## Plano de versões

| Versão | Escopo |
|---|---|
| v0.1b | Estrutura C++/CMake e baseline SSE2 |
| v0.2b | Estado arquitetural inicial Xenon e testes unitários |
| v0.3b | Interpretador PowerPC inicial: ADDI, ADDIS, ADD, AND, OR, XOR |
| v0.4b | Memória, MMU e exceções |
| v0.5b | Fundamentos da GPU Xenos |
| v0.6b | Áudio e subsistemas do sistema |
| v0.7b | Carregador inicial de executáveis XEX |
| v0.8b | Kernel/HAL e serviços necessários |
| v0.9b | Integração, profiling e otimização |
| v1.0 | Marco de estabilidade baseado em testes e compatibilidade demonstrada |

## Build (Windows)

Requisitos de desenvolvimento: CMake 3.20+ e compilador C++20 (Visual Studio 2022 ou MinGW-w64 recente).

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

## Estado real

- [x] Repositório inicial e escopo documentados.\n- [x] Estado arquitetural inicial da CPU e testes unitários adicionados.\n- [ ] CI Windows/Linux aprovado.
- [x] Estrutura de estado de uma thread Xenon (GPRs, PC, LR, CTR, XER, CR, MSR e FPSCR).
- [x] Decodificador e interpretador inicial: ADDI, ADDIS, ADD, AND, OR e XOR.\n- [ ] Cobertura ampla da ISA Xenon, overflow, branches, loads/stores, FPU e VMX.
- [ ] MMU e memória do Xbox 360.
- [ ] GPU Xenos.
- [ ] Áudio, kernel e carregamento XEX.
- [ ] Execução de jogos.

**Aviso:** o limite de 4 GB é uma meta de otimização, não uma promessa de desempenho ou compatibilidade. A emulação completa do Xbox 360 é um projeto de grande porte.
