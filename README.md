# WX360

**Windows Xbox 360 Emulator** — emulador experimental do Xbox 360 para Windows x64, com foco em computadores modestos.

> **Versão em desenvolvimento:** v0.3b. O projeto ainda não executa jogos comerciais.

## Objetivos

- Windows 10/11 de 64 bits.
- x86-64 com SSE2 (baseline da arquitetura); AVX e AVX2 não são requisitos.
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

## Memória física Xenon e integração inicial de execução

- Memória física limitada e alocada sob demanda pelo tamanho configurado, com páginas de 4 KiB.
- Permissões por página para leitura, escrita e execução; fetch de instrução em big-endian respeita a permissão Execute.
- Núcleo experimental conecta PC da thread, fetch de instrução na RAM e interpretador PowerPC.
- **Limitação:** acessos de dados do interpretador ainda usam o buffer legado diretamente; MMU, tradução virtual, BAT/TLB, proteção de dados e exceções arquiteturais ainda não estão integrados.

## Cabeçalho XEX2 (análise inicial)

- Validação de assinatura XEX2 e limites do diretório de cabeçalhos opcionais.
- Extração experimental de flags do módulo, offset PE, offset de segurança e entry point (0x00010100).
- **Limitação importante:** ainda não descriptografa/descomprime XEX, não interpreta todas as estruturas de segurança, não mapeia seções PE na memória e não inicia jogos comerciais.

## Imagens de disco Xbox 360 (em desenvolvimento)

- Leitor inicial de arquivos ISO com leitura de setores sob demanda.
- Validação experimental do descritor XDVDFS e verificação dos limites da imagem.
- Parser experimental da árvore de diretório da raiz e busca de arquivo por nome; leitura parcial de arquivo para evitar carregar a imagem inteira.
- O suporte completo exige parser XDVDFS, resolução de caminhos e integração com o futuro loader XEX.

## Interface Windows inspirada no Aurora (protótipo)

- Janela Win32 leve com tema escuro azul/ciano, navegação lateral e painéis de biblioteca.
- Telas separadas para Início, Biblioteca, Arquivos, Save States e Configurações, com conteúdo informativo específico por seção.
- Destaque de navegação por teclado (setas) e mouse no menu lateral.
- A Biblioteca ainda não indexa jogos nem desenha capas reais; Arquivos não importa jogos e os slots exibidos ainda não consultam o gerenciador de save states.
- Busca/filtros, seleção por controle, ações de importação e configurações efetivas continuam pendentes.

## Biblioteca de capas (em desenvolvimento)

- Cache local por identificador de jogo, em `covers/` quando essa pasta for fornecida ao componente.
- Cliente HTTPS nativo do Windows via WinHTTP, com timeout e limite de 8 MiB por imagem.
- Cache valida assinatura JPEG (SOI/EOI), rejeita respostas HTML/arquivos inválidos e só publica o download após validação.
- Reutilização de capas já armazenadas, enumeração da cache e validação de identificadores para impedir traversal de diretórios.
- A camada HTTP está implementada para Windows, mas ainda falta um catálogo/provedor de metadados e a identificação automática de Title ID a partir de XEX/ISO. O download online só pode ocorrer quando a aplicação fornecer uma URL de capa válida; a interface gráfica também está pendente.

## Parser inicial de cabeçalho STFS

- Lê o cabeçalho fixo `0x971A` e reconhece os magics `CON `, `LIVE` e `PIRS`.
- Extrai tipo de conteúdo, versão dos metadados, tamanho declarado, Title ID, nomes de título/publicador, contagem/tamanho de arquivos de dados e campos básicos do descritor STFS.
- Interpreta contagem de blocos LE16, índice de bloco LE24 e metadados gerais BE.
- Rejeita magic desconhecido, cabeçalho truncado, tamanho declarado fora dos limites, descritor inválido, contagens inconsistentes e volume SVOD ainda não suportado.
- Testes sintéticos verificam extração básica, magic inválido e recusa segura de SVOD.
- **Limite:** não verifica RSA/assinaturas nem SHA-1, não percorre cadeias de blocos, não lê a tabela de arquivos e não extrai arquivos. Cabeçalho estruturalmente válido não comprova autenticidade ou integridade.

## Biblioteca Xbox 360 Content/GOD (identificação inicial)

- O scanner reconhece diretórios de Games on Demand pelo caminho de conteúdo `00007000` quando existe um arquivo/entrada `Data0000`.
- Identifica diretórios de conteúdo pelos tipos `000D0000` (XBLA), `00000002` (DLC) e `000B0000` (Title Update).
- Reconhece candidatos a pacotes STFS pelas extensões `.con`, `.live` e `.pirs` e tenta analisar o cabeçalho; quando válido, a interface mostra Title ID e nome do título.
- O modo portátil cria `roms/GOD` e `roms/Content/0000000000000000`; a tela Arquivos apresenta os itens detectados.
- Testes dedicados cobrem cada tipo de pasta, as três extensões STFS e diretório inexistente.
- **Limite importante:** isso é descoberta por nomes/caminhos, não valida a integridade nem analisa cabeçalhos, hashes, tabelas de arquivos ou assinaturas STFS. Não extrai pacotes, não descriptografa conteúdo protegido, não monta GOD e não carrega/ executa jogos. Os arquivos de teste são placeholders, não pacotes Xbox 360 válidos.

## Biblioteca portátil ISO/XEX

- O scanner percorre recursivamente `WX360-data/roms` e identifica extensões `.iso` e `.xex` sem diferenciar maiúsculas/minúsculas.
- A tela Biblioteca do dashboard Aurora lista os arquivos encontrados (até oito itens visíveis nesta primeira integração).
- A listagem mostra nomes de arquivos; ainda não carrega jogos, não valida o conteúdo dos arquivos e não resolve capas.
- O teste dedicado cria arquivos ISO/XEX e um arquivo não suportado em uma árvore temporária para verificar a descoberta.

## Versão portátil (sem instalação)

- O WX360 foi configurado para iniciar sem instalador: basta extrair o pacote ZIP e abrir `WX360.exe`.
- Na inicialização, a aplicação cria `WX360-data/roms`, `WX360-data/covers` e `WX360-data/saves` ao lado do executável.
- Configurações e dados graváveis ficam nessa pasta portátil, facilitando copiar a pasta completa para outro computador.
- O runtime MSVC é ligado estaticamente no build Windows para evitar exigir a instalação separada do Visual C++ Redistributable.
- O CI Windows está configurado para preparar e publicar o artefato `WX360-Windows-x64-Portable.zip` após build e testes bem-sucedidos.
- **Atenção:** o pacote portátil ainda depende do resultado do CI para ser considerado gerado e validado; a aplicação continua experimental e não executa jogos comerciais completos.

## Pastas de dados do usuário (em desenvolvimento)

- Raiz do modo portátil: `WX360-data/` ao lado de `WX360.exe` no Windows. O diretório pode ser escolhido explicitamente nos testes e em futuras configurações.
- `roms/`: biblioteca de jogos (ISO/XEX), pasta `GOD/` e estrutura `Content/0000000000000000/` para conteúdo; arquivos originais não são modificados.
- `covers/`: cache de capas JPEG.
- `saves/`: slots e arquivos de Save State.
- `configs/`: configurações persistentes (ainda aguardando integração com a interface).
- O componente cria as pastas de forma idempotente e permite configurar uma raiz alternativa para testes.
- A interface ainda precisa ser conectada a esses diretórios para importar e indexar jogos.

## Build no Windows

Requisitos: CMake 3.20+ e compilador C++20 (Visual Studio 2022 ou MinGW-w64 recente).

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

O alvo recomendado no Windows é x86-64 (x64), que fornece SSE2 como baseline. AVX e AVX2 não são habilitados nem exigidos. Não use `-march=native`; mantenha o build compatível com o baseline x86-64. A configuração atual não oferece suporte a CPUs x86 de 32 bits.

## Estado do projeto

- [x] Estrutura C++/CMake e CI Windows/Linux.
- [x] Estado arquitetural inicial de uma thread (GPRs, PC, LR, CTR, XER, CR, MSR e FPSCR).
- [x] Formato versionado de save state para estado da CPU Xenon e RAM, com verificação de integridade e limite de memória.
- [x] Gerenciador inicial de 10 slots independentes para salvar, carregar, verificar e apagar snapshots CPU/RAM; testes adicionados ao CI.
- [x] Interpretador PowerPC inicial e testes unitários.
- [x] B/BC, ADDIC/ADDIC., ADDE/ADDME/SUBFE/SUBFME/ADDZE/SUBFZE (código e testes adicionados), acessos de byte/palavra/meia-palavra (LBZ/LWZ/LHZ/LHA e STB/STW/STH), formas update e testes de buffer big-endian adicionados.
- [ ] Validação final do CI para o commit mais recente em Windows e Linux; geração e inspeção do ZIP portátil.
- [ ] Cobertura mais ampla da ISA Xenon, branches, exceções e semântica de overflow/carry.
- [x] Leitor ISO inicial: validação do descritor XDVDFS e leitura sob demanda de setores (testes adicionados; aguardando CI).
- [x] Parser experimental da árvore XDVDFS da raiz e busca de `default.xex` (testes enviados ao CI).
- [ ] Navegação recursiva XDVDFS e integração ISO/XEX ao carregador.
- [x] Biblioteca de capas com cache local, downloader HTTPS WinHTTP no Windows e validação JPEG.
- [x] Estrutura de pastas roms/covers/saves/configs, criação idempotente e raiz portátil ao lado do executável (teste dedicado adicionado).
- [ ] Integrar catálogo/provedor de metadados, identificar jogos automaticamente e exibir capas reais na biblioteca gráfica.
- [x] Dashboard Win32 inspirado no Aurora com telas distintas e seleção de menu por teclado/mouse.
- [x] Scanner inicial de diretórios GOD/XBLA/DLC/Title Update e candidatos a pacotes STFS por extensão (identificação de caminho, sem validação interna).
- [x] Parser inicial do cabeçalho/descritor STFS, metadados básicos e rejeição de SVOD não suportado (testes sintéticos adicionados; CI ainda não validado).
- [ ] Verificação RSA/assinaturas e SHA-1, parser da árvore STFS, leitura de cadeias de blocos e extração/montagem de pacotes; integração segura de GOD/XBLA/DLC/TU ao carregador.
- [ ] Conectar a biblioteca a jogos indexados e capas reais; ligar importação ISO/XEX/GOD e gerenciamento dos save states; adicionar busca, filtros e controle Xbox 360.
- [ ] Integração dos slots com pausa/retomada do loop de emulação e interface gráfica.
- [ ] Snapshot completo do sistema: GPU, áudio, dispositivos, timers, kernel e estado de todos os threads.
- [x] Memória física experimental com permissões de página e fetch de instrução integrado ao núcleo Xenon (testes adicionados; CI pendente).
- [ ] MMU, memória virtual, BAT/TLB e proteção dos acessos de dados.
- [x] Parser inicial do cabeçalho XEX2 e diretório opcional (testes adicionados; CI pendente).
- [ ] Descriptografia/descompressão XEX, mapeamento PE e carregamento completo.
- [ ] GPU Xenos, áudio, kernel/HAL, exceções/interrupções e execução multicore.
- [ ] Execução de jogos.

## Plano de versões

| Versão | Marco |
|---|---|
| v0.1b | Estrutura e baseline Win32 x86 sem SSE2 |
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
