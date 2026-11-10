#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#define ARQUIVO_CLIENTES "customer.txt"
#define ARQUIVO_COMANDOS "commands.txt"
#define ARQUIVO_RESULTADO "result.txt"

#define LARGURA_MIN_COL1 10
#define LARGURA_MIN_COL2 12

typedef struct {
    int num_clientes; // Numero de clientes (linhas) 
    int num_recursos; // Numero de recursos (colunas)
    int *disponivel; //Instancias livres 
    int *maximo;  //Demanda maxima [n*m] 
    int *alocacao;  
    int *necessidade; //maximo - alocacao [n*m] 
    int *trabalho;  //Vetor auxiliar do teste de seguranca 
    char *finalizado;  //Vetor auxiliar do teste de seguranca 
} banco_t;

typedef enum { TIPO_RQ, TIPO_RL, TIPO_ESTADO } tipo_comando_t;

typedef struct {
    tipo_comando_t tipo;
    int cliente;
    int *quantidade;
    long linha;
} comando_t;

static banco_t banco;
static size_t capacidade_linhas;
static comando_t *comandos;
static size_t num_comandos, capacidade_comandos;
static char **tokens;
static char *buffer_linha;
static size_t capacidade_linha;
static FILE *arquivo_entrada;
static FILE *arquivo_saida;

//fecha os arquivos abertos e aplica free() em todos os ponteiros e vetores
static void limpa_recursos(void) {

    if (arquivo_entrada){
        fclose(arquivo_entrada);
    } 

    if (arquivo_saida){
        fclose(arquivo_saida);
    } 

    //garante que a memoria seja devolvida
    free(buffer_linha);
    free(tokens);
    free(banco.disponivel);
    free(banco.maximo);
    free(banco.alocacao);
    free(banco.necessidade);
    free(banco.trabalho);
    free(banco.finalizado);

    for (size_t i = 0; i < num_comandos; i++){
         free(comandos[i].quantidade);
    }
    
    free(comandos);
}

static void morrer(const char *formato, ...) {

    va_list ap;
    fputs("Erro: ", stderr);

    va_start(ap, formato);
    vfprintf(stderr, formato, ap);

    va_end(ap);
    fputc('\n', stderr);
    exit(EXIT_FAILURE);
}

//Aloca memória dinamicamente e  caso a memória do sistema acabe, chama morrer() automaticamente.
static void *aloca_zero(size_t quantidade, size_t tamanho) {
    void *p = calloc(quantidade, tamanho);

    if (!p){
        morrer("memoria insuficiente");
    } 

    return p;
}

//valida se a string contém apenas dígitos e se o valor não ultrapassa o limite máximo
static int converte_nao_negativo(const char *s, size_t tam, int *saida) {

    long long v = 0;
    if (tam == 0){
        return -1;
    }

    for (size_t i = 0; i < tam; i++) {
        if (s[i] < '0' || s[i] > '9'){
            return -1;
        } 
        v = v * 10 + (s[i] - '0');
        if (v > INT_MAX){
            return -1;
        }
    }
    *saida = (int)v;
    return 0;
}

//Remove espaços em branco, tabulações ou quebras de linha no início e no final de uma string.
static char *remove_espacos(char *s, size_t *tam) {

    size_t l;
    while(*s && isspace((unsigned char)*s)){
        s++;
    }
    l = strlen(s);
    while(l > 0 && isspace((unsigned char)s[l - 1])){
         l--;
    } 

    s[l] = '\0';
    *tam = l;
    return s;
}

static int *obter_linha(int *base, int i) {

    return base + (size_t)i * (size_t)banco.num_recursos;
}

//Leitura dos Arquivos de Entrada (customer.txt e commands.txt)

static void abrir_entrada(const char *caminho) {
    arquivo_entrada = fopen(caminho, "r");
    
    if (!arquivo_entrada){

        morrer("nao foi possivel abrir '%s': %s", caminho, strerror(errno));
    }
}

static void fechar_entrada(void) {
    // Verifica se existe um arquivo aberto antes de tentar fechar
    if (arquivo_entrada) {

        fclose(arquivo_entrada); 
        arquivo_entrada = NULL; 
    }
}

//Lê a próxima linha do arquivo, incrementa o contador de linhas e remove caracteres indesejados
static ssize_t proxima_linha(const char *caminho, long *num_linha) {
    ssize_t tam = getline(&buffer_linha, &capacidade_linha, arquivo_entrada);
    
    // Se o retorno for -1,chegou ao fim do arquivo ou ocorreu um erro de leitura
    if (tam == -1) {
        if (ferror(arquivo_entrada)){
            morrer("falha ao ler '%s': %s", caminho, strerror(errno));
        }
            
        return -1;
    }
    
    (*num_linha)++;
    
    // Procura por bytes nulos no meio do texto 
    if (memchr(buffer_linha, '\0', (size_t)tam)){
        morrer("'%s' linha %ld: byte nulo encontrado", caminho, *num_linha);
    }
        
    // Remove quebras de linha no estilo Linux e Windows do final do buffer
    while (tam > 0 && (buffer_linha[tam - 1] == '\n' || buffer_linha[tam - 1] == '\r')){
        buffer_linha[--tam] = '\0';
    }

        
    // Se for a primeira linha, remove o cabeçalho invisível gerado pelo Windows
    if (*num_linha == 1 && tam >= 3 && memcmp(buffer_linha, "\xEF\xBB\xBF", 3) == 0) {
        memmove(buffer_linha, buffer_linha + 3, (size_t)tam - 3 + 1);
        tam -= 3;
    }
    
    return tam;
}


//Lê o arquivo customer.txt, valida a matriz de recursos máximos e aloca a memória para os clientes.                        
static void ler_clientes(void){
    const int num_recursos = banco.num_recursos; // Guarda a quantidade de recursos especificada na CLI
    long num_linha = 0;
    long linha_vazia = 0;

    abrir_entrada(ARQUIVO_CLIENTES); 
    
    // Lê o arquivo linha por linha até o fim
    while (proxima_linha(ARQUIVO_CLIENTES, &num_linha) != -1) {
        size_t tam_str, campos = 1;
        char *s = remove_espacos(buffer_linha, &tam_str), *p, *fim;
        int *destino;

        // Se a linha for vazia
        if (tam_str == 0) {
            if (!linha_vazia) linha_vazia = num_linha; 
            continue;
        }
        
        //não permite linhas de clientes depois de uma linha vazia no meio do arquivo
        if (linha_vazia){
            morrer("%s: linha %ld esta vazia, mas existem clientes depois dela", ARQUIVO_CLIENTES, linha_vazia);
        }
        // Conta quantas vírgulas existem para saber a quantidade de colunas/campos
        for (p = s; *p; p++){
                 if (*p == ','){
                     campos++;
                 } 
        }
          
            
        // Valida se o número de valores na linha é exatamente igual ao número de recursos informados no ./banker
        if (campos != (size_t)num_recursos){
            morrer("%s linha %ld: %zu coluna(s) encontrada(s), mas foram informados %d recurso(s)", ARQUIVO_CLIENTES, num_linha, campos,num_recursos);
        }

        // Se a quantidade de clientes ultrapassar a capacidade atual da matriz, expande com realloc
        if ((size_t)banco.num_clientes + 1 > capacidade_linhas) {

            size_t nova_cap;
            if (capacidade_linhas != 0) {
                nova_cap = capacidade_linhas * 2;
            } else {
                 nova_cap = 16;
            }
            int *tmp = realloc(banco.maximo, nova_cap * (size_t)num_recursos * sizeof(int));

            if (!tmp){
                morrer("memoria insuficiente");
            }

            banco.maximo = tmp;
            capacidade_linhas = nova_cap;
        }
        
        destino = obter_linha(banco.maximo, banco.num_clientes);

        // Corta os valores separados por vírgula e converte para inteiros
        p = s;
        for (int j = 0; j < num_recursos; j++) {
            fim = strchr(p, ',');
            if (!fim){
                 fim = p + strlen(p); // Se for o último valor da linha
            } 
            
            // Valida se o valor é um número válido e não negativo
            if (converte_nao_negativo(p, (size_t)(fim - p), &destino[j]) != 0){
                 morrer("%s linha %ld: valor invalido na coluna %d", ARQUIVO_CLIENTES, num_linha, j + 1);
            }
    
            p = fim + 1; 
        }
        banco.num_clientes++; 
    }
    fechar_entrada();
    
    // Se nenhuma linha válida foi lida, lança erro
    if (banco.num_clientes == 0){
        morrer("%s esta vazio", ARQUIVO_CLIENTES);
    }
}
                                
//Guarda um comando lido (RQ, RL ou *) na lista dinâmica.  
static void adicionar_comando(tipo_comando_t tipo, int cliente, int *quantidade, long linha) {
    // Se a lista de comandos estiver cheia, expande o vetor dinamicamente
    if (num_comandos == capacidade_comandos) {

        size_t nova_cap;
        if (capacidade_comandos != 0) {
            nova_cap = capacidade_comandos * 2;
        } else {
            nova_cap = 16;
        }

        if (nova_cap > SIZE_MAX / sizeof(*comandos)) {
            free(quantidade); // Libera o vetor de recursos para evitar memory leak
            morrer("quantidade de comandos ultrapassa o limite de memoria");
        }

        comando_t *tmp = realloc(comandos, nova_cap * sizeof(*comandos));
        if (!tmp) {
            free(quantidade); // Evita vazamento de memória se o realloc falhar
            morrer("memoria insuficiente");
        }
        comandos = tmp;
        capacidade_comandos = nova_cap;
    }
    
    // Salva os dados do comando na posição atual da lista
    comandos[num_comandos].tipo = tipo;
    comandos[num_comandos].cliente = cliente;
    comandos[num_comandos].quantidade = quantidade;
    comandos[num_comandos].linha = linha;
    num_comandos++; // Incrementa o total de comandos armazenados
}

//Lê o arquivo commands.txt, identifica os tipos decomando (RQ, RL ou *), valida os parâmetros e insere na fila.      
static void ler_comandos(void) {
    const int num_recursos = banco.num_recursos;
    const size_t cap_tokens = (size_t)num_recursos + 2; // Nome do comando + ID do cliente + m recursos
    long num_linha = 0;

    tokens = aloca_zero(cap_tokens, sizeof(*tokens)); 
    abrir_entrada(ARQUIVO_COMANDOS); 
    
    // Lê o arquivo linha a linha
    while (proxima_linha(ARQUIVO_COMANDOS, &num_linha) != -1) {
        size_t tam_str, num_tokens = 0;
        char *s = remove_espacos(buffer_linha, &tam_str), *p;

        if (tam_str == 0){
            continue; 
        }

        // Divide a linha em tokenseparadas por espaços ou tabulações
        for (p = s; *p; ) {
            char *inicio;

            while (*p && isspace((unsigned char)*p)){
                 p++; //pula espaços
            }
            if (!*p){
                break;
            }

            inicio = p;
            while (*p && !isspace((unsigned char)*p)){
                p++; //encontra o fim da palavra
            } 
            if (*p){
                *p++ = '\0'; 
            } 
            if (num_tokens < cap_tokens){
                tokens[num_tokens] = inicio;
            } 
            num_tokens++;
        }

        // Se o comando for '*' (imprimir tabela de estado)
        if (strcmp(tokens[0], "*") == 0) {
            if (num_tokens != 1){
                morrer("%s linha %ld: '*' nao recebe argumentos", ARQUIVO_COMANDOS, num_linha);
            }
            adicionar_comando(TIPO_ESTADO, -1, NULL, num_linha);
        } 
        // Se o comando for 'RQ' (Requisição) ou 'RL' (Liberação)
        else if (strcmp(tokens[0], "RQ") == 0 || strcmp(tokens[0], "RL") == 0) {
           
            tipo_comando_t tipo;
            if (tokens[0][1] == 'Q') {
                tipo = TIPO_RQ;
            } else {
                tipo = TIPO_RL;
            }

            int cliente, *quantidade;
            // Valida se o número total de argumentos da linha é igual a (m recursos + cliente + nome_do_comando)
            if (num_tokens != (size_t)num_recursos + 2){
                morrer("%s linha %ld: argumentos incompativeis em %s", ARQUIVO_COMANDOS, num_linha, tokens[0]);
            }
            // Converte e valida o ID do cliente
            if (converte_nao_negativo(tokens[1], strlen(tokens[1]), &cliente) != 0){
                morrer("%s linha %ld: cliente invalido", ARQUIVO_COMANDOS, num_linha);
            }

            // Garante que o cliente solicitado existe no customer.txt
            if (cliente >= banco.num_clientes){
                morrer("%s linha %ld: cliente %d nao existe", ARQUIVO_COMANDOS, num_linha, cliente);
            }
                
            //aloca o vetor com os valores das quantidades de recursos do comando
            quantidade = aloca_zero((size_t)num_recursos, sizeof(int));
            for (int j = 0; j < num_recursos; j++) {
                if (converte_nao_negativo(tokens[2 + j], strlen(tokens[2 + j]), &quantidade[j]) != 0) {
                    free(quantidade);
                    morrer("%s linha %ld: valor de recurso invalido", ARQUIVO_COMANDOS, num_linha);
                }
            }
            // Guarda o comando válido na lista
            adicionar_comando(tipo, cliente, quantidade, num_linha);
        } 
        // Erro caso encontre qualquer comando diferente de RQ, RL ou *
        else {
            morrer("%s linha %ld: comando desconhecido '%s'", ARQUIVO_COMANDOS, num_linha, tokens[0]);
        }
    }
    fechar_entrada(); 
}

//MAIN TEMPORARIO
int main(int argc, char *argv[]) {
    atexit(limpa_recursos);

    if (argc < 2) {
        morrer("Uso: %s <r1> <r2> ... <rn>", argv[0]);
    }

    // Lê a quantidade de recursos informada via CLI
    banco.num_recursos = argc - 1;
    banco.disponivel = aloca_zero((size_t)banco.num_recursos, sizeof(int));
    for (int j = 0; j < banco.num_recursos; j++) {
        if (converte_nao_negativo(argv[j + 1], strlen(argv[j + 1]), &banco.disponivel[j]) != 0) {
            morrer("Recurso %d invalido", j + 1);
        }
    }

    printf("=== TESTANDO PARTE 1 E PARTE 2 ===\n");
    
    printf("[1] Lendo customer.txt...\n");
    ler_clientes();
    printf("    -> Sucesso! %d clientes lidos.\n", banco.num_clientes);

    printf("[2] Lendo commands.txt...\n");
    ler_comandos();
    printf("    -> Sucesso! %zu comandos lidos.\n", num_comandos);

    printf("\n--- Resumo dos Comandos Reconhecidos ---\n");
    for (size_t i = 0; i < num_comandos; i++) {
        if (comandos[i].tipo == TIPO_ESTADO) {
            printf("Linha %ld: Comando '*'\n", comandos[i].linha);
        } else {
            printf("Linha %ld: Comando %s | Cliente: %d | Recursos:", 
                   comandos[i].linha,
                   comandos[i].tipo == TIPO_RQ ? "RQ" : "RL", 
                   comandos[i].cliente);
            for (int j = 0; j < banco.num_recursos; j++) {
                printf(" %d", comandos[i].quantidade[j]);
            }
            printf("\n");
        }
    }

    return 0;
}