# WX360

**Windows Xbox 360 Emulator** — emulador experimental do Xbox 360 para Windows x64, com foco em computadores modestos.

> **Versão em desenvolvimento:** v0.4b. O projeto ainda não executa jogos comerciais.
>
> **CI validada em 23/11/2026:** build Release e 25/25 testes aprovados no Windows e Linux. O pipeline gera um vetor LZX positivo com encoder independente e testa a descompressão. O ZIP portátil Windows foi publicado como artefato do GitHub Actions; isso é um pacote de CI, não uma release estável.

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
- Rejeita magic desconhecido, cabeçalho truncado, tamanho declarado fora dos limites, descritor inválido e contagens inconsistentes; agora lê também campos iniciais do descritor SVOD.
- Testes sintéticos verificam extração básica, magic inválido e recusa segura de SVOD.
- A enumeração da tabela de diretórios lê entradas de 0x40 bytes: nomes, flags, blocos válidos/alocados, bloco inicial, índice do diretório pai e tamanho.
- O mapeamento de blocos e a cadeia da tabela usam os níveis de hash STFS (170 blocos por tabela L0) para pacotes read-only; formatos STFS graváveis continuam recusados.
- A interface mostra a quantidade de entradas quando a tabela pode ser lida.
- Testes sintéticos cobrem uma entrada `default.xex` e uma tabela encadeada de dois blocos.
- **Limite:** não verifica RSA/assinaturas nem SHA-1. Cabeçalho/tabela estruturalmente válidos não comprovam autenticidade ou integridade.

## Biblioteca Xbox 360 Content/GOD (identificação inicial)

- O scanner reconhece diretórios de Games on Demand pelo caminho de conteúdo `00007000` quando existe um arquivo/entrada `Data0000`.
- Identifica diretórios de conteúdo pelos tipos `000D0000` (XBLA), `00000002` (DLC) e `000B0000` (Title Update).
- Reconhece candidatos a pacotes STFS pelas extensões `.con`, `.live` e `.pirs` e tenta analisar o cabeçalho; quando válido, a interface mostra Title ID e nome do título.
- O modo portátil cria `roms/GOD` e `roms/Content/0000000000000000`; a tela Arquivos apresenta os itens detectados.
- Testes dedicados cobrem cada tipo de pasta, as três extensões STFS e diretório inexistente.
- **Limite importante:** o scanner reconhece candidatos por nomes/caminhos; a montagem GOD agora é feita por uma camada SVOD/GDF separada e ainda experimental. Não há validação RSA/SHA-1 nem execução comercial comprovada. Os arquivos sintéticos de teste não substituem pacotes reais.

## Hierarquia STFS, extração recursiva e inspeção SVOD

- Reconstrução de caminhos relativos de entradas STFS a partir dos índices de diretório pai, com detecção de ciclos, pais inválidos e nomes com separadores.
- Extração recursiva inicial de arquivos e diretórios STFS, com limite global configurável (2 GiB por padrão) e limite individual já aplicado pelo leitor de blocos.
- Descoberta ordenada de fragmentos `Data000x` em pastas GOD.
- Leitura preliminar do descritor SVOD e inspeção dos marcadores Enhanced GDF, XSF e single-file.
- A leitura GDF percorre a árvore binária de diretórios (nós esquerdo/direito), subdiretórios e nomes, com limites de profundidade e quantidade de entradas.
- O mapeamento SVOD considera blocos de 0x800 bytes, tabelas L0/L1, fragmentos de 0x14388 blocos e a transição para o próximo fragmento.
- A extração GOD reconstrói arquivos e pastas, com limites de tamanho e rejeição de traversal e symlinks nos caminhos de saída.
- **Limitações:** layouts incomuns ainda podem ser recusados; a leitura de nomes que atravessem limites físicos de bloco precisa de validação adicional. Não verifica hashes SHA-1/RSA e não substitui testes com pacotes GOD reais.

## Leitura de dados e extração STFS (etapa inicial)

- Leitor de arquivos STFS que segue cadeias de blocos de dados para arquivos marcados como contíguos ou encadeados, respeitando os limites de tamanho declarados e o tamanho real do pacote.
- Extração individual para uma pasta de destino, com rejeição de nomes contendo separadores de caminho, dois-pontos, `.` e `..`; limite conservador de 256 MiB por arquivo.
- Helper para localizar `default.xex` na tabela raiz de um pacote STFS e extraí-lo para uma pasta escolhida.
- Helper de descoberta de `default.xex` em uma pasta GOD previamente extraída.
- **Limitações:** não há validação SHA-1/RSA; a extração STFS continua sujeita à validação com pacotes reais. O leitor SVOD/GDF é experimental e não autentica fragmentos.

## Leitor GDF/SVOD e integração inicial XEX → Xenon

- O descritor SVOD é interpretado com o bit Enhanced GDF e o número/bloco inicial de dados; para pacotes fragmentados, o leitor mapeia os arquivos ordenados em `.data` conforme `data_file_count`.
- Reconhece os layouts Enhanced GDF, XSF e single-file pelos marcadores `MICROSOFT*XBOX*MEDIA` e `XSF`.
- Percorre a árvore GDF com nós esquerdo/direito, diretórios filhos e nomes de arquivo; limita profundidade e quantidade de entradas.
- Mapeia blocos de 0x800 bytes com tabelas de hash L0/L1, fragmentos e rollover; reconstrói diretórios e extrai arquivos GOD.
- `ContentLoader` oferece extração GOD/SVOD e procura `default.xex` após a extração.
- `XexImageLoader` inspeciona o cabeçalho XEX2 usando tamanho real do arquivo e gera um plano de carregamento com entry point virtual.
- `XenonCore::start_xex` só transfere o PC para um entry point físico depois que o plano indicar descriptografia, descompressão, mapeamento PE e importações do kernel concluídos, e a RAM permitir fetch executável.
- Teste de integração sintético XEX2: cria um arquivo XEX2 sem criptografia/compactação, valida cabeçalho e file-format info, inspeciona entry point, entrega o PE já transformado ao mapeador, inicia o XenonCore e executa a primeira instrução (`addi r3,r0,42`), verificando `r3 = 42` e avanço do PC.
- Esse teste comprova o caminho sintético sem transformações nem imports reais; não comprova descriptografia retail/devkit, Normal/LZX integrado a um XEX real, relocations/imports de kernel, autenticação RSA ou execução de jogos comerciais. O plano XEX ainda não é produzido automaticamente como pronto para jogos reais. A leitura de fragmentos reais e layouts complexos requer validação adicional.

## XEX2 Normal — enquadramento e IV

- Referência de implementação Xenia consultada: o payload XEX2 Normal é descriptografado com AES-CBC usando chaining value inicial de 16 bytes zerados. Foi adicionada a API `aes128_xex_cbc_decrypt` e o XEX loader agora oferece uma entrada que usa esse IV, sem exigir que o chamador o adivinhe.
- O cabeçalho de formato agora lê `window_size` e o primeiro descritor de bloco Normal.
- Foi adicionado parser limitado dos blocos Normal: verifica SHA-1 do bloco, lê o descritor seguinte embutido e remonta chunks com prefixos de tamanho big-endian e terminador zero. Há um teste sintético de reconstrução e rejeição de bloco adulterado.
- O decoder LZX foi integrado ao caminho de chunks Normal. Um vetor positivo gerado independentemente por `github.com/abemedia/go-cabinet/lzx` é descompactado e comparado byte a byte com o conteúdo original no CI Windows/Linux. A aprovação do vetor não substitui validação com XEX retail/devkit real.
- Nenhum XEX comercial/retail/devkit real foi fornecido ou descriptografado. A chave de sessão e a cadeia AES ainda só têm vetores de teste AES genéricos, não um caso XEX ponta a ponta.
- A validação SHA-1 das páginas continua disponível após reconstrução, mas ainda não foi integrada ao pipeline de transformação normal de ponta a ponta.
- A resolução de ordinais/thunks ainda não interpreta o fluxo de registros import_table do executável, e não há catálogo real completo de exports xboxkrnl/xam. Não inventar endereços de kernel.
- O primeiro boot sintético XEX2 até uma instrução Xenon foi validado em CI. Boot de XEX comercial continua pendente.

## AES-128 XEX — integração parcial

- Adicionada implementação AES-128 portátil tiny-AES-C (material em domínio público, com cópia de UNLICENSE em `third_party/tiny_aes`).
- CMake agora habilita C além de C++ e compila a biblioteca AES como dependência estática do núcleo.
- API C++ `crypto/aes128.hpp`: decrypt de bloco ECB, decrypt CBC com IV fornecido pelo chamador e unwrap do campo de chave de sessão usando chave retail/devkit selecionável.
- O XEX loader expõe `decrypt_normal_payload`, conectando a extração da chave cifrada da Security Info, unwrap ECB e decrypt CBC.
- Testes NIST de AES-128 ECB/CBC e do primitive de unwrap estão registrados no CTest; a execução dos 25 testes foi aprovada no Windows e Linux em 23/11/2026.
- Limitação importante: o IV é passado explicitamente; a extração/derivação correta do IV a partir do formato XEX2 e o fluxo completo de descriptografia ainda precisam ser validados com amostras legítimas retail/devkit. A chave de sessão só foi testada com vetor AES conhecido, não com XEX retail/devkit real.
- O carregador ainda não marca automaticamente `decryption_complete` nem integra o resultado à descompressão/PE; isso evita liberar uma imagem como válida antes da validação SHA-1.
- A descompressão Normal/LZX possui decoder e vetor positivo independente aprovado; ainda faltam validação com XEX retail/devkit real, verificação RSA, SHA-1 ponta a ponta, parser completo de ordinais, catálogo real de exports xboxkrnl/xam e boot de XEX comercial pelo Xenon.

## XEX2: segurança, SHA-1 e tabela de import libraries (etapa inicial)

- Adicionado SHA-1 portátil com teste conhecido para a mensagem `abc`.
- Adicionado parser estrutural de Security Info XEX2: tamanhos, image size, flags, load address, assinatura RSA em bytes, digests SHA-1, chave AES cifrada, export-table pointer e page descriptors.
- A rotina SHA-1 agora valida os digests dos blocos descritos pelos page descriptors contra uma imagem reconstruída, exigindo cobertura/tamanho consistentes; há teste sintético positivo e teste de corrupção. Isso ainda não valida a assinatura RSA nem todos os digests de cabeçalho/import table, e depende de uma imagem já descriptografada/descomprimida.
- O optional header `0x000103FF` agora é identificado. O parser de import libraries lê a string table, registros por biblioteca, versões, contagens e lista de valores de thunk/índices, com limites de tamanho e validação de strings.
- O parser da tabela de bibliotecas ainda não interpreta completamente os descriptors individuais de importação/ordinais nem aplica todos os imports aos thunks PE; o catálogo real de exports do xboxkrnl/xam também não existe.
- AES-128-ECB/CBC e decoder Normal/LZX já estão implementados e cobertos por testes automatizados; o vetor positivo LZX independente e a suíte completa passaram em Windows/Linux. A aplicação correta da cadeia criptográfica a amostras XEX retail/devkit e a validação RSA continuam pendentes.
- O núcleo Xenon já possui handoff protegido e execução de uma instrução via `step()` em memória executável; isso é um teste de instrução sintética, não inicialização de um XEX comercial.

## Avanço incremental do leitor GOD/SVOD e XEX2

- Fragmentos de diretórios SVOD com mais de um arquivo são agora aceitos somente quando seus nomes seguem `Data0000` ... `DataNNNN`, ordenados pelo índice numérico e sem lacunas/duplicatas. Nomes lexicográficos ou arquivos extras são rejeitados.
- O teste SVOD verifica o parser de nomes de fragmentos e mantém o fixture sintético single-file. Isso **não** substitui a validação com um conjunto GOD real de vários fragmentos.
- O parser XEX2 identifica o optional header `0x000003FF` (file format info), lê encryption/compression type e image size da security info, valida a tabela de blocos Basic e exige que a soma de data/zero bytes corresponda ao tamanho da imagem.
- Foi implementada descompressão XEX Basic **somente quando encryption type = NONE**: copia os trechos de dados e preenche os trechos zero. Entrada criptografada é recusada explicitamente.
- XEX AES-CBC retail/devkit, verificação RSA/SHA-1, compressão Normal/LZX, parsing da tabela real de import libraries e catálogo real de exports Xbox kernel continuam pendentes.
- O resolver de imports agora valida permissões de escrita dos thunks antes de patchar a memória, sem escrever diretamente no buffer bruto.
- Um teste sintético cobre metadados XEX Basic, expansão dos blocos e recusa de payload criptografado.

## Mapeador PE XEX e infraestrutura de importações Xenon

- Foi adicionado um mapeador PE32 limitado ao formato PowerPC big-endian/Xbox (machine 0x01F2, subsystem Xbox), com validação de cabeçalhos, limites de seções, cópia dos dados raw e permissões de página.
- Relocations PE32 do tipo HIGHLOW são aplicadas com delta de endereço; tipos não suportados são recusados.
- O entry point XEX pode selecionar o RVA dentro da imagem PE e é convertido para o endereço físico mapeado.
- `XexImageLoader::map_transformed_pe` só marca descriptografia/descompressão como concluídas quando o chamador fornece explicitamente um buffer PE já transformado. Esta função não descriptografa nem descomprime o contêiner XEX.
- Foi adicionada infraestrutura de resolução de imports por biblioteca/ordinal, com registro explícito de exports, comparação de nomes sem diferenciar maiúsculas e escrita big-endian dos thunks. Imports sem export registrado são recusados.
- Testes sintéticos cobrem mapeamento de seção executável, permissões de página, registro de exports e recusa de ordinal ausente.
- **Limitações críticas:** ainda não existe parser do cabeçalho de segurança XEX para extrair/validar a chave AES, RSA retail/devkit, descriptografia AES-CBC, descompressão BASIC/LZX, parser do diretório de importações XEX nem tabela real de exports de xboxkrnl/xam. Portanto, a resolução atual é infraestrutura genérica e não resolve por si só imports de jogos comerciais. O primeiro boot comercial ainda não foi alcançado.

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
- [x] Leitura inicial de árvore GDF/SVOD, mapeamento de fragmentos e extração experimental GOD (testes sintéticos adicionados; execução do CI ainda pendente).
- [x] Plano XEX2 e handoff protegido para o núcleo Xenon após mapeamento (não inclui ainda o mapeador PE/decrypt/decompress/imports).
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
- [x] Leitura inicial das cadeias de blocos STFS e extração segura de arquivo individual (testes adicionados; execução ainda não confirmada).\n- [x] Reconstrução inicial da hierarquia STFS e extração recursiva limitada (testes adicionados; execução ainda não confirmada).\n- [x] Inspeção inicial do descritor/layout SVOD e descoberta dos fragmentos GOD Data000x.\n- [x] Percurso inicial da árvore GDF/SVOD, montagem de blocos e extração recursiva experimental (sem validação com fragmentos GOD reais; CI pendente).
- [x] Ordenação estrita dos nomes Data0000...DataNNNN e rejeição de lacunas/duplicatas (teste sintético).
- [ ] Validar mapeamento SVOD com pacote GOD real e arquivos Data000x.
- [ ] Conectar a biblioteca a jogos indexados e capas reais; ligar importação ISO/XEX/GOD e gerenciamento dos save states; adicionar busca, filtros e controle Xbox 360.
- [ ] Integração dos slots com pausa/retomada do loop de emulação e interface gráfica.
- [ ] Snapshot completo do sistema: GPU, áudio, dispositivos, timers, kernel e estado de todos os threads.
- [x] Memória física experimental com permissões de página e fetch de instrução integrado ao núcleo Xenon (testes adicionados; CI pendente).
- [ ] MMU, memória virtual, BAT/TLB e proteção dos acessos de dados.
- [x] Parser inicial do cabeçalho XEX2 e diretório opcional (testes adicionados; CI pendente).
- [x] Parser dos metadados de compressão XEX2 e descompressão Basic sem criptografia (fixture sintético; CI pendente).
- [ ] AES-128-CBC com derivação de session key retail/devkit, descompressão Normal/LZX e verificação SHA-1/RSA ponta a ponta.
- [x] Mapeador PE32 PowerPC inicial, seções, permissões e relocations HIGHLOW (testes sintéticos adicionados; CI pendente).
- [x] Infraestrutura de resolução de imports por biblioteca/ordinal (sem catálogo real de exports xboxkrnl/xam; CI pendente).
- [x] Identificação do optional header 0x000103FF e parser inicial de import libraries (sem resolução completa dos ordinais/thunks; CI pendente).\n- [ ] Integração dos imports/ordinais com catálogo real de exports xboxkrnl/xam.
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
