#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#include <signal.h>
#include <sys/signal.h>
#include <unistd.h>
#include <sched.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <getopt.h>
#include <hiredis/hiredis.h>

#define MAX_PROCS 100

typedef struct {
    int port_start;
    int num_proc;
    char *socket_base_dir;
    char *output_base_dir;
} redis_launch_args_t;

typedef struct {
    int num_proc;
    unsigned short cpu_ids[MAX_PROCS];
    unsigned short ports[MAX_PROCS];
    pid_t pids[MAX_PROCS];
    char *socket_base_dir;
    char *output_base_dir;
    char *socket_paths[MAX_PROCS];
    char *output_paths[MAX_PROCS];
} redis_launch_config_t;

static volatile int keepRunning = 1;
static redis_launch_args_t *redis_args = NULL;
static redis_launch_config_t *redis_conf = NULL;

void cleanup() {
    if (redis_conf) {
        // Send SIGTERM to all Redis processes
        for (int i = 0; i < redis_conf->num_proc; i++) {
            if (redis_conf->socket_paths[i]) {
                free(redis_conf->socket_paths[i]);
            }

            if (redis_conf->pids[i] > 0) {
                printf("Terminating Redis worker with PID %d\n", redis_conf->pids[i]);
                kill(redis_conf->pids[i], SIGTERM);
            }
        }
        sleep(1);

        free(redis_conf);
    }

    if (redis_args) {
        free(redis_args);
    }
}

int set_cpu_affinity(unsigned short *core_id, pid_t *pid) {
    cpu_set_t cpu_set;
    CPU_ZERO(&cpu_set);
    CPU_SET(*core_id, &cpu_set);

    if (sched_setaffinity(*pid, sizeof(cpu_set_t), &cpu_set) == -1) {
        perror("sched_setaffinity failed");
        return -1;
    }

    return 0;
}

int launch_redis_process(unsigned short *port, unsigned short *cpu_num, pid_t *pid, char *socket_path) {

    *pid = fork();
    if (*pid == -1) {
        perror("Fork failed!");
        return -1;
    }

    if (*pid == 0) {
        // inside the child process

        if (set_cpu_affinity(cpu_num, pid) == -1) {
            fprintf(stderr, "Failed to set CPU affinity!\n");
            cleanup();
            exit(1);
        }
        char *port_str = "";
        if (!socket_path) {
            sprintf(port_str, "%d", *port);
        } else {
            port_str = "0";
            printf("Setting port to 0, as unix domain socket was chosen -> %s\n", socket_path);
        }

        char *args[] = {
            "redis-server",
            "--unixsocket", socket_path,
            "--unixsocketperm", "700",
            "--port", port_str,
            "--maxmemory", "4gb",
            "--appendonly", "no",
            "--save", "",
            NULL 
        };
        
        printf("Launching redis server...\n");
        int arg_len = sizeof(args) / sizeof(args[0]);

        for (int it=0; it<arg_len; it++) {
            char *arg = args[it];
            if (it == 0) { 
                printf("=== Launch Args ===\n");
            }
            char *sep = (it > 0 && ((it - 1) % 2)) ? "\n" : " ";
            printf("%s%s", arg, sep);
        }

        char log_filename[1024];
        sprintf(log_filename, ".redis/redis_%d.log", *cpu_num);

        int log_fd = open(log_filename, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (log_fd == -1) {
            perror("Error opening logfile fd");
            cleanup();
            exit(1);
        }

        dup2(log_fd, STDOUT_FILENO);
        dup2(log_fd, STDERR_FILENO);

        close(log_fd);

        execvp("redis-server", args);
        perror("Launching redis server failed!");
        cleanup();
        exit(1);
    }

    sleep(1);

    return 0;
}

int is_redis_running(unsigned short *port, char *socket_path) {
    redisReply *reply = NULL;
    redisContext *ctx = NULL;

    if (socket_path) {
        ctx = redisConnectUnix(socket_path);
    } else if (*port != 0) {
        ctx = redisConnect("localhost", *port);
    } else {
        fprintf(stderr, "Invalid Config...\n");

        freeReplyObject(reply);
        redisFree(ctx);

        return -1;
    }

    reply = redisCommand(ctx, "PING");
    if (!reply) {
        perror("Unable to get redis reply");

        freeReplyObject(reply);
        redisFree(ctx);

        return -1;
    }

    printf("Redis Reply -> %s\n", reply->str);

    if (strcmp(reply->str, "PONG") != 0) {
        fprintf(stderr, "Invalid reply from redis for PING -> %s\n", reply->str);

        freeReplyObject(reply);
        redisFree(ctx);

        return -1;
    }

    freeReplyObject(reply);
    redisFree(ctx);

    return 0;
}

static void _signal_handler(int signo){
	if (signo == SIGTERM || signo == SIGINT){
		perror("Interrupt Signal Received! Shutting down...");
        keepRunning = 0;
        cleanup();
		exit(signo);
	}
}

int initialize_redis_conf(redis_launch_args_t *args) {
    if (!args) {
        return -1;
    }

    assert(args->num_proc <= MAX_PROCS);

    int num_cpus = sysconf(_SC_NPROCESSORS_ONLN);
    if (args->num_proc > num_cpus) {
        fprintf(stderr, "num_proc=%d cannot be greater than num_cpu=%d available!\n", args->num_proc, num_cpus);
    }

    redis_conf = malloc(sizeof(redis_launch_config_t));
    if (!redis_conf) {
        fprintf(stderr, "malloc failed for redis_conf!\n");
        cleanup();
        return -1;
    }

    redis_conf->num_proc = args->num_proc;
    // redis_conf->cpu_ids = malloc(args->num_proc * sizeof(unsigned short));
    // redis_conf->ports = malloc(args->num_proc * sizeof(unsigned short));
    // redis_conf->pids = malloc(args->num_proc * sizeof(unsigned int));
    redis_conf->output_base_dir = args->output_base_dir;
    redis_conf->socket_base_dir = args->socket_base_dir;

    // if (!redis_conf->cpu_ids || !redis_conf->ports || !redis_conf->pids) {
    //     perror("malloc failed when initializing redis_conf!\n");
    //     cleanup();
    //     return -1;
    // }


    for (int i=0; i < args->num_proc; i++) {
        redis_conf->cpu_ids[i] = (unsigned short)i;
        redis_conf->ports[i] = (unsigned short)(args->port_start + i);
        redis_conf->pids[i] = 0;
    }

    return 0;
}

int path_exists(char *path) {
    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
}

char* join_paths(char *base, char *sub) {
    if (!base || !sub) return NULL;

    if (!path_exists(base)) {
        fprintf(stderr, "Base dir [%s] does not exist!\n", base);
        return NULL;
    }

    size_t base_len = strlen(base);
    size_t sub_len = strlen(base);

    int need_slash = (base_len > 0 && base[base_len - 1] != '/');
    int skip_sub_slash = (sub_len > 0 && sub[0] == '/');

    size_t new_len = base_len + sub_len + (need_slash ? 1 : 0) - (skip_sub_slash ? 1 : 0) + 1;
    char *joined = malloc(new_len);
    if (!joined) return NULL;

    strcpy(joined, base);
    if (need_slash) strcat(joined, "/");
    strcat(joined, skip_sub_slash ? sub + 1 : sub);

    return joined;
}

int launch_redis_processes(redis_launch_config_t *redis_conf, int verify) {
    if (!redis_conf) {
        return -1;
    }

    for (int i = 0; i < redis_conf->num_proc; i++) {
        assert(i < MAX_PROCS);

        char sock_file_path[512];
        sprintf(sock_file_path, "redis_worker_%d.sock", i);

        printf("Socket file name -> %s\n", sock_file_path);

        char *sock_full_path = join_paths(redis_conf->socket_base_dir, sock_file_path);
        if (sock_full_path == NULL) {
            fprintf(stderr, "Error creating socket string!\n");
            return -1;
        }
        printf("Socket file path -> %s\n", sock_full_path);

        int launch_status = launch_redis_process(&redis_conf->ports[i], &redis_conf->cpu_ids[i], &redis_conf->pids[i], sock_full_path);
        if (launch_status == -1) {
            fprintf(stderr, "Error launching redis process - %d\n", i);
            perror("Error launching redis process:");
            return -1;
        }

        redis_conf->socket_paths[i] = sock_full_path;

        printf("Redis worker [%d] launched with PID -> %d\n", i, redis_conf->pids[i]);

        if (verify) {
            printf("Verifying redis worker [%d]...\n", i);
            sleep(5);

            printf("Socket Path = %s | Socket exists = %d\n", sock_full_path, path_exists(sock_full_path));

            if (is_redis_running(NULL, sock_full_path) == -1) {
                fprintf(stderr, "Status check failed for Redis Worker [%d]\n", i);
                return -1;
            }
        }

    }

    return 0;
}

int main() {

    // int port = 6380;
    // int cpu_id = 0;
    // pid_t pid;
    // char *socket_path = ".redis/redis_test.sock";

    redis_args = malloc(sizeof(redis_launch_args_t));
    redis_args->num_proc = 4;
    redis_args->port_start = 6380;
    redis_args->socket_base_dir = "/home/abhinand/dev/github/threadis/temp/.redis";
    redis_args->output_base_dir = "/home/abhinand/dev/github/threadis/temp/.redis";

    if (initialize_redis_conf(redis_args) == -1) {
        perror("Failed to initialize redis config...");
        cleanup();
        exit(1);
    }

    // if (launch_redis_process(&port, &cpu_id, &pid, socket_path) == -1) {
    //     perror("Failed to launch redis process...");
    //     cleanup();
    //     exit(1);
    // }

    // printf("Created Process ID -> %d\n", pid);

    if (launch_redis_processes(redis_conf, 1) == -1) {
        perror("Failed to launch redis process...");
        cleanup();
        exit(1);
    }

    if (SIG_ERR == signal(SIGINT, _signal_handler)){
		fprintf(stderr,"Unable to catch SIGINT...exiting.\n");
		exit(1);
	}
	if (SIG_ERR == signal(SIGTERM, _signal_handler)){
		fprintf(stderr,"Unable to catch SIGTERM...exiting.\n");
		exit(1);
	}

    for (int it=0; it<5; it++) {
        sleep(5);
        printf("----- Test [%d] -----\n", it);
        // exit(1);

        for (int i = 0; i < redis_conf->num_proc; i++) {
            printf("Pinging Redis Worker [%d]...\n", i);
            if (is_redis_running(&redis_conf->ports[i], redis_conf->socket_paths[i]) == -1) {
                fprintf(stderr, "Error pinging Redis Worker [%d]\n", i);
                continue;
            }

            printf("OK!\n");
        }

        printf("----- End Test [%d] -----\n", it);
    }

    cleanup();
}
