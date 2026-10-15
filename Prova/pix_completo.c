/*
 * INF0336 - A01 - Sistema PIX (modelo N:1) - Partes 1 + 2 + 3
 *
 * N remetentes enviam dinheiro AO MESMO TEMPO para UMA unica conta de destino,
 * atendidos por uma thread pool de P workers.
 *
 *   RECURSO COMPARTILHADO : saldo_destino
 *   SECAO CRITICA         : ler saldo_destino -> somar valor -> gravar saldo_destino
 *   CONDICAO DE CORRIDA   : duas threads leem o mesmo saldo antigo (lost update)
 *
 * PARTE 1 (-k -u 100 ...) : sem protecao -> mostra a condicao de corrida (use -k)
 * PARTE 2                 : escalonador FCFS (fila FIFO) + mutex na secao critica
 * PARTE 3                 : debugging por SINAIS. Ao pressionar QUALQUER TECLA, a tecla
 *                           dispara SIGUSR1 para o proprio processo; uma thread dedicada
 *                           (sigwait) captura o sinal e imprime o estado atual da fila.
 *                           Tambem funciona de fora:  kill -USR1 <pid>
 *                           Ctrl+C (SIGINT) encerra restaurando o terminal.
 *
 * Compilar: gcc -O2 -Wall -pthread -o pix_completo pix_completo.c
 * Uso     : ./pix_completo -n N [-p P] [-t proc_ms] [-s semente] [-u us] [-a ms] [-k] [-q]
 *           -k  desliga o mutex (so FCFS): mostra a condicao de corrida
 *           -a  imprime o estado da fila automaticamente a cada 'ms' (para gravar o video
 *               ou testar sem teclado), usando o mesmo mecanismo de sinal
 * Exemplo : ./pix_completo -n 30 -p 5 -t 600     (e va apertando teclas)
 */
#define _GNU_SOURCE
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <termios.h>
#include <sys/select.h>

#define SALDO_INICIAL 0L

static int N = 100, P = 10, SEED = 1, CS_US = 100, PROC_MS = 300, QUIET = 0, AUTO_MS = 0;
static int USA_MUTEX = 1;

/* ---------- recurso compartilhado ---------- */
static long saldo_destino = SALDO_INICIAL;
static pthread_mutex_t m_saldo = PTHREAD_MUTEX_INITIALIZER;

/* ---------- requisicoes ---------- */
enum { NA_FILA = 0, PROCESSANDO, ESPERA_MUTEX, SECAO_CRITICA, CONCLUIDA };
static const char *NOME_ESTADO[] = { "na fila", "processando (antifraude/rede)",
                                     "aguardando mutex do saldo", "NA SECAO CRITICA", "concluida" };
typedef struct {
    int    id;
    long   valor;
    int    proc_ms;
    int    worker;
    int    ordem_inicio;
    int    estado;
    double t_chegada, t_inicio, t_fim;
} Req;
static Req *reqs;

/* ---------- fila FCFS: buffer circular FIFO ---------- */
static int *fila;
static int f_head = 0, f_tail = 0, f_count = 0, iniciadas = 0, concluidas = 0;
static pthread_mutex_t m_fila = PTHREAD_MUTEX_INITIALIZER;

static pthread_mutex_t m_log = PTHREAD_MUTEX_INITIALIZER;
static struct timespec t0;
static double agora(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (t.tv_sec - t0.tv_sec) + (t.tv_nsec - t0.tv_nsec) / 1e9;
}
#define LOG(...) do { if (!QUIET) { pthread_mutex_lock(&m_log); \
    printf("[%6.3fs] ", agora()); printf(__VA_ARGS__); printf("\n"); fflush(stdout); \
    pthread_mutex_unlock(&m_log); } } while (0)

/* ---------- PARTE 3: controle de terminal e sinais ---------- */
static struct termios term_orig;
static int term_alterado = 0;
static volatile int encerrar = 0;     /* avisa as threads auxiliares que a simulacao acabou */
static sigset_t conj_sinais;          /* {SIGUSR1, SIGINT}: bloqueados em todas as threads */
static pid_t meu_pid;

static void restaura_terminal(void) {
    if (term_alterado) { tcsetattr(STDIN_FILENO, TCSANOW, &term_orig); term_alterado = 0; }
}

/* Mostra o estado atual da fila e dos workers (chamado pela thread de debug) */
static void dump_estado(void) {
    pthread_mutex_lock(&m_log);
    pthread_mutex_lock(&m_fila);               /* foto consistente da fila */
    int na_fila = f_count, feitas = concluidas;
    printf("\n========== [DEBUG] estado em t=%.3fs ==========\n", agora());
    printf("Fila FCFS (cabeca -> cauda), %d aguardando:\n  ", na_fila);
    if (na_fila == 0) printf("(vazia)");
    for (int k = 0; k < na_fila && k < 20; k++) {
        int id = fila[(f_head + k) % N];
        printf("%s#%d(R$%ld)", k ? " -> " : "[CABECA] ", reqs[id].id, reqs[id].valor);
    }
    if (na_fila > 20) printf(" -> ... (+%d)", na_fila - 20);
    printf("\nEm atendimento:\n");
    int ativos = 0;
    for (int i = 0; i < N; i++) {
        int e = __atomic_load_n(&reqs[i].estado, __ATOMIC_RELAXED);
        if (e == PROCESSANDO || e == ESPERA_MUTEX || e == SECAO_CRITICA) {
            printf("  worker %2d <- req#%-3d (R$ %3ld)  %s\n", reqs[i].worker, reqs[i].id, reqs[i].valor, NOME_ESTADO[e]);
            ativos++;
        }
    }
    if (!ativos) printf("  (nenhum)\n");
    pthread_mutex_unlock(&m_fila);
    printf("Resumo: %d na fila | %d em atendimento | %d concluidas | %d total | saldo atual = %ld\n",
           na_fila, ativos, feitas, N, __atomic_load_n(&saldo_destino, __ATOMIC_RELAXED));
    printf("==================================================\n\n");
    fflush(stdout);
    pthread_mutex_unlock(&m_log);
}

/* Thread dedicada: espera os sinais com sigwait (assim pode usar mutex/printf com seguranca,
 * o que seria proibido dentro de um handler assincrono). */
static void *thread_debug(void *arg) {
    (void)arg;
    for (;;) {
        int sig;
        sigwait(&conj_sinais, &sig);
        if (encerrar) break;
        if (sig == SIGINT) { restaura_terminal(); fprintf(stderr, "\nInterrompido (SIGINT).\n"); _exit(130); }
        if (sig == SIGUSR1) dump_estado();
    }
    return NULL;
}

/* Thread do teclado: terminal sem modo canonico -> cada tecla vira um byte imediatamente
 * e dispara SIGUSR1 contra o proprio processo. */
static void *thread_teclado(void *arg) {
    (void)arg;
    while (!encerrar) {
        fd_set fds; FD_ZERO(&fds); FD_SET(STDIN_FILENO, &fds);
        struct timeval tv = { 0, 100000 };
        if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0) {
            char c;
            if (read(STDIN_FILENO, &c, 1) == 1) kill(meu_pid, SIGUSR1);
        }
    }
    return NULL;
}

/* Gatilho automatico opcional (-a): equivale a "apertar uma tecla" a cada AUTO_MS */
static void *thread_auto(void *arg) {
    (void)arg;
    while (!encerrar) {
        usleep((useconds_t)AUTO_MS * 1000);
        if (!encerrar) kill(meu_pid, SIGUSR1);
    }
    return NULL;
}

/* ---------- worker ---------- */
static void *worker(void *arg) {
    int w = (int)(long)arg;
    for (;;) {
        /* ===== ESCALONADOR FCFS: retira a requisicao mais antiga da fila ===== */
        pthread_mutex_lock(&m_fila);
        if (f_count == 0) { pthread_mutex_unlock(&m_fila); break; }
        int id = fila[f_head]; f_head = (f_head + 1) % N; f_count--;
        Req *r = &reqs[id];
        r->t_inicio = agora(); r->worker = w; r->ordem_inicio = iniciadas++;
        __atomic_store_n(&r->estado, PROCESSANDO, __ATOMIC_RELAXED);
        int restante = f_count;
        pthread_mutex_unlock(&m_fila);
        LOG("worker %d  INICIA   req#%-3d (R$ %3ld)   [ainda na fila: %d]", w, r->id, r->valor, restante);

        /* trabalho fora da secao critica: EM PARALELO entre os workers */
        if (r->proc_ms > 0) usleep((useconds_t)r->proc_ms * 1000);

        /* ===== SECAO CRITICA ===== */
        __atomic_store_n(&r->estado, ESPERA_MUTEX, __ATOMIC_RELAXED);
        if (USA_MUTEX) pthread_mutex_lock(&m_saldo);
        __atomic_store_n(&r->estado, SECAO_CRITICA, __ATOMIC_RELAXED);
        long lido = saldo_destino;
        if (CS_US > 0) usleep(CS_US);
        saldo_destino = lido + r->valor;
        if (USA_MUTEX) pthread_mutex_unlock(&m_saldo);
        /* ===== FIM DA SECAO CRITICA ===== */

        r->t_fim = agora();
        pthread_mutex_lock(&m_fila);
        __atomic_store_n(&r->estado, CONCLUIDA, __ATOMIC_RELAXED);
        concluidas++;
        pthread_mutex_unlock(&m_fila);
        LOG("worker %d  CONCLUI  req#%-3d", w, r->id);
    }
    return NULL;
}

int main(int argc, char **argv) {
    int opt;
    while ((opt = getopt(argc, argv, "n:p:s:t:u:a:kq")) != -1) {
        switch (opt) {
        case 'n': N = atoi(optarg); break;
        case 'p': P = atoi(optarg); break;
        case 's': SEED = atoi(optarg); break;
        case 't': PROC_MS = atoi(optarg); break;
        case 'u': CS_US = atoi(optarg); break;
        case 'a': AUTO_MS = atoi(optarg); break;
        case 'k': USA_MUTEX = 0; break;
        case 'q': QUIET = 1; break;
        default: fprintf(stderr, "uso: %s -n N [-p P] [-t proc_ms] [-s semente] [-u us] [-a ms] [-k] [-q]\n", argv[0]); return 1;
        }
    }
    if (N < 1) N = 1;
    if (P < 1) P = 1;
    meu_pid = getpid();
    clock_gettime(CLOCK_MONOTONIC, &t0);

    /* bloqueia os sinais ANTES de criar threads: todas herdam a mascara e so a thread_debug os recebe */
    sigemptyset(&conj_sinais);
    sigaddset(&conj_sinais, SIGUSR1);
    sigaddset(&conj_sinais, SIGINT);
    pthread_sigmask(SIG_BLOCK, &conj_sinais, NULL);

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

    /* todas as N requisicoes chegam no MESMO instante (um unico timestamp) */
    double t_chegada = agora();
    for (int i = 0; i < N; i++) {
        reqs[i].t_chegada = t_chegada;
        fila[f_tail] = i; f_tail = (f_tail + 1) % N; f_count++;
    }
    LOG("CHEGAM %d requisicoes ao mesmo tempo. Fila FCFS: req#1 ... req#%d | pool de P=%d threads", N, N, P);

    /* threads auxiliares da parte 3 */
    pthread_t t_dbg, t_key, t_aut;
    int tem_key = 0, tem_aut = 0;
    pthread_create(&t_dbg, NULL, thread_debug, NULL);
    if (!QUIET && isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &term_orig) == 0) {
        struct termios raw = term_orig;
        raw.c_lflag &= ~(ICANON | ECHO);          /* cada tecla chega na hora, sem ENTER (ISIG mantido) */
        raw.c_cc[VMIN] = 1; raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
        term_alterado = 1;
        atexit(restaura_terminal);
        pthread_create(&t_key, NULL, thread_teclado, NULL);
        tem_key = 1;
        LOG("DEBUG ativo: pressione QUALQUER TECLA (ou kill -USR1 %d) para ver o estado da fila", (int)meu_pid);
    }
    if (AUTO_MS > 0) { pthread_create(&t_aut, NULL, thread_auto, NULL); tem_aut = 1; }

    pthread_t *ws = malloc(P * sizeof(pthread_t));
    for (long w = 0; w < P; w++) pthread_create(&ws[w], NULL, worker, (void *)w);
    for (int w = 0; w < P; w++) pthread_join(ws[w], NULL);

    /* encerra as threads auxiliares */
    encerrar = 1;
    kill(meu_pid, SIGUSR1);                        /* acorda a thread_debug para ela sair */
    pthread_join(t_dbg, NULL);
    if (tem_key) pthread_join(t_key, NULL);
    if (tem_aut) pthread_join(t_aut, NULL);
    restaura_terminal();

    /* ---------- verificacoes ---------- */
    long perdido = esperado - saldo_destino;
    int fcfs = 1;
    double soma_esp = 0, max_esp = 0;
    for (int i = 0; i < N; i++) {
        if (reqs[i].ordem_inicio != i) fcfs = 0;
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
