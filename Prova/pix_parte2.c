/*
 * INF0336 - A01 - Parte 2: protecao minima + escalonador FCFS
 *
 * Mesmo modelo N:1 da Parte 1 (N envios simultaneos -> 1 conta, atendidos por uma thread pool),
 * agora com DUAS adicoes:
 *
 *   (A) ESCALONADOR FCFS: as N requisicoes entram numa fila FIFO na ordem de chegada;
 *       a pool de P workers retira sempre da CABECA da fila. As requisicoes que superam
 *       o tamanho da pool esperam na fila e sao atendidas na ordem 1,2,3,...
 *   (B) EXCLUSAO MUTUA (mutex) na secao critica: so uma thread por vez le-soma-grava o saldo.
 *
 * Compilar: gcc -O2 -Wall -pthread -o pix_parte2 pix_parte2.c
 * Uso     : ./pix_parte2 -n N [-p P] [-t proc_ms] [-s semente] [-u us] [-k] [-q]e
 *           -k  desliga o mutex (so FCFS) para mostrar que ele e' indispensavel
 */
#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#define SALDO_INICIAL 0L

static int N = 100, P = 10, SEED = 1, CS_US = 100, PROC_MS = 0, QUIET = 0;
static int USA_MUTEX = 1;                    /* (B) 1 = protege a secao critica */

/* ---------- recurso compartilhado (igual a Parte 1) ---------- */
static long saldo_destino = SALDO_INICIAL;
static pthread_mutex_t m_saldo = PTHREAD_MUTEX_INITIALIZER;   /* (B) protege saldo_destino */

/* ---------- requisicoes ---------- */
typedef struct {
    int    id;            /* 1..N = ordem de chegada */
    long   valor;
    int    proc_ms;       /* tempo simulado de processamento (antifraude, rede...) */
    int    worker;
    int    ordem_inicio;  /* em que posicao foi retirada da fila */
    double t_chegada, t_inicio, t_fim;
} Req;
static Req *reqs;

/* ---------- (A) fila FCFS: buffer circular FIFO ---------- */
static int *fila;
static int f_head = 0, f_tail = 0, f_count = 0, iniciadas = 0;
static pthread_mutex_t m_fila = PTHREAD_MUTEX_INITIALIZER;    /* protege a propria fila */

static pthread_mutex_t m_log = PTHREAD_MUTEX_INITIALIZER;
static struct timespec t0;
static double agora(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (t.tv_sec - t0.tv_sec) + (t.tv_nsec - t0.tv_nsec) / 1e9;
}
#define LOG(...) do { if (!QUIET) { pthread_mutex_lock(&m_log); \
    printf("[%6.3fs] ", agora()); printf(__VA_ARGS__); printf("\n"); fflush(stdout); \
    pthread_mutex_unlock(&m_log); } } while (0)

static void *worker(void *arg) {
    int w = (int)(long)arg;
    for (;;) {
        /* ===== (A) ESCALONADOR FCFS: retira a requisicao mais antiga da fila ===== */
        pthread_mutex_lock(&m_fila);
        if (f_count == 0) { pthread_mutex_unlock(&m_fila); break; }     /* fila vazia: worker encerra */
        int id = fila[f_head]; f_head = (f_head + 1) % N; f_count--;    /* sai da CABECA */
        Req *r = &reqs[id];
        r->t_inicio = agora(); r->worker = w; r->ordem_inicio = iniciadas++;
        LOG("worker %d  INICIA   req#%-3d (R$ %3ld)   [ainda na fila: %d]", w, r->id, r->valor, f_count);
        pthread_mutex_unlock(&m_fila);

        /* trabalho fora da secao critica: continua EM PARALELO entre os workers */
        if (r->proc_ms > 0) usleep((useconds_t)r->proc_ms * 1000);

        /* ===== SECAO CRITICA (igual a Parte 1, agora protegida) ===== */
        if (USA_MUTEX) pthread_mutex_lock(&m_saldo);                    /* (B) entrada */
        long lido = saldo_destino;                                      /* le   */
        if (CS_US > 0) usleep(CS_US);                                   /* latencia de acesso a base */
        saldo_destino = lido + r->valor;                                /* grava */
        if (USA_MUTEX) pthread_mutex_unlock(&m_saldo);                  /* (B) saida */
        /* ===== FIM DA SECAO CRITICA ===== */

        r->t_fim = agora();
        LOG("worker %d  CONCLUI  req#%-3d", w, r->id);
    }
    return NULL;
}

int main(int argc, char **argv) {
    int opt;
    while ((opt = getopt(argc, argv, "n:p:s:t:u:kq")) != -1) {
        switch (opt) {
        case 'n': N = atoi(optarg); break;
        case 'p': P = atoi(optarg); break;
        case 's': SEED = atoi(optarg); break;
        case 't': PROC_MS = atoi(optarg); break;
        case 'u': CS_US = atoi(optarg); break;
        case 'k': USA_MUTEX = 0; break;
        case 'q': QUIET = 1; break;
        default: fprintf(stderr, "uso: %s -n N [-p P] [-t proc_ms] [-s semente] [-u us] [-k] [-q]\n", argv[0]); return 1;
        }
    }
    if (N < 1) N = 1;
    if (P < 1) P = 1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    reqs = calloc(N, sizeof(Req));
    fila = calloc(N, sizeof(int));
    long esperado = SALDO_INICIAL;
    srand(SEED);
    for (int i = 0; i < N; i++) {
        reqs[i].id = i + 1;
        reqs[i].valor = 1 + rand() % 100;
        reqs[i].proc_ms = PROC_MS > 0 ? PROC_MS / 2 + rand() % (PROC_MS / 2 + 1) : 0;
        esperado += reqs[i].valor;
    }

    /* todas as N requisicoes chegam no MESMO instante: entram na fila na ordem 1..N */
    for (int i = 0; i < N; i++) {
        reqs[i].t_chegada = agora();
        fila[f_tail] = i; f_tail = (f_tail + 1) % N; f_count++;
    }
    LOG("CHEGAM %d requisicoes ao mesmo tempo. Fila FCFS: req#1 ... req#%d | pool de P=%d threads", N, N, P);

    pthread_t *ws = malloc(P * sizeof(pthread_t));
    for (long w = 0; w < P; w++) pthread_create(&ws[w], NULL, worker, (void *)w);
    for (int w = 0; w < P; w++) pthread_join(ws[w], NULL);

    /* ---------- verificacoes ---------- */
    long perdido = esperado - saldo_destino;
    int fcfs = 1;
    double soma_esp = 0, max_esp = 0;
    for (int i = 0; i < N; i++) {
        if (reqs[i].ordem_inicio != i) fcfs = 0;                 /* FCFS: i-esima a chegar = i-esima a iniciar */
        double e = reqs[i].t_inicio - reqs[i].t_chegada;
        soma_esp += e; if (e > max_esp) max_esp = e;
    }
    if (QUIET) {
        printf("RESULT N=%d P=%d perdido=%ld ok=%d fcfs=%d espera_media=%.4f\n", N, P, perdido, perdido == 0, fcfs, soma_esp / N);
    } else {
        printf("\n---- Linha do tempo (chegada = 0 para todas) ----\n");
        printf(" req | worker | inicio(s) | fim(s) | espera na fila(s)\n");
        for (int i = 0; i < N && i < 60; i++)
            printf(" #%-3d|   %2d   |  %6.3f   | %6.3f |  %6.3f\n", reqs[i].id, reqs[i].worker,
                   reqs[i].t_inicio, reqs[i].t_fim, reqs[i].t_inicio - reqs[i].t_chegada);
        printf("\n---- Verificacao ----\n");
        printf("Saldo esperado: %ld | obtido: %ld | perdido: R$ %ld -> %s\n", esperado, saldo_destino, perdido,
               perdido ? "INCONSISTENTE" : "CONSISTENTE");
        printf("Ordem de atendimento = ordem de chegada (FCFS): %s\n", fcfs ? "SIM" : "NAO");
        printf("Espera media na fila: %.3fs | maxima: %.3fs\n", soma_esp / N, max_esp);
    }
    free(reqs); free(fila); free(ws);
    return 0;
}
