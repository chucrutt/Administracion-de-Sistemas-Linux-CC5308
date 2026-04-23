#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <fcntl.h>

typedef struct {
    char** entries;
    size_t size;
    size_t capacity;
} History;

typedef struct {
    int should_exit;
    int exit_status;
} CommandResult;

typedef struct {
    char** argv;
    char* stdin_path;
    char* stdout_path;
    int parse_ok;
} ParsedCommand;

static History g_history = {NULL, 0, 0};

int is_blank_line(const char* line) {
    if (line == NULL) {
        return 1;
    }

    for (size_t i = 0; line[i] != '\0'; i++) {
        if (line[i] != ' ' && line[i] != '\t' && line[i] != '\n' && line[i] != '\r') {
            return 0;
        }
    }

    return 1;
}

void add_history(const char* line) {
    if (is_blank_line(line)) {
        return;
    }

    char* entry = strdup(line);
    if (entry == NULL) {
        perror("strdup");
        return;
    }

    size_t len = strlen(entry);
    if (len > 0 && entry[len - 1] == '\n') {
        entry[len - 1] = '\0';
    }

    if (g_history.size == g_history.capacity) {
        size_t new_capacity = (g_history.capacity == 0) ? 16 : g_history.capacity * 2;
        char** tmp = realloc(g_history.entries, new_capacity * sizeof(char*));
        if (tmp == NULL) {
            free(entry);
            perror("realloc");
            return;
        }
        g_history.entries = tmp;
        g_history.capacity = new_capacity;
    }

    g_history.entries[g_history.size++] = entry;
}

void free_history() {
    for (size_t i = 0; i < g_history.size; i++) {
        free(g_history.entries[i]);
    }
    free(g_history.entries);
    g_history.entries = NULL;
    g_history.size = 0;
    g_history.capacity = 0;
}

void run_pwd_builtin() {
    size_t size = 128;

    while (1) {
        char* cwd = malloc(size);
        if (cwd == NULL) {
            perror("malloc");
            return;
        }

        if (getcwd(cwd, size) != NULL) {
            printf("%s\n", cwd);
            free(cwd);
            return;
        }

        if (errno == ERANGE) {
            free(cwd);
            if (size > (SIZE_MAX / 2)) {
                fprintf(stderr, "pwd: path is too long\n");
                return;
            }
            size *= 2;
            continue;
        }

        perror("pwd");
        free(cwd);
        return;
    }
}

int parse_exit_status(const char* text, int* status) {
    if (text == NULL || status == NULL) {
        return -1;
    }

    errno = 0;
    char* endptr = NULL;
    long value = strtol(text, &endptr, 10);

    if (errno != 0 || text[0] == '\0' || *endptr != '\0' || value < INT_MIN || value > INT_MAX) {
        return -1;
    }

    *status = (int)value;
    return 0;
}

CommandResult run_builtin(char** cmd) {
    CommandResult result = {0, 0};

    if (strcmp(cmd[0], "cd") == 0) {
        if (cmd[2] != NULL) {
            fprintf(stderr, "cd: too many arguments\n");
            return result;
        }

        const char* target = cmd[1];
        if (target == NULL) {
            target = getenv("HOME");
            if (target == NULL) {
                fprintf(stderr, "cd: HOME not set\n");
                return result;
            }
        }

        if (chdir(target) == -1) {
            perror("cd");
        }
        return result;
    }

    if (strcmp(cmd[0], "exit") == 0) {
        if (cmd[2] != NULL) {
            fprintf(stderr, "exit: too many arguments\n");
            return result;
        }

        int status = 0;
        if (cmd[1] != NULL && parse_exit_status(cmd[1], &status) != 0) {
            fprintf(stderr, "exit: numeric argument required\n");
            return result;
        }

        result.should_exit = 1;
        result.exit_status = status;
        return result;
    }

    if (strcmp(cmd[0], "pwd") == 0) {
        if (cmd[1] != NULL) {
            fprintf(stderr, "pwd: too many arguments\n");
            return result;
        }

        run_pwd_builtin();
        return result;
    }

    if (strcmp(cmd[0], "export") == 0) {
        if (cmd[1] == NULL || cmd[2] != NULL) {
            fprintf(stderr, "export: usage export NAME=VALUE\n");
            return result;
        }

        char* equal = strchr(cmd[1], '=');
        if (equal == NULL || equal == cmd[1]) {
            fprintf(stderr, "export: usage export NAME=VALUE\n");
            return result;
        }

        size_t name_len = (size_t)(equal - cmd[1]);
        char* name = malloc(name_len + 1);
        if (name == NULL) {
            perror("malloc");
            return result;
        }
        memcpy(name, cmd[1], name_len);
        name[name_len] = '\0';

        const char* value = equal + 1;
        if (setenv(name, value, 1) == -1) {
            perror("export");
        }
        free(name);
        return result;
    }

    if (strcmp(cmd[0], "unset") == 0) {
        if (cmd[1] == NULL || cmd[2] != NULL) {
            fprintf(stderr, "unset: usage unset NAME\n");
            return result;
        }

        if (unsetenv(cmd[1]) == -1) {
            perror("unset");
        }
        return result;
    }

    if (strcmp(cmd[0], "history") == 0) {
        if (cmd[1] != NULL) {
            fprintf(stderr, "history: too many arguments\n");
            return result;
        }

        for (size_t i = 0; i < g_history.size; i++) {
            printf("%zu %s\n", i + 1, g_history.entries[i]);
        }
        return result;
    }

    result.should_exit = -1;
    return result;
}

int is_builtin_command(const char* name) {
    if (name == NULL) {
        return 0;
    }

    return strcmp(name, "cd") == 0 ||
           strcmp(name, "exit") == 0 ||
           strcmp(name, "pwd") == 0 ||
           strcmp(name, "export") == 0 ||
           strcmp(name, "unset") == 0 ||
           strcmp(name, "history") == 0;
}

void show_prompt() {
    printf("mi-shell-prompt> ");
    fflush(stdout);
}

char* read_input() {
    char* line = NULL;
    size_t len = 0;
    if (getline(&line, &len, stdin) == -1) {
        free(line);
        exit(0);
    }
    return line;
}

ParsedCommand parse_command(char* buf) {
    ParsedCommand parsed = {NULL, NULL, NULL, 1};

    size_t capacity = 8;
    size_t count = 0;
    char** argv = malloc(capacity * sizeof(char*));
    if (argv == NULL) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    char* saveptr = NULL;
    char* token = strtok_r(buf, " \t\n", &saveptr);

    while (token != NULL) {
        if (strcmp(token, "<") == 0 || strcmp(token, ">") == 0) {
            int is_input = (token[0] == '<');
            char* path = strtok_r(NULL, " \t\n", &saveptr);

            if (path == NULL) {
                fprintf(stderr, "syntax error: missing file after %s\n", token);
                parsed.parse_ok = 0;
                break;
            }

            if (is_input) {
                if (parsed.stdin_path != NULL) {
                    fprintf(stderr, "syntax error: multiple input redirections\n");
                    parsed.parse_ok = 0;
                    break;
                }
                parsed.stdin_path = path;
            } else {
                if (parsed.stdout_path != NULL) {
                    fprintf(stderr, "syntax error: multiple output redirections\n");
                    parsed.parse_ok = 0;
                    break;
                }
                parsed.stdout_path = path;
            }

            token = strtok_r(NULL, " \t\n", &saveptr);
            continue;
        }

        if (count + 1 >= capacity) {
            capacity *= 2;
            char** tmp = realloc(argv, capacity * sizeof(char*));
            if (tmp == NULL) {
                free(argv);
                perror("realloc");
                exit(EXIT_FAILURE);
            }
            argv = tmp;
        }

        argv[count++] = token;
        token = strtok_r(NULL, " \t\n", &saveptr);
    }

    if (!parsed.parse_ok) {
        free(argv);
        parsed.argv = NULL;
        return parsed;
    }

    if (count == 0) {
        if (parsed.stdin_path != NULL || parsed.stdout_path != NULL) {
            fprintf(stderr, "syntax error: missing command\n");
            parsed.parse_ok = 0;
        }
        free(argv);
        parsed.argv = NULL;
        return parsed;
    }

    argv[count] = NULL;
    parsed.argv = argv;
    return parsed;
}

void free_parsed_command(ParsedCommand* parsed) {
    if (parsed == NULL) {
        return;
    }

    free(parsed->argv);
    parsed->argv = NULL;
    parsed->stdin_path = NULL;
    parsed->stdout_path = NULL;
    parsed->parse_ok = 1;
}

int apply_redirections(const ParsedCommand* parsed) {
    if (parsed->stdin_path != NULL) {
        int in_fd = open(parsed->stdin_path, O_RDONLY);
        if (in_fd == -1) {
            perror(parsed->stdin_path);
            return -1;
        }

        if (dup2(in_fd, STDIN_FILENO) == -1) {
            perror("dup2");
            close(in_fd);
            return -1;
        }
        close(in_fd);
    }

    if (parsed->stdout_path != NULL) {
        int out_fd = open(parsed->stdout_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (out_fd == -1) {
            perror(parsed->stdout_path);
            return -1;
        }

        if (dup2(out_fd, STDOUT_FILENO) == -1) {
            perror("dup2");
            close(out_fd);
            return -1;
        }
        close(out_fd);
    }

    return 0;
}

void restore_fd_if_needed(int saved_fd, int target_fd) {
    if (saved_fd == -1) {
        return;
    }

    if (dup2(saved_fd, target_fd) == -1) {
        perror("dup2");
    }
    close(saved_fd);
}

CommandResult execute_command(const ParsedCommand* parsed) {
    CommandResult result = {0, 0};

    if (parsed == NULL || parsed->argv == NULL || !parsed->parse_ok) {
        return result;
    }

    if (is_builtin_command(parsed->argv[0])) {
        int saved_stdin = -1;
        int saved_stdout = -1;

        if (parsed->stdin_path != NULL) {
            saved_stdin = dup(STDIN_FILENO);
            if (saved_stdin == -1) {
                perror("dup");
                return result;
            }
        }

        if (parsed->stdout_path != NULL) {
            saved_stdout = dup(STDOUT_FILENO);
            if (saved_stdout == -1) {
                perror("dup");
                restore_fd_if_needed(saved_stdin, STDIN_FILENO);
                return result;
            }
        }

        if (apply_redirections(parsed) == -1) {
            restore_fd_if_needed(saved_stdin, STDIN_FILENO);
            restore_fd_if_needed(saved_stdout, STDOUT_FILENO);
            return result;
        }

        result = run_builtin(parsed->argv);
        restore_fd_if_needed(saved_stdin, STDIN_FILENO);
        restore_fd_if_needed(saved_stdout, STDOUT_FILENO);
        return result;
    }

    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        return result;
    }

    if (pid == 0) {
        if (apply_redirections(parsed) == -1) {
            exit(EXIT_FAILURE);
        }

        execvp(parsed->argv[0], parsed->argv);
        perror("execvp");
        exit(EXIT_FAILURE);
    }

    int status;
    while (waitpid(pid, &status, 0) == -1) {
        if (errno == EINTR) {
            continue;
        }
        perror("waitpid");
        break;
    }

    return result;
}

int main(void) {
    while (1) {
        show_prompt();
        char* buf = read_input();
        add_history(buf);
        ParsedCommand parsed = parse_command(buf);
        CommandResult result = execute_command(&parsed);
        free_parsed_command(&parsed);
        free(buf);

        if (result.should_exit == 1) {
            free_history();
            return result.exit_status;
        }
    }

    free_history();
    return 0;
}

