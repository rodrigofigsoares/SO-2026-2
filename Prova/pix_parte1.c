/*
 * INF0336 - A01 - Parte 1: sincronismo do Sistema PIX (modelo N:1)
 *
 * N remetentes enviam dinheiro AO MESMO TEMPO para UMA unica conta de destino.
 * As requisicoes sao atendidas por uma thread pool de P workers.
 * NAO ha nenhuma protecao: o objetivo e' mostrar a condicao de corrida.
 *
 *   RECURSO COMPARTILHADO : saldo_destino
 *   SECAO CRITICA         : ler saldo_destino -> somar valor -> gravar saldo_destino
 *   CONDICAO DE CORRIDA   : duas threads leem o mesmo saldo antigo (lost update)
 *
 * Compilar: gcc -O2 -Wall -pthread -o pix_parte1 pix_parte1.c
 * Uso     : ./pix_parte1 -n N [-p P] [-s semente] [-u us] [-v] [-q]
 */
#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define SALDO_INICIAL 0L

static int N = 100, P = 10, SEED = 1, CS_US = 100, VERBOSE = 0, QUIET = 0;

static long saldo_destino = SALDO_INICIAL;   /* <<< recurso compartilhado >>> */
static long *valor;                          /* valor enviado por cada requisicao (1..100) */
static volatile int proximo = 0;             /* proxima requisicao a ser atendida */
static pthread_mutex_t m_log = PTHREAD_MUTEX_INITIALIZER;  /* so' para nao embaralhar o printf */

static void *worker(void *arg) {
    int w = (int)(long)arg;
    for (;;) {
        int i = __sync_fetch_and_add(&proximo, 1);   /* pega a proxima requisicao da pool */
        if (i >= N) break;

        /* ---------- INICIO DA SECAO CRITICA (sem protecao!) ---------- */
        long lido = saldo_destino;                   /* 1) le o saldo         */
        if (VERBOSE) { pthread_mutex_lock(&m_log);
            printf("worker %2d | req#%-3d | LE    saldo=%ld  (vai somar %ld)\n", w, i + 1, lido, valor[i]);
            pthread_mutex_unlock(&m_log); }
        if (CS_US > 0) usleep(CS_US);                /*    latencia de acesso a base (-u 0 desliga) */
        saldo_destino = lido + valor[i];             /* 2) grava lido+valor (pode ser valor velho!) */
        if (VERBOSE) { pthread_mutex_lock(&m_log);
            printf("worker %2d | req#%-3d | GRAVA saldo=%ld\n", w, i + 1, lido + valor[i]);
            pthread_mutex_unlock(&m_log); }
        /* ---------- FIM DA SECAO CRITICA ---------- */
    }
    return NULL;
}

int main(int argc, char **argv) {
    int opt;
    while ((opt = getopt(argc, argv, "n:p:s:u:vq")) != -1) {
        switch (opt) {
        case 'n': N = atoi(optarg); break;
        case 'p': P = atoi(optarg); break;
        case 's': SEED = atoi(optarg); break;
        case 'u': CS_US = atoi(optarg); break;
        case 'v': VERBOSE = 1; break;
        case 'q': QUIET = 1; break;
        default: fprintf(stderr, "uso: %s -n N [-p P] [-s semente] [-u us] [-v] [-q]\n", argv[0]); return 1;
        }
    }
    if (N < 1) N = 1;
    if (P < 1) P = 1;

    /* gera os valores e calcula o saldo ESPERADO (soma sequencial, sem concorrencia) */
    valor = malloc(N * sizeof(long));
    long esperado = SALDO_INICIAL;
    srand(SEED);
    for (int i = 0; i < N; i++) { valor[i] = 1 + rand() % 100; esperado += valor[i]; }

    pthread_t *ws = malloc(P * sizeof(pthread_t));
    for (long w = 0; w < P; w++) pthread_create(&ws[w], NULL, worker, (void *)w);
    for (int w = 0; w < P; w++) pthread_join(ws[w], NULL);

    long perdido = esperado - saldo_destino;
    if (QUIET) {
        printf("RESULT N=%d P=%d perdido=%ld ok=%d\n", N, P, perdido, perdido == 0);
    } else {
        printf("\nN=%d envios simultaneos | pool de P=%d threads\n", N, P);
        printf("Saldo inicial : %ld\n", SALDO_INICIAL);
        printf("Saldo esperado: %ld  (inicial + soma dos %d envios)\n", esperado, N);
        printf("Saldo obtido  : %ld\n", saldo_destino);
        printf("Dinheiro perdido: R$ %ld  ->  %s\n", perdido, perdido ? "INCONSISTENTE (condicao de corrida)" : "consistente");
    }
    free(valor); free(ws);
    return 0;
}
