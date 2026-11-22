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

- Aritmética/lógica: ADDI, ADDIS, ADDIC, ADDIC., SUBFIC, ADD, ADDE, ADDME, ADDZE, SUBF, SUBFE, SUBFME, SUBFZE, NEG, AND, OR e XOR.
- Comparação: CMP e CMPL.
- Rotação/máscara: RLWINM.
- Branches: B e BC.
- Memória: LWZ, LWZU, LBZ, STW, STWU, STB, LHZ, LHZU, LHA, LHAU, STH e STHU, usando um buffer de memória fornecido pelo chamador e big-endian; formas update atualizam RA após acesso bem-sucedido.
- Resultado de execução distingue instrução não suportada e falha de acesso ao buffer.

**Limitações conhecidas:** subconjunto muito pequeno da ISA; sem execução completa de threads, exceções, MMU, BAT/TLB, FPU/VMX, dispositivos, kernel ou GPU. A semântica de branches e condições ainda precisa de expansão e validação contra a especificação PowerPC/Xenon.

## Imagens de disco Xbox 360 (em desenvolvimento)

- Leitor inicial de arquivos ISO com leitura de setores sob demanda.
- Validação experimental do descritor XDVDFS e verificação dos limites da imagem.
- Parser experimental da árvore de diretório da raiz e busca de arquivo por nome; leitura parcial de arquivo para evitar carregar a imagem inteira.
- O suporte completo exige parser XDVDFS, resolução de caminhos e integração com o futuro loader XEX.

## Interface Windows inspirada no Aurora (protótipo)\n\n- Janela Win32 leve com tema escuro azul/ciano, navegação lateral e painéis de biblioteca.\n- Estrutura visual inicial para Início, Biblioteca, Arquivos, Save States e Configurações.\n- O protótipo ainda usa cartões demonstrativos: não possui renderização real das capas, seleção por controle, busca/filtros funcionais, importação de jogos pela interface nem ações conectadas aos slots.\n\n## Biblioteca de capas (em desenvolvimento)\n\n- Cache local por identificador de jogo, em `covers/` quando essa pasta for fornecida ao componente.\n- Cliente HTTPS nativo do Windows via WinHTTP, com timeout e limite de 8 MiB por imagem.\n- Cache valida assinatura JPEG (SOI/EOI), rejeita respostas HTML/arquivos inválidos e só publica o download após validação.\n- Reutilização de capas já armazenadas, enumeração da cache e validação de identificadores para impedir traversal de diretórios.\n- A camada HTTP está implementada para Windows, mas ainda falta um catálogo/provedor de metadados e a identificação automática de Title ID a partir de XEX/ISO. O download online só pode ocorrer quando a aplicação fornecer uma URL de capa válida; a interface gráfica também está pendente.\n\n## Build no Windows

Requisitos: CMake 3.20+ e compilador C++20 (Visual Studio 2022 ou MinGW-w64 recente).

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

O projeto não ativa flags de AVX/AVX2. SSE2 é a base pretendida para x86-64.

## Estado do projeto

- [x] Estrutura C++/CMake e CI Windows/Linux.
- [x] Estado arquitetural inicial de uma thread (GPRs, PC, LR, CTR, XER, CR, MSR e FPSCR).\n- [x] Formato versionado de save state para estado da CPU Xenon e RAM, com verificação de integridade e limite de memória.\n- [x] Gerenciador inicial de 10 slots independentes para salvar, carregar, verificar e apagar snapshots CPU/RAM; testes adicionados ao CI.
- [x] Interpretador PowerPC inicial e testes unitários.
- [x] B/BC, ADDIC/ADDIC., ADDE/ADDME/SUBFE/SUBFME/ADDZE/SUBFZE (código e testes adicionados), acessos de byte/palavra/meia-palavra (LBZ/LWZ/LHZ/LHA e STB/STW/STH), formas update e testes de buffer big-endian adicionados.
- [ ] Validação final do CI para o commit mais recente em Windows e Linux.
- [ ] Cobertura mais ampla da ISA Xenon, branches, exceções e semântica de overflow/carry.
- [x] Leitor ISO inicial: validação do descritor XDVDFS e leitura sob demanda de setores (testes adicionados; aguardando CI).\n- [x] Parser experimental da árvore XDVDFS da raiz e busca de `default.xex` (testes enviados ao CI).\n- [ ] Navegação recursiva XDVDFS e integração ISO/XEX ao carregador.\n- [x] Biblioteca de capas com cache local, downloader HTTPS WinHTTP no Windows e validação JPEG.\n- [ ] Integrar catálogo/provedor de metadados, identificar jogos automaticamente e exibir capas reais na biblioteca gráfica.\n- [x] Protótipo inicial de dashboard Win32 inspirado no Aurora.\n- [ ] Conectar navegação, seleção por controle/mouse, importação de jogos, biblioteca de capas e gerenciamento dos save states à interface.\n- [ ] Integração dos slots com pausa/retomada do loop de emulação e interface gráfica.\n- [ ] Snapshot completo do sistema: GPU, áudio, dispositivos, timers, kernel e estado de todos os threads.\n- [ ] Memória do Xbox 360, MMU, BAT/TLB.
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
