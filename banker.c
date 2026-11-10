/*
 * banker.c - Algoritmo de Banker (evitacao de deadlock)
 *
 * Autora: Vitoria Gabrielle Janeiro Duran (vgjda2)
 */

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

    return base + (size_t)i * (size_t)banco.m;
}