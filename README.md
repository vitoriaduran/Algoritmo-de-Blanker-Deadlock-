# Algoritmo de Banker (evitação de deadlock) em C
 
Implementação do algoritmo de Banker (Silberschatz et al., *Operating System Concepts*, 10ª ed., p. P-45). O número de recursos vem da linha de comando e o número de clientes vem de `customer.txt`. Os comandos são lidos de `commands.txt` e o resultado é gravado em `result.txt`.
 
Aluna: Vitória Gabrielle Janeiro Duran (vgjda2)
 
## Arquivos
 
| Arquivo | Função |
|---|---|
| `banker.c` | código-fonte completo |
| `Makefile` | `make` gera o executável `banker`; `make clean` remove os arquivos compilados |
| `evidencias.log` | gravação (`script -a`) das compilações e execuções, com casos válidos, limite e inválidos |
| `vgjda2.pdf` | relatório da entrega |
 
## Compilar e executar
 
```bash
make clean
make
./banker 10 5 7
cat result.txt
```
 
Cada argumento é o número de instâncias de um tipo de recurso (`10 5 7` = 3 recursos). O programa lê `customer.txt` e `commands.txt` **do diretório onde é executado** e cria `result.txt` nele. Funciona em Linux, Unix e macOS com um compilador C11.
 
## Formato dos arquivos de entrada
 
**customer.txt** — uma linha por cliente com a demanda máxima de cada recurso, separada por vírgula e sem espaços:
 
```
7,5,3
3,2,2
```
 
O número de linhas é o número de clientes. O número de colunas deve ser igual ao número de argumentos.
 
**commands.txt** — um comando por linha:
 
| Comando | Significado |
|---|---|
| `RQ c r0 r1 ...` | cliente `c` requisita `r0 r1 ...` instâncias |
| `RL c r0 r1 ...` | cliente `c` libera `r0 r1 ...` instâncias |
| `*` | imprime as matrizes MAXIMUM, ALLOCATION e NEED e o vetor AVAILABLE |
 
## Como cada `RQ` é decidido
 
1. Se o pedido é maior que a necessidade (`NEED`) do cliente, é negado: `exceed its maximum allocation`.
2. Se o pedido é maior que o disponível (`AVAILABLE`), responde `The resources ... are not enough ...`.
3. Caso contrário, a alocação é simulada e o algoritmo de segurança roda. Se existe uma sequência segura, o pedido é concedido (`Allocate to customer ...`). Se não, a simulação é desfeita e o pedido é negado: `result in an unsafe state`.
## Tratamento de erros
 
Argumentos, `customer.txt` e `commands.txt` são validados **antes** de qualquer comando ser executado. Se algo estiver errado, o programa escreve `Erro: ...` (com arquivo e linha) em `stderr`, remove qualquer `result.txt` antigo e termina com status 1, sem falha de segmentação.
 
Casos tratados:
- número de argumentos diferente do número de colunas de `customer.txt` ou de valores em `RQ`/`RL`;
- nenhum argumento, ou argumento não numérico, negativo ou grande demais;
- arquivo inexistente, sem permissão, diretório ou binário;
- `customer.txt` vazio, com linha em branco no meio, valor inválido ou número de colunas diferente entre linhas;
- comando desconhecido (somente `RQ`, `RL` e `*`), com campos a mais ou a menos, cliente inexistente ou valor inválido.
Decisões onde o enunciado não define o comportamento:
- `RL` maior que o alocado não é aplicado e gera `The customer c release ... was denied because exceed its allocation`;
- linhas em branco em `commands.txt` são ignoradas;
- finais de linha CRLF e BOM UTF-8 são aceitos.
## Testes
 
Os casos de teste válidos, limite e inválidos estão registrados em `evidencias.log` (gravado com `script -a`).
 
## Relatório Técnico
O relatório completo de desenvolvimento, testes e evidências está disponível em [`vgjda2.pdf`](./vgjda2.pdf).
